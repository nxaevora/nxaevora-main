import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for start,end in [(0x419A800,0x419AA20),(0x41B3C90,0x41B3D10)]:
 for p in range(start,end,8):
  v=D['rel'].get(p)
  if v is not None:
   msg=''
   if 0<=v<len(b):
    z=b[v:v+160].split(b'\x00')[0]
    if z and all(32<=c<127 for c in z):msg=z.decode()
   print(hex(p),hex(v),msg)
for t in [0x419A958,0x41B3CA0,0x41B3CE0]:
 print('refs',hex(t),[(hex(p),hex(owner(p))) for p in D['refs'].get(t,[])])
