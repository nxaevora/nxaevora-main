from pathlib import Path
import csv,itertools,numpy as np
cap=Path('../../work/arceus-games/ArceusTest/captures')
for meta in sorted(cap.glob('*_light_0_cb4.bin'))[::2]:
 base=meta.name.split('_light')[0]
 rows=list(csv.DictReader((s for s in (cap/(base+'_attachments.tsv')).read_text().splitlines() if not s.startswith('#')),delimiter='\t'))
 def img(addr,dt):
  r=next(r for r in rows if r['gva']==addr);w,h=int(r['image_w']),int(r['image_h'])
  return np.fromfile(r['file'],dt).reshape(h,w,-1)
 try:n=img('552210000','u1')[::4,::4].astype(np.int32);depth=img('54fc10000','<u4')[::4,::4,0]
 except (StopIteration,FileNotFoundError):continue
 m=np.fromfile(meta,'<f4')[48:64].reshape(4,4)
 y,x=np.indices(n.shape[:2]);h,w=n.shape[:2]
 clip=np.stack([2*(x+.125)/w-1,1-2*(y+.125)/h,(depth&0xffffff)/16777215.,np.ones_like(x)],axis=-1)
 p=clip@m;p=p[:,:,:3]/p[:,:,3:]
 def unit(a):return a/np.maximum(1.e-12,np.linalg.norm(a,axis=-1,keepdims=True))
 dx=p[1:-1,2:]-p[1:-1,:-2];dy=p[2:,1:-1]-p[:-2,1:-1];geom=unit(np.cross(dx,dy))
 enc=np.stack([n[:,:,0]*16+(n[:,:,1]>>4),(n[:,:,1]&15)*256+n[:,:,2]],axis=-1)/2047.-1
 xx,yy=enc[:,:,0],enc[:,:,1];zz=1-abs(xx)-abs(yy)
 normal=unit(np.stack([np.where(zz<0,(1-abs(yy))*np.sign(xx),xx),np.where(zz<0,(1-abs(xx))*np.sign(yy),yy),zz],axis=-1))[1:-1,1:-1]
 mask=(clip[1:-1,1:-1,2]>.0001)&(np.linalg.norm(dx,axis=-1)<1)&(np.linalg.norm(dy,axis=-1)<1)&(np.linalg.norm(geom,axis=-1)>.9)
 dots=(geom*normal).sum(-1)[mask]
 print(base,len(dots),np.percentile(dots,[10,25,50,75,90]),'within 25deg',np.mean(abs(dots)>.9063))
 print('best',max((round(float(np.mean(abs(np.sum(geom[mask]*normal[mask][:,perm]*sign,axis=-1))>.9063)),3),perm,sign) for perm in itertools.permutations(range(3)) for sign in itertools.product([-1,1],repeat=3)))
