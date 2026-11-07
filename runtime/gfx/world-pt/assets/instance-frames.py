"""Match archived instanced triangles to native vertex normals, tangents and BC5 inputs."""
import sys,runpy,struct,shutil
from pathlib import Path
import numpy as np
from PIL import Image
from texture import texture,here
srcdata=runpy.run_path(str(here/'export-shading.py'))
def export(src,dst):
    b=src.read_bytes()
    magic,ver,archive,ns,ni,nt=struct.unpack_from('<4s5I',b)
    if magic!=b'GRVE' or ver not in (1,2):raise ValueError(src)
    shapes=np.frombuffer(b,'<u4',ns*8,24).copy().reshape(-1,8)
    start=24+32*ns+64*ni
    verts=np.frombuffer(b,'<f4',12*nt,start).reshape(-1,4)
    end=start+48*nt+ns*16384*(1 if ver==1 else 2);assert end==len(b)
    frames=np.zeros((3*nt,2),'<u4')
    maps=bytearray(ns*32772);matched=0;mapped=0
    for folder in ('terrain','model/pack'):
        groups,images=srcdata['archive'](src.stem,folder)
        for j,shape in enumerate(shapes):
            first,count=map(int,shape[:2]);v=verts[3*first:3*(first+count)]
            if shape[3]&0x40000000:continue
            for ids,pos,N,T,uv,key,_ in groups.get(3*count,[]):
                if not np.array_equal(v[:,:3],pos[ids]):continue
                st=uv[ids].copy();st[:,1]=1-st[:,1]
                if not np.array_equal(st.astype('<f2'),v[:,3].copy().view('<f2').reshape(-1,2)):continue
                n=srcdata['unit'](N[ids].astype(float));t=T[ids].astype(float);tangent=srcdata['unit'](t[:,:3])
                if not np.isfinite(n).all() or not np.isfinite(t).all():continue
                f=frames[first*3:3*(first+count)]
                f[:,0]=srcdata['pack'](n,np.linalg.norm(n,axis=1)>.9)
                f[:,1]=srcdata['pack'](tangent,np.where(np.linalg.norm(tangent,axis=1)<.9,0,np.where(t[:,3]>0,1,2)))
                shape[3]|=0x40000000;matched+=1
                name,strength=key
                if name in images and texture(images[name])[1]>>8==30 and 0<strength<=4:
                    img=images[name];_,_,w,h,mips=texture(img);lv=0
                    while max(w,h)>>(lv+1)>=128 and lv+1<len(mips):lv+=1
                    xy=np.asarray(Image.fromarray(srcdata['decode'](img,lv)).resize((128,128),Image.Resampling.BILINEAR))[:,:,:2]
                    maps[j*32772:(j+1)*32772]=struct.pack('<f',strength)+xy.tobytes();mapped+=1
                break
        if matched==ns:break
    if not matched:shutil.copy2(src,dst);return
    with dst.open('wb') as f:
        f.write(struct.pack('<4s5I',magic,3,archive,ns,ni,nt))
        f.write(shapes.tobytes());f.write(b[24+32*ns:])
        if ver==1:f.write(bytes(ns*16384))
        f.write(frames.tobytes());f.write(maps)
    print(src.stem,matched,'of',ns,'meshes with exact vertices and UVs;',mapped,'native normal maps',flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:]);dst.mkdir(exist_ok=True)
    for p in src.iterdir():
        if p.suffix=='.grove':export(p,dst/p.name)
        elif p.is_file():shutil.copy2(p,dst/p.name)
