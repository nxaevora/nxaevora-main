from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for p in range(0x1fa3ce0,0x1fa4a20,4):
 a,v=struct.unpack_from('<II',b,p)
 if a&0x9f000000==0x90000000 and v&0xffc00000==0x91000000 and (v>>5)&31==a&31:
  imm=((a>>29)&3)|(((a>>5)&0x7ffff)<<2)
  if imm&0x100000:imm-=0x200000
  t=(p&~4095)+(imm<<12)+((v>>10)&4095)
  if t<0x32a0000: print(hex(p),'code',hex(t))
  elif 0x3790000<t<0x3980000: print(hex(p),b[t:b.index(0,t)].decode(errors='replace')[:100])
