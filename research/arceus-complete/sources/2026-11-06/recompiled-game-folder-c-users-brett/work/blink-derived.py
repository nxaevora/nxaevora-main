exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for t in [0x4138670,0x4143700,0x4146210,0x4147c18]:
 print('TABLE',hex(t))
 for i in range(0x340,0x380,8):
  f=D['rel'].get(t+i,0);print(hex(i),hex(f),size.get(f),str(vals(f)))
