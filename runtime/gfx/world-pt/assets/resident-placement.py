"""Read static model placements from the game's resident field tables."""
import re,struct
from collections import defaultdict
from itertools import permutations
from pathlib import Path
import numpy as np
from models import Asset,entries,game

def vec3(a,row,field,default):
    ref=a.field(row,field)
    if not ref:return np.full(3,default,dtype=float)
    obj=a.ref(ref)
    return np.array([struct.unpack_from('<f',a.b,a.field(obj,j))[0] if a.field(obj,j) else default for j in range(3)])
def tables():
    members={i:b for i,_,b in entries(game/'archive/field/resident_release.gfpak')}
    source=Asset(members[81]);catalog={}
    for o in source.tables(source.root(),0):
        key=struct.unpack_from('<Q',source.b,source.field(o,0))[0]
        model=source.string(o,8)
        if key in catalog:raise ValueError('Duplicate field model ID')
        catalog[key]=model
    areas={}
    for i in [94,1086,1148,1365,1520,1675,1954]:
        names=Asset(members[i]);rows=names.tables(names.root(),0)
        labels={m[1] for o in rows if (m:=re.fullmatch(r'bg_area(\d\d)_\d+',names.string(o,0).strip()))}
        if len(labels)!=1:raise ValueError('Ambiguous field placement table')
        area='ha_area'+labels.pop()
        a=Asset(members[i+1]);placements=defaultdict(list)
        for o in a.tables(a.root(),0):
            if not a.field(o,2):continue
            key=struct.unpack_from('<Q',a.b,a.field(o,2))[0]
            path=catalog.get(key,'')
            if not path.startswith('bin/field/model/pack/') or not path.endswith('.trmdl'):continue
            name=Path(path).stem
            for z in a.tables(o,4):
                xyz=vec3(a,z,7,0.);rot=vec3(a,z,8,0.);scale=vec3(a,z,9,1.)
                cond=tuple(a.string(z,j) if a.field(z,j) else '' for j in range(2,7))
                if not np.isfinite([xyz,rot,scale]).all() or min(abs(scale))<.001:continue
                placements[name].append((xyz,rot,scale,cond,a.string(o,0)))
        areas[area]=placements
    return areas
def transform(xyz,degree,scale,order='xyz'):
    x,y,z=np.deg2rad(degree);cx,cy,cz=np.cos([x,y,z]);sx,sy,sz=np.sin([x,y,z])
    axes={'x':[[1,0,0],[0,cx,sx],[0,-sx,cx]],'y':[[cy,0,-sy],[0,1,0],[sy,0,cy]],'z':[[cz,sz,0],[-sz,cz,0],[0,0,1]]}
    m=np.diag(scale)
    for axis in order:m=m@axes[axis]
    return np.vstack([m,xyz])
if __name__=='__main__':
    import runpy
    placed=runpy.run_path(str(Path(__file__).with_name('placed-structures.py')))
    areas=tables()
    for area,models in areas.items():
        eligible=sum(not any(cond) for rows in models.values() for _,_,_,cond,_ in rows)
        print(area,len(models),'models',sum(map(len,models.values())),'placements',eligible,'with empty conditions')
    records=areas['ha_area00'];samples=[]
    for model,instances in placed['recover']().items():
        for inst in instances:
            tr=inst['tr'];source=records.get(model,[])
            if not source:continue
            xyz,r,s,_,_=min(source,key=lambda v:np.linalg.norm(v[0]-tr[3]))
            if np.max(abs(xyz-tr[3]))>.001:continue
            samples.append((model,xyz,r,s,tr))
    for order in permutations('xyz'):
        err=np.array([abs(transform(x,r,s,order)-tr).max() for m,x,r,s,tr in samples])
        print(''.join(order),'verified',sum(err<.001),'of',len(samples),'worst',err.max().round(5))
