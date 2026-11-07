import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
fields={0x80,0x81,0x82,0x37c}
for f,z in fns:
 for p in range(f,f+z,4):
  w=struct.unpack_from('<I',b,p)[0]
  op=w&0xffc00000
  if op in [0x39000000,0x39400000,0xb9000000,0xb9400000]:
   off=((w>>10)&4095)*(4 if op in [0xb9000000,0xb9400000] else 1)
   if off in fields and (off==0x37c or (op==0x39000000 and off==0x82)):
    print(hex(p),hex(f),'store' if op in [0x39000000,0xb9000000] else 'load',hex(off),'base',(w>>5)&31,'reg',w&31,'STR',str(vals(f))[:200])
