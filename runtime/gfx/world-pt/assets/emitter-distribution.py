"""Build a static area-times-radiance proposal over verified emissive structure triangles."""
from pathlib import Path
import shutil,struct,sys
import numpy as np
def sample(image,uv):
    pos=(uv%1)*128-.5
    base=np.floor(pos).astype('i4');f=pos-base
    out=np.zeros((len(uv),3))
    for y in range(2):
        for x in range(2):
            w=(f[:,0] if x else 1-f[:,0])*(f[:,1] if y else 1-f[:,1])
            out+=w[:,None]*image[(base[:,1]+y)%128,(base[:,0]+x)%128,:3]
    return out
def convert(src,dst):
    b=src.read_bytes()
    magic,ver,n,m,g,archive,first,normals,banks=struct.unpack_from('<4s8I',b)
    if magic!=b'PROP' or ver!=6:raise ValueError(src)
    vertex=np.frombuffer(b,'<f4',12*n,36).reshape(n,3,4)
    tag=np.frombuffer(b,'<u4',n,36+48*n)
    end=36+52*n+32768*m+64*g+24*n+32772*normals
    groups=np.frombuffer(b,'<u4',4*g,end).copy().reshape(g,4)
    atlas=np.frombuffer(b,'<f2',banks*128*128*4,end+16*g).reshape(banks,128,128,4)
    assert end+16*g+atlas.nbytes==len(b)
    ids=[];weights=[];count=0
    for group,(bank,scale,_,_) in enumerate(groups):
        if bank==0:continue
        k=np.flatnonzero(tag>>16==group);verts=vertex[k];xyz=verts[:,:,:3].astype(float)
        uv=verts[:,:,3].copy().view('<f2').reshape(-1,3,2).astype(float)
        assert np.array_equal(k,np.arange(k[0],k[0]+len(k)))
        area=.5*np.linalg.norm(np.cross(xyz[:,1]-xyz[:,0],xyz[:,2]-xyz[:,0]),axis=1)
        center=uv.mean(axis=1)
        rgb=sample(atlas[bank-1],center)
        for corner in range(3):rgb+=sample(atlas[bank-1],.35*center+.65*uv[:,corner])
        power=area*(rgb@np.array([.2126,.7152,.0722]))*.25*scale.view('<f4')
        if not np.isfinite(power).all() or (power<0).any():raise ValueError('Invalid emission estimate')
        groups[group,3]=1+count;count+=len(k);ids.append(k);weights.append(power)
    pdf=np.concatenate(weights);cdf=np.cumsum(pdf)
    if not cdf[-1]>0:raise ValueError('Empty light distribution')
    packed=np.column_stack([np.concatenate(ids).astype('<u4'),(cdf/cdf[-1]).astype('<f4').view('<u4')]).astype('<u4')
    with dst.open('wb') as f:
        f.write(struct.pack('<4s9I',magic,7,n,m,g,archive,first,normals,banks,count))
        f.write(b[36:end]);f.write(groups.tobytes());f.write(atlas.tobytes());f.write(packed.tobytes());f.write(bytes(4))
    print(src.stem,'emitter triangles',count,'positive sampled power',np.count_nonzero(pdf),'world area luminance',round(cdf[-1],3),flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:]);dst.mkdir(exist_ok=True)
    for p in src.iterdir():
        if p.suffix=='.props':convert(p,dst/p.name)
        elif p.is_file():shutil.copy2(p,dst/p.name)
