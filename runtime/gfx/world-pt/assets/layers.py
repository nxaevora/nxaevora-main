from models import *
a=Asset((here/'member3.bin').read_bytes())
for i,t in enumerate(a.tables(a.root(),8)):
 print('layer',i)
 a.dump(t)
 for j in range(6):
  try:
   p,n=a.vec(t,j)
   if n<200:print(j,a.b[p:p+n])
   elif n==1024**2:print('weight range',np.percentile(np.frombuffer(a.b,'u1',n,p)[::103],[0,50,95,100]))
  except Exception:pass
