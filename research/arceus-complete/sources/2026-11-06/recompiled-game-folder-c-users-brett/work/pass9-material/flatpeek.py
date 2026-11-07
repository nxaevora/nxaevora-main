import sys
sys.path.pop(0)
from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/model/pack/objects_area00_const05.trmtr').read_bytes()
def u32(x):return struct.unpack_from('<I',b,x)[0]
def tbl(x):
 v=x-struct.unpack_from('<i',b,x)[0];n,z=struct.unpack_from('<HH',b,v);print('table',hex(x),'vt',hex(v),'size',z)
 for i in range((n-4)//2):
  o=struct.unpack_from('<H',b,v+4+2*i)[0]
  if o:print(i,'pos',hex(x+o),'u',hex(u32(x+o)),'float',struct.unpack_from('<f',b,x+o)[0],'target',hex(x+o+u32(x+o)),b[x+o+u32(x+o):x+o+u32(x+o)+32])
tbl(u32(0));tbl(64)
for key in [b'NormalHeight',b'Metallic',b'UVScaleOffset']:
 p=b.index(key);print(key,'at',hex(p),'surround',b[p-40:p+40].hex(' '))
