import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in [0x40D4068,0x4199B20,0x41B3D18]:
 print('TABLE',hex(a))
 for p in range(a,a+0x180,8):
  t=D['rel'].get(p)
  if t is not None:
   z=b[t:t+160].split(b'\x00')[0]
   msg=z.decode() if z and all(32<=c<127 for c in z) else ''
   print(hex(p),hex(t),msg)
for a in [0x4199b20,0x4199b30,0x4199b40,0x41B3C98,0x41B3CD8]:print('ref',hex(a),[(hex(x),hex(owner(x))) for x in D['refs'].get(a,[])])
