import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for t in [0x41b3ac8,0x41b5000]:
 print('VTABLE',hex(t))
 for p in range(t,t+48,8):
  f=D['rel'].get(p,0);print(hex(p),hex(f),size.get(f),vals(f)[:8])
