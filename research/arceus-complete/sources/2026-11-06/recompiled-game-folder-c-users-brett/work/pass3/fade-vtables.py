import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for base in [0x40ea428,0x40ea4f0,0x40ea8b0]:
 print('VTABLE',hex(base))
 for p in range(base,base+0xc8,8):
  t=D['rel'].get(p)
  if t is not None:print(hex(p-base),hex(t),'fnsize',hex(size[t]) if t in size else None,vals(t)[:2])
for t in [0xc21490,0x180ba2c,0x186b5cc]:
 print('refs',hex(t),[hex(p) for p in D['rev'].get(t,[])])
