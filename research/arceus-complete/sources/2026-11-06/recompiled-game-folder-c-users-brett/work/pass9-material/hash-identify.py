import sys
sys.path.pop(0)
from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/archive/field/model/pack/ha_area00_s01.gfpak').read_bytes()
def h(s,mode):
 x=0xcbf29ce484222325
 for c in s.encode():x=((x^c)*0x100000001b3)&0xffffffffffffffff if mode else ((x*0x100000001b3)&0xffffffffffffffff)^c
 return x
for name in ['ground_area00_s01_water01'+e for e in ['.trmtr','.trmdl','.trmsh','.trmbf']]:
 for p in ['', 'bin/field/model/pack/', '/bin/field/model/pack/', 'romfs/bin/field/model/pack/']:
  for m in [0,1]:
   v=h(p+name,m);off=b[:0x880].find(struct.pack('<Q',v))
   if off>=0:print(name,p,m,hex(v),hex(off))
