"""Build the standalone Vulkan tests against the same source as the launchers."""
from pathlib import Path
import subprocess
import sys

here=Path(__file__).resolve().parent
root=here.parents[1];ups=here.parent/'ups';shared=Path('C:/Users/brett/Documents/Codex/work')
cc=shared/'llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
headers=next((shared/'nvn-src/.cache/cpm/vulkan-headers').glob('*/include'))
amd=root.parent/'files-pasted-by-the-user-project/work/za/third_party/ffx_api'
vulkan='C:/Windows/System32/vulkan-1.dll'
def compile(*args):subprocess.run([cc,'-O2','-I'+str(headers),*args],check=True)
subprocess.run([sys.executable,here/'build_shaders.py'],check=True)
compile(ups/'ngx.c','-c','-o',here/'ngx.o')
compile('-fno-exceptions','-fno-rtti','-I'+str(amd),ups/'fsr.cpp','-c','-o',here/'fsr.o')
compile(here/'replay_gpu.c',vulkan,'-o',here/'replay_gpu.exe')
target=Path(sys.argv[1]) if len(sys.argv)>1 else root/'outputs/upscale-runtime/TemporalReplay.exe'
compile(here/'temporal_replay.c',here/'ngx.o',here/'fsr.o',vulkan,'-ladvapi32','-o',target)
