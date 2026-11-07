import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for lo,hi in [(0x2736800,0x2739000),(0x1319000,0x131e920),(0x1061000,0x107e200),(0xc6e000,0xc71600)]:
 print('RANGE',hex(lo),hex(hi))
 for f,z in fns:
  if lo<=f<hi and (vals(f) or 0x2736800<=f<0x2737164): print(hex(f),hex(z),vals(f))
