exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 f=int(arg,16);print('\nFN',hex(f))
 for slot in D['rev'].get(f,[]):
  print('slot',hex(slot))
  for q in range(slot-0x90,slot+0x90,8):
   t=D['rel'].get(q,0)
   if 0x3790000<=t<0x39d0000:
    e=b.find(b'\0',t,t+100)
    if e>t and all(32<=c<127 for c in b[t:e]):print(hex(q),b[t:e].decode())
