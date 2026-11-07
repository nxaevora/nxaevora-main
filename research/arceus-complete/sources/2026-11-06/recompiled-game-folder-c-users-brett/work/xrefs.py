from pathlib import Path
import struct,sys
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
targets={int(x,16) for x in sys.argv[1:]}
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
w=memoryview(b[:n]).cast('I')
pages={t&~4095 for t in targets}
for i,a in enumerate(w):
 if a&0x9f000000!=0x90000000: continue
 imm=((a>>29)&3)|(((a>>5)&0x7ffff)<<2)
 if imm&(1<<20): imm-=1<<21
 page=(i*4&~4095)+(imm<<12)
 if page not in pages: continue
 reg=a&31
 for j in range(i+1,min(i+9,len(w))):
  v=w[j]
  if v&0xffc00000==0x91000000 and (v>>5)&31==reg:
   target=page+((v>>10)&4095)
   if target in targets: print(hex(i*4),hex(j*4),'->',hex(target))
