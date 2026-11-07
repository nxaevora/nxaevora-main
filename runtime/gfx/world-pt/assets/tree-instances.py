from models import Asset,entries,game,here,strings,np
from pak_paths import directory_index,name_hash
import re,collections,runpy
def village():
    pack=game/'archive/field/terrain/ha_area00.gfpak'
    index=directory_index(pack)
    def member(name):
        ids=index[name_hash(name)]
        if len(ids)!=1:raise ValueError(name)
        return ids[0]
    names=[n for n in strings((here/'member5.bin').read_bytes()) if n.endswith('.trins')]
    assets=collections.defaultdict(list)
    for n in names:
        model=re.fullmatch(r'area00_0_0_(.+)_X\d+Z\d+_d0.trins',n).group(1)
        assets[model].append(member(n))
    needed={i for ids in assets.values() for i in ids}
    components={model:[member(model+ext) for ext in ('.trmsh','.trmbf','.trmtr')] for model in assets}
    needed.update(i for ids in components.values() for i in ids)
    files={i:Asset(b) for i,_,b in entries(pack) if i in needed}
    for model,ids in assets.items():
        mats=[]
        for i in ids:
            f=files[i]; tab=f.ref(f.field(f.root(),4));off,size=f.vec(tab,0)
            assert size%64==0
            mats.extend(np.frombuffer(f.b,'<f4',size//4,off).reshape(-1,4,4))
        yield model,np.stack(mats),[files[i] for i in components[model]]
def primitives(m,b):
    shapes=m.tables(m.root(),1);chunks=b.tables(b.root(),1)
    assert len(shapes)==len(chunks)
    for shape,chunk in zip(shapes,chunks):
        layout=m.tables(shape,3)[0]
        streams=m.tables(layout,1)
        fields=[(m.number(a,1),m.number(a,2),m.number(a,3),m.number(a,4)) for a in m.tables(layout,0)]
        attr=next(f for f in fields if f[0]==1)
        stride=m.number(streams[0],0)
        raw,size=b.vec(b.tables(chunk,1)[0],0)
        xyz=np.ndarray((size//stride,3),'<f4',b.b,raw+attr[3],strides=(stride,4))
        raw,size=b.vec(b.tables(chunk,0)[0],0)
        dtype=np.dtype(['u1','<u2','<u4'][m.number(shape,2)])
        indices=np.frombuffer(b.b,dtype,size//dtype.itemsize,raw)
        uv_field=next(f for f in fields if f[:3]==(6,0,48))
        assert uv_field[3]+8<=stride
        vraw,_=b.vec(b.tables(chunk,1)[0],0)
        uv=np.ndarray((len(xyz),2),'<f4',b.b,vraw+uv_field[3],strides=(stride,4))
        for t in m.tables(shape,4):
            count,first=m.number(t,0),m.number(t,1)
            yield m.string(shape,0),m.string(t,3),xyz[indices[first:first+count]],uv[indices[first:first+count]]
if __name__=='__main__':
    read=runpy.run_path(str(here/'static-scenes.py'))['read']
    captures=collections.defaultdict(list)
    for f in sorted((here/'static').glob('*.bin')):
        for name,cut,p,uv in read(f):
            captures[len(p)].append((f.stem,name,p))
    for model,mats,(mesh,buf,materials) in village():
        print(model,len(mats),mats[:,3,:3].min(0),mats[:,3,:3].max(0))
        for shape,mat,p,fields in primitives(mesh,buf):
            if not len(p):continue
            ix=np.linspace(0,len(p)-1,15,dtype=int)
            points=p[ix]@mats[:,:3,:3]+mats[:,None,3,:3]
            best=(1e20,'','',0)
            for file,name,live in captures[len(p)]:
                d=np.linalg.norm(points-live[ix],axis=2).max(axis=1)
                i=int(d.argmin())
                if d[i]<best[0]:best=(d[i],file,name,i)
            print(shape,mat,len(p),p.min(0).round(2),p.max(0).round(2),best)
