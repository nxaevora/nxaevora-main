import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
print('WORLD CTOR CALLERS');describe(0x2f28830)
for p in D['calls'].get(0x2f28830,[]):print(hex(p),hex(owner(p)))
print('SCHEDULER VTABLE REFS')
for t,refs in D['refs'].items():
 if 0x4187e00<=t<=0x4187ff0:
  for p in refs:print(hex(t),hex(p),hex(owner(p)),vals(owner(p))[:8])
print('MOTION FINAL CALLERS');describe(0x2f269e0)
