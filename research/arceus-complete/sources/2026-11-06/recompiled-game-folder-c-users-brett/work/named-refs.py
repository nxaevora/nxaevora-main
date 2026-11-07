exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 f=int(arg,16);print('\nTARGET',hex(f))
 for p in D['refs'].get(f,[]):
  o=owner(p)
  print('REF',hex(p),'owner',hex(o) if o else '-', 'nearby strings')
  for q,t,v in D['srefs'].get(o,[]):
   if abs(p-q)<180:print(hex(q),v)
