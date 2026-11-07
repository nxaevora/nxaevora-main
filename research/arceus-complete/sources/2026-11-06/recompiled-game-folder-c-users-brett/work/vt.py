exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in sys.argv[1:]:
 t=int(a,16);print('\nTABLE',a)
 for p in range(t,t+0x58,8):
  v=D['rel'].get(p,0);print(hex(p),hex(v),str(vals(v))[:160])
