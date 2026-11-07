"""Recover rigid model placements from full native/live vertex and UV correspondences."""
import collections,re,runpy,sys,struct
from pathlib import Path
import numpy as np
from models import Asset,entries,game,here,strings
from pak_paths import name_hash,directory_index
parts=runpy.run_path(str(here/'tree-instances.py'))['primitives']
read=runpy.run_path(str(here/'static-scenes.py'))['read']
def catalog(area):
    pack=game/'archive/field/model/pack'/(area+'.gfpak')
    index=directory_index(pack)
    members={i:b for i,_,b in entries(pack)}
    models=set()
    for b in members.values():
        if len(b)>65536 or b'.trmsh' not in b or b'.trmtr' not in b:continue
        models.update(Path(n).stem for n in strings(b) if n.endswith('.trmsh'))
    for model in sorted(models):
        ids=[index.get(name_hash(model+ext),[]) for ext in ('.trmsh','.trmbf','.trmtr')]
        if not all(len(v)==1 for v in ids):continue
        mesh,buff,mat=[Asset(members[v[0]]) for v in ids]
        table={mat.string(t,0):t for t in mat.tables(mat.root(),1)}
        try: geometry=list(parts(mesh,buff))
        except (AssertionError,IndexError,StopIteration,ValueError):continue
        yield model,geometry,mat,table
def capture_index(paths=None):
    rows=collections.defaultdict(list)
    for f in sorted(paths if paths is not None else (here/'static').glob('*.bin')):
        for name,cut,p,uv in read(f):
            if len(p)<12 or not np.isfinite(p).all() or not np.isfinite(uv).all():continue
            rows[len(p)].append((f.stem,name,cut,p,uv))
    return rows
def recover(area='ha_area00',paths=None):
    rows=capture_index(paths);matched=collections.defaultdict(list)
    for model,geometry,mat,tables in catalog(area):
        if not model.startswith(('objects_','ground_','room_')):continue
        for k,(shape,material,p,uv) in enumerate(geometry):
            if not len(p) or len(p) not in rows:continue
            t=tables[material]
            bind={mat.string(b,0):mat.string(b,1) for b in mat.tables(t,2)}
            name=Path(bind.get('BaseColorMap','')).stem
            ix=np.linspace(0,len(p)-1,min(len(p),43),dtype=int)
            src=p[ix].astype(float);src=np.column_stack([src,np.ones(len(src))])
            if np.linalg.matrix_rank(src)<4:continue
            uv=uv.copy();uv[:,1]=1-uv[:,1]
            for file,texture,cut,q,actual in rows[len(p)]:
                if texture and name and texture!=name:continue
                if np.max(abs(uv[ix]-actual[ix]))>.001:continue
                tr=np.linalg.lstsq(src,q[ix],rcond=None)[0]
                if max(abs(np.linalg.svd(tr[:3],compute_uv=False)-1))>.12:continue
                if np.max(abs(src@tr-q[ix]))>.005:continue
                err=float(np.max(abs(p@tr[:3]+tr[3]-q)))
                if err>.005 or np.max(abs(uv-actual))>.001:continue
                found=None
                for v in matched[model]:
                    if np.max(abs(tr-v['tr']))<.005:found=v;break
                if found is None:
                    found=dict(tr=tr,frames=set(),parts=set(),textures=set(),cutoffs=collections.defaultdict(set),maxError=0.,total=len(geometry));matched[model].append(found)
                if name and texture==name:found['textures'].add(k)
                found['cutoffs'][k].add(int(cut));found['frames'].add(file);found['parts'].add(k);found['maxError']=max(found['maxError'],err)
    return matched
if __name__=='__main__':
    matches=recover()
    for m,instances in matches.items():
        print(m,[(len(v['frames']),len(v['parts']),v['total'],round(v['maxError'],5)) for v in instances])
    print('models',len(matches),'placements',sum(map(len,matches.values())))
