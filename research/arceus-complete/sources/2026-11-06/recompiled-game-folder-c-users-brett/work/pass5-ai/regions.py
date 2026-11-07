import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for lo,hi in [(0x1053308,0x105a100),(0x189a914,0x18a0000)]:
 for f,z in fns:
  if lo<=f<hi:
   print(hex(f),hex(z),'STR',str(vals(f))[:400],'CALLERS',','.join(hex(x) for x in callers(f)[:12]),'VT',','.join(hex(x) for x in D['rev'].get(f,[])[:5]))
