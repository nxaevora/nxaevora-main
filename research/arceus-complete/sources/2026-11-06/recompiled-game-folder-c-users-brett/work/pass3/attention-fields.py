import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for start,end,fields in [(0x19b0000,0x19d0000,{0x440,0x4c0,0x540,0x5c0,0x670}),(0x24c0000,0x24e0000,{0x280,0x2a4})]:
 for p in range(start,end,4):
  w=struct.unpack_from('<I',b,p)[0]
  if w&0xffc00000==0xb9000000 and ((w>>10)&0xfff)*4 in fields:
   print(f'{p:X} fn={owner(p):X} STR W{w&31},[X{(w>>5)&31},#{((w>>10)&0xfff)*4:X}]')
print('LOOK OWNER');describe(0x19c65b8)
print('FACE OWNER');describe(0x24d17dc)
print('FACE vtable locations', [hex(x) for x in D['rev'].get(0x24d17dc,[])])
