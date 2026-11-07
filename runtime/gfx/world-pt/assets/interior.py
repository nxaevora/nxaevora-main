from models import *
from nav import nav
def mesh_data(area,meshId,bufId):
 m,b=pair(area,meshId,bufId)
 for shape,chunk in zip(m.tables(m.root(),1),b.tables(b.root(),1)):
  layout=m.tables(shape,3)[0];stride=m.number(m.tables(layout,1)[0],0)
  fields={m.number(a,1):(m.number(a,3),m.number(a,4)) for a in m.tables(layout,0)}
  raw,size=b.vec(b.tables(chunk,1)[0],0);n=size//stride
  xyz=np.ndarray((n,3),'<f4',b.b,raw+fields[1][1],strides=(stride,4))
  raw,size=b.vec(b.tables(chunk,0)[0],0);dtype=['u1','<u2','<u4'][m.number(shape,2)]
  idx=np.frombuffer(b.b,dtype,size//np.dtype(dtype).itemsize,raw)
  print(m.string(shape,0),n,len(idx),xyz.min(0),xyz.max(0),idx.min(),idx.max())
  yield xyz,idx
if __name__=='__main__':
 list(mesh_data('ha_area00_s01',11,9))
 d,items=nav('ha_area00_s01')
 print('nav arrays')
 for typ,off,n in items:
  if typ==0x20000013 and n>32:
   a=np.frombuffer(d,'<f4',int(n)*4,int(off)).reshape(-1,4)
   print(a.min(0),a.max(0))
