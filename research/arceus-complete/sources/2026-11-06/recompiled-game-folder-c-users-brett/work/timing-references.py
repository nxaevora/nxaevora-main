from pathlib import Path
import struct
exec(Path('work/relocs.py').read_text().split('for off in range')[0])
for target in (0x194f110,0x194f20c,0x19b2140):
 print('target',hex(target))
 for off,v in rel.items():
  if v==target:
   print('vtable',hex(off))
   for p in range(off-0x30,off+0x90,8):print(hex(p),hex(rel.get(p,0)))
# Direct branch callers to known blink update and camera input accessor.
for target in (0x194f110,0x194f20c,0x16ee9f8,0x16eea54):
 print('callers',hex(target))
 for p in range(0,0x3300000,4):
  w=struct.unpack_from('<I',b,p)[0]
  if w&0x7c000000==0x14000000:
   imm=w&0x3ffffff
   if imm&0x2000000:imm-=0x4000000
   if p+imm*4==target:print(hex(p))
