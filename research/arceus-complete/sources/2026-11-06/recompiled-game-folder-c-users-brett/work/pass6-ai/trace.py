import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,n in fns:
 if 0xb32000<=f<0xb34500:
  print(hex(f),hex(n),'strings',vals(f),'rev',[hex(x) for x in D['rev'].get(f,[])])
for p in [0x4272ce0,0x4272cf0,0x4272d20,0x4272d28,0x4272d30,0x4272d38,0x4272d40,0x427a118]:
 target=D['rel'].get(p)
 print('POINTER',hex(p),'raw',hex(struct.unpack_from('<Q',b,p)[0]),'rel',hex(target) if target else None)
 if target:print('data',b[target:target+24].hex(),'floats',struct.unpack_from('<6f',b,target))
for p in [0x378a7e8,0x378a140,0x3788d10]:print('VECTOR',hex(p),struct.unpack_from('<4f',b,p))
for p in [0x4272ce0,0x4272cf0,0x4272d20,0x4272d28,0x4272d30,0x4272d38,0x4272d40,0x427a118]:
 print('REFS',hex(p),[(hex(q),hex(owner(q) or 0),vals(owner(q))) for q in D['refs'].get(p,[])][:20])
