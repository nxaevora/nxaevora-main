from pathlib import Path
import struct, subprocess, sys
p=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin')
b=p.read_bytes(); base=0x8005000
if len(sys.argv)>1:
 start=int(sys.argv[1],16); size=int(sys.argv[2],16) if len(sys.argv)>2 else 0x200
 Path('work/code.bin').write_bytes(b[start:start+size])
 tools=Path(r'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin')
 subprocess.run([str(tools/'llvm-objcopy.exe'),'-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,alloc,load,readonly,code','work/code.bin','work/code.elf'],check=True)
 subprocess.run([str(tools/'llvm-objdump.exe'),'-d','--no-show-raw-insn','--triple=aarch64',f'--adjust-vma={base+start}','work/code.elf'],check=True)
else:
 n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
 ws=struct.unpack_from(f'<{n//4}I',b)
 hits=[]
 for i,w in enumerate(ws):
  if w & 0xffc00000 == 0x39c00000 and (w>>10)&0xfff==0x20:
   hits.append(hex(i*4))
 print('ldrsb +20',hits)
 print('constant',hex(struct.unpack_from('<I',b,0x397e8d8)[0]))
