from fb import *
import numpy as np
for i in [3,5]:
 b=(here/f'member{i}.bin').read_bytes();f=FB(b);t=f.fields(f.u32(0))
 for ix in ([0,1,2] if i==3 else [0,1,2,10]):
  if t[ix] and f.ref(t[ix])<len(b):
   print(i,ix,'ref',f.ref(t[ix]))
   try: print([(off,struct.unpack_from('<f',b,off)[0] if off else None) for off in f.fields(f.ref(t[ix]))])
   except Exception as e:print(str(e))
 if i==3:
  n=f.ref(t[5]);a=np.frombuffer(b,dtype='<f4',count=f.u32(n),offset=n+4).reshape(1025,1025)
  print('range normalized height',np.nanpercentile(a,[0,1,10,50,90,99,100]))
  np.save(here/'village-height.npy',a)
