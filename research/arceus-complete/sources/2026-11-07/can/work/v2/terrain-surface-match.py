import sys,runpy,struct
from pathlib import Path
import numpy as np
sys.path.insert(0,'work/assets');m=runpy.run_path('work/assets/terrain-uv.py')
from texture import *
from models import Asset
v2=Path('work/v2');pack=game/'archive/field/terrain/ha_area00.gfpak'
raw=v2/'filter-validate-native-trunks-RR-0/rr-guides.bin'
with raw.open('rb') as f:
 matrix=np.fromfile(f,'<f4',48).reshape(3,4,4);f.seek(192+1920*1080*16)
 normal=np.fromfile(f,'<f2',1920*1080*4).reshape(1080,1920,4).astype('f4')
 f.seek(192+1920*1080*28);depth=np.fromfile(f,'<f4',1920*1080).reshape(1080,1920)
a=np.fromfile(v2/'native-trunks/primary.bin','<u4').reshape(1080,1920,4)
# Only use pixels whose first ray hit the independently loaded terrain mesh.
y,x=np.where((a[:,:,0]>>31!=0)&(a[:,:,3]==1)&(a[:,:,0]!=0xffffffff))
z=depth[y,x];pts=np.stack([2*(x+.5)/1920-1,1-2*(y+.5)/1080,z,np.ones(len(y))],1)@matrix[0]
p=pts[:,:3]/pts[:,3:];rough=normal[y,x,3]
print(len(rough),p[:2],np.unique(rough,return_counts=True)[0][::8])
for i,_,b in entries(pack):
 if i!=37:continue
 brti=struct.unpack_from('<Q',b,struct.unpack_from('<Q',b,0x28)[0])[0];ptr=struct.unpack_from('<Q',b,brti+0x70)[0];data=bytearray(b);surfs=[]
 for j in (0,1):
  struct.pack_into('<Q',data,ptr,4096+j*1024**2);surfs.append(m['native_mip'](bytes(data),0))
 splat=np.concatenate(surfs,2).astype('f4')/255.
 np.save(v2/'native-weights.npy',splat)
 break
def bilinear(img,u,v,wrap=False):
 h,w=img.shape[:2];sx=u*w-.5;sy=v*h-.5;x=np.floor(sx).astype(int);y=np.floor(sy).astype(int)
 z=np.zeros((len(x),img.shape[2]))
 for dx in range(2):
  for dy in range(2):
   a,b=(x+dx)%w,(y+dy)%h
   if not wrap:a=np.clip(x+dx,0,w-1);b=np.clip(y+dy,0,h-1)
   z+=img[b,a]*(abs((1-dx)-(sx-x))*abs((1-dy)-(sy-y)))[:,None]
 return z
for flip in (0,1):
 u=p[:,0]/1025;v=p[:,2]/1025
 if flip:v=1-v
 w=bilinear(splat,u*1023/1024+.5/1024,v*1023/1024+.5/1024)
 pred=w@np.array([.9,1,1,1,.85,1,1,1])
 print('v flip',flip,'rough pred abs error',np.percentile(abs(pred-rough),[25,50,75,95]),'<=.008',np.mean(abs(pred-rough)<.008),'dominant',np.bincount(w.argmax(1),minlength=8))
