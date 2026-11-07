from fb import *
from itertools import islice
import numpy as np
class Asset(FB):
 def root(self):return self.u32(0)
 def field(self,t,j):
  f=self.fields(t);return f[j] if j<len(f) else None
 def number(self,t,j,default=0):
  v=self.field(t,j);return self.u32(v) if v else default
 def vec(self,t,j):
  v=self.field(t,j)
  if not v:return (0,0)
  d=self.ref(v);return d+4,self.u32(d)
 def tables(self,t,j):
  d,n=self.vec(t,j);return [self.ref(d+4*i) for i in range(n)]
 def string(self,t,j):
  d,n=self.vec(t,j);return self.b[d:d+n].decode('utf8')
def pair(area,mesh,buffer):
 pack=game/'archive/field/model/pack'/(area+'.gfpak')
 loaded={i:Asset(data) for i,k,data in islice(entries(pack),max(buffer,mesh)+1) if i in (buffer,mesh)}
 return loaded[mesh],loaded[buffer]
if __name__=='__main__':
 mesh,buf=pair('ha_area00_s01',11,9)
 shapes=mesh.tables(mesh.root(),1);chunks=buf.tables(buf.root(),1)
 print('shapes',len(shapes),'chunks',len(chunks))
 for i,(shape,chunk) in enumerate(zip(shapes,chunks)):
  name=mesh.string(shape,0);print('shape',i,name,'bounds',struct.unpack_from('<6f',mesh.b,mesh.field(shape,1)))
  for l in mesh.tables(shape,3):
   print('layout',[(mesh.number(t,1),mesh.number(t,2),mesh.number(t,3),mesh.number(t,4)) for t in mesh.tables(l,0)],'stride',[mesh.number(z,0) for z in mesh.tables(l,1)])
  print('materials',[(mesh.string(t,3),mesh.number(t,0),mesh.number(t,1)) for t in mesh.tables(shape,4)][:4])
  print('stream');buf.dump(chunk)
  for j in range(3):
   try:
    for rec in buf.tables(chunk,j)[:2]: print('nested',j,buf.fields(rec),buf.vec(rec,0))
   except (struct.error,ValueError):pass
  if i==1:break
