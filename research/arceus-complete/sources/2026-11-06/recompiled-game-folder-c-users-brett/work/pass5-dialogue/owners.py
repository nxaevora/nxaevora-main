import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 f=owner(int(arg,16)); describe(f)
 slots=D['rev'].get(f,[])
 print('SLOTS', [hex(v) for v in slots])
 for slot in slots[:2]:
  for t,rs in D['refs'].items():
   if slot-0x100<=t<=slot:
    for r in rs[:4]:
     q=owner(r)
     print('VTREF',hex(t),hex(r),hex(q) if q else None,vals(q))
