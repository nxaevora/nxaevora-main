import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 a=int(arg,16)
 for p in range(a,a+0xd8,8):
  f=D['rel'].get(p)
  print(f'{p:X} +{p-a:X}',hex(f) if f is not None else '-',str(vals(f))[:300])
