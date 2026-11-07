import struct,sys,runpy
from pathlib import Path
import numpy as np
sys.path.insert(0,'work/assets')
from texture import *
m=runpy.run_path('work/assets/terrain-uv.py')
v2=Path('work/v2')
def sample(img,u,v,wrap=False):
    h,w=img.shape[:2];sx=u*w-.5;sy=v*h-.5;x=np.floor(sx).astype(int);y=np.floor(sy).astype(int)
    z=np.zeros((len(x),img.shape[2]))
    for dx in range(2):
        for dy in range(2):
            a,b=(x+dx)%w,(y+dy)%h
            if not wrap:a=np.clip(x+dx,0,w-1);b=np.clip(y+dy,0,h-1)
            z+=img[b,a]*(abs((1-dx)-(sx-x))*abs((1-dy)-(sy-y)))[:,None]
    return z
g,n,s=np.memmap(v2/'field-material/gbuffer.bin',dtype='u1',mode='r',shape=(3,1080,1920,4))
a=np.memmap(v2/'field-material/primary.bin',dtype='<u4',mode='r',shape=(1080,1920,4))
rough=n[:,:,3]/255.
tag=round(struct.unpack('<f',struct.pack('<I',1048872069))[0]*255)
print('tag',tag,'counts',(g[:,:,3]==tag).sum(),'rough bounds',rough[g[:,:,3]==tag].min(),rough[g[:,:,3]==tag].max())
with (v2/'filter-validate-field-material-RR-0/rr-guides.bin').open('rb') as f:
    mat=np.fromfile(f,'<f4',48).reshape(3,4,4)
    f.seek(192+1920*1080*28);depth=np.fromfile(f,'<f4',1920*1080).reshape(1080,1920)
y,x=np.where((g[:,:,3]==tag)&(depth>0)&(a[:,:,3]!=0))
x,y=x[::7],y[::7]
clip=np.stack((2*(x+.5)/1920-1,1-2*(y+.5)/1080,depth[y,x],np.ones(len(x))),1)
q=clip@mat[0];p=q[:,:3]/q[:,3:]
u,v=p[:,0]/1025,p[:,2]/1025;splat=np.load(v2/'native-weights.npy',mmap_mode='r')
images,terrain,table=m['textures'](game/'archive/field/terrain/ha_area00.gfpak')
def lin(c):return np.where(c>.04045,((c+.055)/1.055)**2.4,c/12.92)
def srgb(c):return np.where(c>.0031308,1.055*np.maximum(c,0)**(1/2.4)-.055,12.92*c)
true=g[y,x,:3].astype(float)/255
for flip in (0,1):
    weights=sample(splat,(u*1023+.5)/1024,((1-v if flip else v)*1023+.5)/1024)
    weights/=np.maximum(.001,weights.sum(1))[:,None]
    print('weight flip',flip,'roughness error',np.mean(abs(weights@np.array([.9,1,1,1,.85,1,1,1])-rough[y,x])),'dominant',np.bincount(weights.argmax(1)))
    for uvflip in (0,1):
        pred=np.zeros_like(true)
        for j,(name,st) in table.items():
            image=m['native_mip'](images[name],1)[:,:,:3]/255.
            rgb=sample(lin(image),p[:,0]/st[0],((1025-p[:,2]) if uvflip else p[:,2])/st[1],True)
            pred+=rgb*weights[:,j,None]
        pred=srgb(pred);sel=np.max(weights[:,1:],axis=1)>.97
        dd=np.abs(true-pred)
        print('all abs bytes',np.percentile(dd*255,[50,90,95]),'all correlation',np.corrcoef(true.mean(1),pred.mean(1))[0,1],'mean bytes',true.mean(0)*255,pred.mean(0)*255)
        print('UV flip',uvflip,'mean RGB',true[sel].mean(0).round(3),pred[sel].mean(0).round(3),'mae/p50/p95 raw',np.percentile(dd[sel]*255,[50,95]),'corr',np.corrcoef(true[sel].mean(1),pred[sel].mean(1))[0,1])
        for j in [1,5,6]:
            mask=weights[:,j]>.97
            if mask.sum()>100:print(' slot',j,mask.sum(),'obs',true[mask].mean(0).round(3),'pred',pred[mask].mean(0).round(3),'corr',round(np.corrcoef(true[mask].mean(1),pred[mask].mean(1))[0,1],3))
