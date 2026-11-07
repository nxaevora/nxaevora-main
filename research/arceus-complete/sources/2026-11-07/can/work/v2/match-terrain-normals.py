"""Compare archive detail normals with native G-buffer normals at reprojected terrain pixels."""
import struct,sys,runpy
from pathlib import Path
import numpy as np
sys.path.insert(0,'work/assets')
from texture import game,entries,texture
from models import Asset
decode=runpy.run_path('work/assets/terrain-uv.py')['native_mip']
def bilinear(img,u,v,wrap=True):
    h,w=img.shape[:2];sx=u*w-.5;sy=v*h-.5;x=np.floor(sx).astype(int);y=np.floor(sy).astype(int)
    z=np.zeros((len(x),img.shape[2]))
    for dx in range(2):
        for dy in range(2):
            a,b=(x+dx)%w,(y+dy)%h
            if not wrap:a=np.clip(x+dx,0,w-1);b=np.clip(y+dy,0,h-1)
            z+=img[b,a]*(abs((1-dx)-(sx-x))*abs((1-dy)-(sy-y)))[:,None]
    return z
def norm(a):
    return a/np.maximum(1.e-8,np.linalg.norm(a,axis=1))[:,None]
def angular(a,b):
    return np.rad2deg(np.arccos(np.clip((a*b).sum(1),-1,1)))
v2=Path('work/v2');stage=sys.argv[1] if len(sys.argv)>1 else 'field-material'
g,n,s=np.memmap(v2/stage/'gbuffer.bin',dtype='u1',mode='r',shape=(3,1080,1920,4))
with (v2/('filter-validate-'+stage+'-RR-0')/'rr-guides.bin').open('rb') as f:
    mat=np.fromfile(f,'<f4',48).reshape(3,4,4);f.seek(192+1920*1080*28)
    depth=np.fromfile(f,'<f4',1920*1080).reshape(1080,1920)
y,x=np.where((g[1:-1,1:-1,3]==66)&(depth[1:-1,1:-1]>0))
x=x[::5]+1;y=y[::5]+1
def unproject(x,y):
    clip=np.stack((2*(x+.5)/1920-1,1-2*(y+.5)/1080,depth[y,x],np.ones(len(x))),1)
    q=clip@mat[0];return q[:,:3]/q[:,3:]
p=unproject(x,y)
a=unproject(x+1,y)-p;b=unproject(x,y+1)-p
geom=norm(np.cross(a,b));geom*=np.where(geom[:,1]>0,1,-1)[:,None]
r,h,b=n[y,x,:3].astype(int).T
nx=np.clip((r*16+(h>>4))/2047.-1,-1,1);ny=np.clip(((h&15)*256+b)/2047.-1,-1,1)
nz=1-abs(nx)-abs(ny);actual=norm(np.stack([np.where(nz<0,(1-abs(ny))*np.sign(nx),nx),np.where(nz<0,(1-abs(nx))*np.sign(ny),ny),nz],1))
weights=bilinear(np.load(v2/'native-weights.npy',mmap_mode='r'),(p[:,0]*1023/1025+.5)/1024,(p[:,2]*1023/1025+.5)/1024,False)
weights/=np.maximum(.001,weights.sum(1))[:,None]
print('mean geom/actual',geom.mean(0),actual.mean(0),'angles',np.percentile(angular(geom,actual),[50,90,95]),flush=True)
pack=game/'archive/field/terrain/ha_area00.gfpak';images={}
for i,_,blob in entries(pack):
    if blob[:4]==b'BNTX':
        name=texture(blob)[0]
        if name.endswith('_nrm'):images[name]=blob
    elif i==4:
        asset=Asset(blob);t=asset.tables(asset.root(),1)[0]
        names={asset.string(b,0):asset.string(b,1) for b in asset.tables(t,2)}
        vec={asset.string(b,0):struct.unpack_from('<4f',blob,asset.field(b,1)) for b in asset.tables(t,7) if asset.field(b,1)}
for j in range(7):
    good=(weights[:,j]>.97)&(geom[:,1]>.995)&(actual[:,1]>.5)
    if good.sum()<50:continue
    q=p[good];obs=actual[good];slope=geom[good];st=vec['SplatST'+str(j)]
    img=images[Path(names['SplatNormal'+str(j)]).stem]
    print('layer',j,len(q),'slope only',np.percentile(angular(slope,obs),[50,95]))
    for mip in [0,1,2,3]:
        raw=bilinear(decode(img,mip)[:,:,:2],(q[:,0]+st[2])/st[0],(q[:,2]+st[3])/st[1])/127.5-1
        coeff=np.linalg.lstsq(raw,obs[:,[0,2]]-slope[:,[0,2]],rcond=None)[0]
        pred=slope.copy();pred[:,[0,2]]+=raw@coeff
        print(mip,coeff.round(3).tolist(),np.percentile(angular(norm(pred),obs),[50,95]).round(3))
