"""Compare reprojected village terrain albedo with native splat textures."""
from pathlib import Path
import runpy,struct,sys
import numpy as np
from PIL import Image
from models import Asset,game,entries,here
from texture import texture

def native_mip(blob,level):
    name,fmt,w,h,mips=texture(blob)
    bc={26:1,27:2,28:3,29:4,30:5,32:7}[fmt>>8]
    block=8 if bc in (1,4) else 16
    brti=struct.unpack_from('<Q',blob,struct.unpack_from('<Q',blob,0x28)[0])[0]
    bh=struct.unpack_from('<I',blob,brti+0x34)[0]&7
    w=max(1,w>>level);h=max(1,h>>level)
    ny=(h+3)//4;nx=(w+3)//4*block
    while bh and (8<<(bh-1))>=ny:bh-=1
    y,x=np.indices((ny,nx))
    off=((y>>(3+bh))*((nx+63)//64)+(x>>6))*(512<<bh)+(((y>>3)&((1<<bh)-1))*512)+((x&32)<<3)+((y&6)<<5)+((x&16)<<1)+((y&1)<<4)+(x&15)
    blocks=np.frombuffer(blob,'u1')[mips[level]+off].tobytes()
    return np.asarray(Image.frombytes('L' if bc==4 else 'RGB' if bc==5 else 'RGBA',(w,h),blocks,'bcn',(bc,'BC'+str(bc)))).copy()

def textures(pack):
    images={};table={}
    for i,_,blob in entries(pack):
        if blob[:4]==b'BNTX':
            name=texture(blob)[0]
            if name.endswith('_alb'):images[name]=blob
        elif i==3:terrain=Asset(blob)
        elif i==4:
            f=Asset(blob);t=f.tables(f.root(),1)[0]
            names={f.string(x,0):f.string(x,1) for x in f.tables(t,2)}
            uv={f.string(x,0):struct.unpack_from('<4f',blob,f.field(x,1)) for x in f.tables(t,7)}
            for j in range(16):
                if 'SplatColor'+str(j) in names:table[j]=(Path(names['SplatColor'+str(j)]).stem,uv['SplatST'+str(j)])
    return images,terrain,table

def ground_samples(dump,landFile,pack):
    with Path(dump).open('rb') as f:
        mat=np.fromfile(f,'<f4',48).reshape(3,4,4)
        diff=np.fromfile(f,'<f2',1920*1080*4).reshape(1080,1920,4)
        f.seek(48*4+1920*1080*(8*3+4))
        z=np.fromfile(f,'<f4',1920*1080).reshape(1080,1920)
    ix,iy=np.meshgrid(np.arange(1,1920,3),np.arange(1,1080,3))
    clip=np.stack((2*(ix+.5)/1920-1,1-2*(iy+.5)/1080,z[iy,ix],np.ones_like(ix)),axis=-1)
    world=clip@mat[0];world=world[:,:,:3]/world[:,:,3,None]
    p=world.reshape(-1,3);colors=diff[iy,ix,:3].astype('f4').reshape(-1,3)/.96
    images,f,table=textures(pack)
    height=runpy.run_path(str(here/'export-adaptive.py'))['source_height'](pack);side=len(height)
    ox,oz,sx,sz=struct.unpack_from('<4f',landFile.read_bytes(),24)
    coord=(p[:,[0,2]]-[ox,oz])/[512*sx,512*sz]
    coord=np.clip(coord,0,1)
    q=coord*(side-1);i=np.clip(q.astype(int),0,side-2);a,b=i.T;x,y=(q-i).T
    h0=height[b,a];h1=height[b+1,a+1]
    heightRef=np.where(x+y<=1,h0+x*(height[b,a+1]-h0)+y*(height[b+1,a]-h0),h1+(1-x)*(height[b+1,a]-h1)+(1-y)*(height[b,a+1]-h1))
    good=(np.abs(p[:,1]-heightRef)<.05)&np.all(colors>0,axis=1)
    weights=[]
    for layer in f.tables(f.root(),8):
        off,n=f.vec(layer,1);dim=round(n**.5)
        if dim<2:weights.append(np.zeros(len(p)));continue
        a,b=np.clip(np.rint(coord*dim).astype(int),0,dim-1).T
        weights.append(np.frombuffer(f.b,'u1',n,off).reshape(dim,dim)[b,a])
    weights=np.stack(weights,1)
    for slot,(name,uv) in table.items():
        keep=good & (weights[:,slot]>.94*np.maximum(weights.sum(1),1))
        if keep.sum()<50:continue
        yield name,uv,p[keep],colors[keep],images[name]

if __name__=='__main__':
    pack=game/'archive/field/terrain/ha_area00.gfpak'
    v2=here.parent/'v2'
    for name,uv,p,c,img in ground_samples(v2/'filter-validate-native-trunks-RR-0/rr-guides.bin',v2/'material-terrain/ha_area00.land',pack):
        print(name,uv,len(p),p.min(0).round(2),p.max(0).round(2),c.mean(0))
        image=native_mip(img,1)[:,:,:3].astype('f4')
        # Uniformly distributed pixels from separate screen regions are kept for a held-out fit.
        np.savez(v2/(name+'-samples.npz'),p=p,c=c,image=image,uv=uv)
