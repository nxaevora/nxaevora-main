import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import struct
print('VTABLES')
for t in [0x4187f80,0x417ea60]:
 for p in range(t,t+0x88,8):
  print(hex(p),hex(D['rel'].get(p,0)))
print('SLOT70 indirect call candidates in component region')
for p in range(0x2950000,0x2980000,4):
 w=struct.unpack_from('<I',b,p)[0]
 if w&0xffc00000 == 0xf9400000 and ((w>>10)&4095)==0xe:
  reg=w&31;blr=0xd63f0000|(reg<<5)
  for d in range(4,32,4):
   if struct.unpack_from('<I',b,p+d)[0]==blr:
    f=owner(p);print(hex(p),hex(f),vals(f));break
print('CLOCK GLOBAL REFS')
for p in D['refs'].get(0x42eef30,[]):print(hex(p),hex(owner(p) or 0),vals(owner(p)))
