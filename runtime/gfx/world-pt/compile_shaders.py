"""Compile the renderer's authored GLSL programs into C word arrays."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parent
CONTACT = {
    "reference": "QA_REFERENCE", "skyDistribution": "SKY_DISTRIBUTION",
    "verifyObjects": "VERIFY_OBJECTS", "verifyTrees": "VERIFY_TREES",
    "mesh": "BUILD_MESH", "calibrate": "CALIBRATE", "trianglePower": "EMITTER_TRIANGLES",
    "emitterCdf": "EMITTER_DISTRIBUTION", "particleGrid": "PARTICLE_GRID",
    "contact": None, "rrContact": "RR_GUIDES",
}


def build(compiler, output, features):
    output.mkdir(exist_ok=True, parents=True)
    digest = hashlib.sha256()
    for path in sorted([*(SOURCE.glob("*.inc")), *(SOURCE.glob("*.h")),
                        SOURCE.parent / "src/nx/gpu/vk/pipeline.c",
                        SOURCE.parent / "src/nx/gpu/shader/spirv_translate.c"]):
        digest.update(path.name.encode("ascii") + b"\0" + path.read_bytes())
    digest.update(",".join(sorted(features)).encode("ascii"))
    headers = {"world-pt-cache.h": ['#define WORLD_PT_CACHE_ID "' + digest.hexdigest()[:16] + '"']}
    with tempfile.TemporaryDirectory(dir=output) as tmp:
        def add(header, symbol, source, defines=(), ctype="u32"):
            spv = Path(tmp) / (symbol + ".spv")
            subprocess.run([compiler, "-V", "--target-env", "vulkan1.2", "-I" + str(SOURCE / "motion"),
                            *("-D" + flag for flag in defines), "-o", spv, source], check=True)
            words = [hex(word) for word, in struct.iter_unpack("<I", spv.read_bytes())]
            headers.setdefault(header, []).append(f"static const {ctype} {symbol}[]={{" + ",".join(words) + "};")
        variants = dict(CONTACT)
        for flag, symbol in (("GROVE_TRACE_TEST","fieldWitness"), ("FIELD_WITNESS","fieldWitness"),
                             ("EMISSION_WITNESS","emissionWitness")):
            if flag in features and symbol not in variants:
                variants[symbol] = flag
        history = ["GEOMETRY_HISTORY"] if "GEOMETRY_HISTORY" in features else []
        for name, flag in variants.items():
            add("contact_words.h", name + "_words", SOURCE / "contact.comp", history + ([flag] if flag else []))
        filt = history + (["HISTORY_WITNESS"] if "HISTORY_WITNESS" in features else [])
        add("contact_words.h", "denoise_words", SOURCE / "denoise.comp", filt)
        if "COMPARE_FILTER" in features:
            previous = SOURCE / "denoise-baseline.comp"
            if history:
                previous = Path(tmp) / "prior.comp"
                previous.write_text((SOURCE / "denoise-released.comp").read_text().replace(
                    "ref.v[2u*480u*270u+i]", "ref.v[3u*480u*270u+i]"))
            add("contact_words.h", "baseline_words", previous)
        add("contact_words.h", "overlay_mask_words", SOURCE / "overlay-mask.comp")
        add("material-tile-words.h", "material_tile_words", SOURCE / "material-tile.comp")
        add("prepare_sky.h", "up_words", SOURCE / "prepare_sky.comp", ctype="unsigned int")
        add("seed_sky.h", "sky_seed_words", SOURCE / "seed_sky.comp", ctype="unsigned int")
        for name in ("measure","candidates","score","choose","validate","apply","seed"):
            add("motion/flow_words.h", name + "_spv", SOURCE / "motion" / (name + ".comp"), ctype="uint32_t")
        for name, rows in headers.items():
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n".join(rows) + "\n", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path)
    parser.add_argument("features", nargs="*")
    opts = parser.parse_args()
    build(opts.compiler, opts.output, set(opts.features))
