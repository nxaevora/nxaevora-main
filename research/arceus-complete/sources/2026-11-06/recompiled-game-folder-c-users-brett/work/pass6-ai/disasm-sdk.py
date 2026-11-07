import sys
sys.path.pop(0)
from pathlib import Path
import subprocess,re
root=Path('work/pass6-ai');bindir=Path(r'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin')
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/sdk.bin').read_bytes()
f=0x5295f0;z=640
raw=root/'sdk-acos.bin';elf=raw.with_suffix('.elf');raw.write_bytes(b[f:f+z])
subprocess.run([str(bindir/'llvm-objcopy.exe'),'-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,alloc,load,readonly,code',str(raw),str(elf)],check=True)
asm=subprocess.check_output([str(bindir/'llvm-objdump.exe'),'-d','--no-show-raw-insn','--triple=aarch64',f'--adjust-vma={f}',str(elf)],text=True)
root.joinpath('sdk-acos.txt').write_text(asm)
print(asm)
