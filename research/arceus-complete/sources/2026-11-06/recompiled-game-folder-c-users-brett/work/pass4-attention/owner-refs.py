import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for fn in (0x19c2f80,0x19c65b8,0x19c6760,0x24d17dc,0x24d3900):
 print('FN',hex(fn),'VTREFS',[hex(x) for x in D['rev'].get(fn,[])])
 for v in D['rev'].get(fn,[]):
  print('  local relocation neighbourhood',[(hex(p-v),hex(t)) for p,t in D['rel'].items() if v-0x28<=p<=v+0x28])
  for target,refs in D['refs'].items():
   if v-0x180<target<=v:
    for p in refs:
     f=owner(p)
     print('  ADDRREF',hex(target),hex(p),hex(f) if f else None,vals(f) if f else '')
