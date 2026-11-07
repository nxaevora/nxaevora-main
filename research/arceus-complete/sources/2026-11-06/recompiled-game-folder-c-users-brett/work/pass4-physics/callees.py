import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f in [0x2fe2604,0x2f9158c]:
 print('CALLEES',hex(f))
 for target,refs in D['calls'].items():
  sites=[p for p in refs if owner(p)==f]
  if sites:print(hex(target),','.join(hex(p) for p in sites),str(vals(target))[:300])
