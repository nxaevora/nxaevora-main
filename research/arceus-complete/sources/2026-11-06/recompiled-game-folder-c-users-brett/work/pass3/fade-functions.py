import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f in starts:
 if 0xc18000<=f<0xc29000:
  print(hex(f),hex(size[f]),vals(f)[:8])
print('C264CC address references',[(hex(p),hex(t)) for t,ps in D['refs'].items() for p in ps if 0xc24000<=t<0xc264cc][:35])
