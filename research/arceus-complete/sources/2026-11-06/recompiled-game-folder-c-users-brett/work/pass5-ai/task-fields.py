import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,z in fns:
 if not 0x2950000<=f<0x2972000:continue
 hits=[]
 for p in range(f,f+z,4):
  w=struct.unpack_from('<I',b,p)[0];op=w&0xffc00000
  if op in [0xb9000000,0xb9400000]:
   off=((w>>10)&4095)*4
   if off in [0x1e8,0x378,0x37c,0x398]:hits.append((hex(p),hex(off),'str' if op==0xb9000000 else 'ldr'))
  if (w&0xffc00000)==0x29400000:
   off=(w>>15)&127;off=(off-128 if off>=64 else off)*4
   if off in [0x378,0x37c]:hits.append((hex(p),hex(off),'ldp'))
 if hits:print(hex(f),hits,'CALLERS',','.join(hex(x) for x in callers(f)[:15]))
