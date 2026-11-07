exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for slot in D['rev'].get(0x194f110,[]):
 print('blink fn slot',hex(slot))
 bases=[t for t in D['refs'] if slot-0x400<=t<=slot]
 for t in bases:
  if D['rel'].get(t+0x350)==0x194f110:
   print('base',hex(t))
   for i in range(0x340,0x380,8):print(hex(i),hex(D['rel'].get(t+i,0)))
   for p in D['refs'][t]:print('construct',hex(p),hex(owner(p)))
for f in (0x195afec,):describe(f)
