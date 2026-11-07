"""Extend verified prop geometry with the archive's vertex frames and BC5 material normals."""
from pathlib import Path
from collections import defaultdict
import shutil, struct, sys
import numpy as np
from PIL import Image
from models import Asset,entries,game,strings
from pak_paths import name_hash,directory_index
from texture import texture
import runpy
decode=runpy.run_path('work/assets/terrain-uv.py')['native_mip']

def unit(v):
    return v/np.maximum(1.e-12,np.linalg.norm(v,axis=1,keepdims=True))

def pack(v,mark):
    xyz=np.rint(np.clip(v,-1,1)*511).astype('<i4').view('<u4')&1023
    return xyz[:,0]|xyz[:,1]<<10|xyz[:,2]<<20|np.asarray(mark,dtype='<u4')<<30

def archive(area,folder='model/pack'):
    path=game/'archive/field'/folder/(area+'.gfpak')
    ix=directory_index(path);data={i:b for i,_,b in entries(path)}
    textures={texture(b)[0]:b for b in data.values() if b[:4]==b'BNTX'}
    names={Path(s).stem for b in data.values() if len(b)<65536 and b'.trmtr' in b and b'.trmsh' in b for s in strings(b) if s.endswith('.trmsh')}
    groups=defaultdict(list)
    for name in sorted(names):
        ids=[ix.get(name_hash(name+ext),[]) for ext in ('.trmsh','.trmbf','.trmtr')]
        if any(len(x)!=1 for x in ids):continue
        mesh,buf,mats=[Asset(data[x[0]]) for x in ids]
        byname={mats.string(t,0):t for t in mats.tables(mats.root(),1)}
        for shape,chunk in zip(mesh.tables(mesh.root(),1),buf.tables(buf.root(),1)):
            layout=mesh.tables(shape,3)[0];stride=mesh.number(mesh.tables(layout,1)[0],0)
            fields={(mesh.number(a,1),mesh.number(a,2),mesh.number(a,3)):mesh.number(a,4) for a in mesh.tables(layout,0)}
            required=[(1,0,51),(2,0,43),(3,0,43),(6,0,48)]
            if not all(k in fields for k in required):continue
            raw,length=buf.vec(buf.tables(chunk,1)[0],0)
            def read(k,n,dt):
                return np.ndarray((length//stride,n),dt,buf.b,raw+fields[k],strides=(stride,np.dtype(dt).itemsize))
            pos,norm,tan,uv=[read(k,n,dt) for k,n,dt in zip(required,(3,3,4,2),('<f4','<f2','<f2','<f4'))]
            start,length2=buf.vec(buf.tables(chunk,0)[0],0);dt=np.dtype(['u1','<u2','<u4'][mesh.number(shape,2)])
            indices=np.frombuffer(buf.b,dt,length2//dt.itemsize,start)
            for t in mesh.tables(shape,4):
                count,first=mesh.number(t,0),mesh.number(t,1)
                name=mesh.string(t,3);mat=byname[name]
                if [mats.string(o,0) for o in mats.tables(mat,1)]!=['Standard']:continue
                binds={mats.string(o,0):Path(mats.string(o,1)).stem for o in mats.tables(mat,2)}
                scalar={mats.string(o,0):struct.unpack_from('<f',mats.b,mats.field(o,1))[0] if mats.field(o,1) else 0. for o in mats.tables(mat,4)}
                key=(binds.get('NormalMap',''),scalar.get('NormalHeight',1.))
                groups[count].append((indices[first:first+count],pos,norm,tan,uv,key,name))
    return groups,textures

def export(path,dst):
    blob=path.read_bytes()
    magic,ver,n,m,g,archiveSize=struct.unpack_from('<4s5I',blob)
    assert magic==b'PROP' and ver==2
    mesh=np.frombuffer(blob,'<f4',12*n,24).reshape(-1,4)
    checkStart=24+52*n+32768*m
    checks=bytearray(blob[checkStart:]);assert len(checks)==64*g
    groups,images=archive(path.stem);frames=np.zeros((3*n,2),'<u4')
    banks={};atlas=[];covered=0;matched=0;start=0
    for group in range(g):
        count=3*struct.unpack_from('<I',checks,64*group)[0]
        world=mesh[start:start+count,:3];st=mesh[start:start+count,3].copy().view('<f2').reshape(-1,2)
        rows=np.linspace(0,count-1,min(count,51),dtype=int)
        for ids,pos,normals,tangents,uv,key,name in groups.get(count,[]):
            idx=ids[rows];native=uv[idx].copy();native[:,1]=1-native[:,1]
            if not np.array_equal(st[rows],native.astype('<f2')):continue
            A=np.column_stack((pos[idx],np.ones(len(idx))))
            if np.linalg.matrix_rank(A)<4:continue
            tr=np.linalg.lstsq(A,world[rows],rcond=None)[0];M=tr[:3]
            # Native instances can use nonuniform scaling. Require an invertible
            # transform and verify every position and half-UV below.
            scale=np.linalg.svd(M,compute_uv=False)
            if scale[-1]<.01 or scale[0]/scale[-1]>16 or np.max(abs(A@tr-world[rows]))>.001:continue
            native=uv[ids].copy();native[:,1]=1-native[:,1]
            if not np.array_equal(st,native.astype('<f2')) or np.max(abs(pos[ids]@M+tr[3]-world))>.005:continue
            N=unit(normals[ids].astype(float)@np.linalg.inv(M).T)
            T=tangents[ids].astype(float);sign=T[:,3]*np.sign(np.linalg.det(M))
            T=unit(T[:,:3]@M);valid=np.linalg.norm(N,axis=1)>.9
            frames[start:start+count,0]=pack(N,valid)
            frames[start:start+count,1]=pack(T,np.where(np.linalg.norm(T,axis=1)<.9,0,np.where(sign>0,1,2)))
            tex,height=key
            if tex in images and texture(images[tex])[1]>>8==30 and np.isfinite(height) and 0<height<=4:
                if key not in banks:
                    b=images[tex];_,_,w,h,mips=texture(b);level=0
                    while max(w,h)>>(level+1)>=128 and level+1<len(mips):level+=1
                    xy=np.asarray(Image.fromarray(decode(b,level)).resize((128,128),Image.Resampling.BILINEAR))[:,:,:2]
                    # The original BC5 samples stay unsigned. Store the separate native height with the map.
                    banks[key]=len(atlas)+1;atlas.append(struct.pack('<f',height)+xy.tobytes())
                struct.pack_into('<f',checks,64*group+60,float(banks[key]))
            matched+=1;covered+=count//3;break
        start+=count
    assert start==3*n and matched and atlas
    companion=path.with_suffix('.lighting')
    lighting=companion.read_bytes() if companion.is_file() else b''
    if lighting:
        count,=struct.unpack_from('<I',lighting)
        assert count and len(lighting)==4+16*g+128*128*8*count
    target=dst/path.name
    with target.open('wb') as f:
        f.write(struct.pack('<4s7I',magic,6 if lighting else 5,n,m,g,archiveSize,g,len(atlas)))
        if lighting:f.write(lighting[:4])
        f.write(blob[24:checkStart]);f.write(checks);f.write(frames.tobytes());f.writelines(atlas)
        if lighting:f.write(lighting[4:])
    print(target.name,covered,'of',n,'triangles,',matched,'of',g,'groups,',len(atlas),'normal maps',flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:3]);dst.mkdir(exist_ok=True)
    for p in src.iterdir():
        if p.suffix=='.props' and p.stem in ('ha_area00','ha_area00_s01'):export(p,dst)
        elif p.is_file() and p.suffix!='.lighting':shutil.copy2(p,dst/p.name)
