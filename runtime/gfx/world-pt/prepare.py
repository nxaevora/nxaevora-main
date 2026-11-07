"""Prepare a clean, opt-in ScarletNX renderer source tree from the vendored revision."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def prepare(destination, experiments):
    source = HERE.parents[2] / "third_party" / "ScarletNX"
    destination = destination.resolve()
    if destination.exists() or destination.is_relative_to(source):
        raise ValueError("Choose a new output directory outside the vendored ScarletNX tree")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as scratch:
        tree = Path(scratch) / "tree"
        shutil.copytree(source, tree, ignore=shutil.ignore_patterns(".git", ".cache", "__pycache__"))
        git_dir = Path(scratch) / "git"
        env = {**os.environ, "GIT_DIR": str(git_dir), "GIT_WORK_TREE": str(tree)}
        subprocess.run(["git", "init", "-q"], cwd=tree, env=env, check=True)

        def apply(name):
            subprocess.run(["git", "apply", "--whitespace=nowarn", str(HERE / name)],
                           cwd=tree, env=env, check=True)

        apply("runtime.patch")
        shutil.copytree(HERE / "overlay", tree, dirs_exist_ok=True)
        shutil.copytree(HERE / "src", tree / "world-pt")
        for name in ("compile_shaders.py", "integration.cmake"):
            shutil.copy2(HERE / name, tree / "world-pt")
        if experiments:
            shutil.copy2(tree / "world-pt/denoise.comp", tree / "world-pt/denoise-released.comp")
            apply("experiments.patch")
        tree.rename(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--experiments", action="store_true")
    opts = parser.parse_args()
    prepare(opts.destination, opts.experiments)
