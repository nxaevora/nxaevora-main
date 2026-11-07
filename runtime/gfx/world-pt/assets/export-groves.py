"""Store each supported static plant mesh alongside its archive placements and material."""
import collections,re,runpy,struct,sys,shutil
from pathlib import Path
import numpy as np
from PIL import Image
from models import Asset,entries,here,game,strings
from texture import texture
from pak_paths import directory_index,name_hash
tree=runpy.run_path(str(here/'tree-instances.py'))
decode=runpy.run_path(str(here/'terrain-uv.py'))['native_mip']
def export(pack,dest):
    index=directory_index(pack)
    placements=collections.defaultdict(set)
    for _,_,b in entries(pack):
        if len(b)>1000000 or b'.trins' not in b:continue
        for n in strings(b):
            match=re.search(r'_(plants_[A-Za-z0-9_]*(?:tree|shrub)[A-Za-z0-9_]*)_X\d+Z\d+_d0\.trins$',n)
            if match:placements[match[1]].add(n)
    by_instance={};models={}
    for model,names in sorted(placements.items()):
        ids=[index.get(name_hash(model+ext),[]) for ext in ('.trmsh','.trmbf','.trmtr')]
        if any(len(k)!=1 for k in ids):continue
        models[model]=[k[0] for k in ids]
        for n in names:
            if len(index[name_hash(n)])!=1:raise ValueError(n)
            by_instance[index[name_hash(n)][0]]=model
    wanted={i for ids in models.values() for i in ids}
    components={};textures={};transforms=collections.defaultdict(list)
    for i,_,b in entries(pack):
        if i in wanted:components[i]=Asset(b)
        elif i in by_instance:
            f=Asset(b);t=f.ref(f.field(f.root(),4));off,size=f.vec(t,0)
            if size%64:raise ValueError('Bad placement table')
            matrices=np.frombuffer(b,'<f4',size//4,off).reshape(-1,4,4)
            for mat in matrices:
                assert np.isfinite(mat).all() and np.max(abs(mat[:,3]-[0,0,0,1]))<1e-5
                if np.abs(np.linalg.det(mat[:3,:3]))>.001:transforms[by_instance[i]].append(mat)
        elif b[:4]==b'BNTX':
            name=texture(b)[0]
            if name.endswith('_alb'):textures[name]=b
    shapes=[];positions=[];colors=[];instances=[];sourceCount=0;skipped=[]
    for model,ids in models.items():
        mesh,buff,material=[components[i] for i in ids]
        tables={material.string(t,0):t for t in material.tables(material.root(),1)}
        try:parts=list(tree['primitives'](mesh,buff))
        except (StopIteration,RuntimeError,IndexError,ValueError,AssertionError):skipped.append(model);continue
        for _,name,pos,uv in parts:
            t=tables[name]
            bind={material.string(b,0):material.string(b,1) for b in material.tables(t,2)}
            scalar={material.string(b,0):struct.unpack_from('<f',material.b,material.field(b,1))[0] if material.field(b,1) else 0. for b in material.tables(t,4)}
            if not set(bind)<= {'BaseColorMap','NormalMap'} or 'BaseColorMap' not in bind:continue
            if any('Wind' in key for key in scalar) or scalar.get('EmissionIntensity',0)!=0 or 'Roughness' not in scalar or 'Metallic' not in scalar:continue
            n=len(pos)//3;tex=Path(bind['BaseColorMap']).stem
            if len(pos)%3 or not 0<n<16384 or tex not in textures:continue
            blob=textures[tex];_,fmt,w,h,mips=texture(blob)
            if (fmt&255)!=6 or fmt>>8 not in (26,27,28,32):continue
            level=0
            while max(w,h)>>(level+1)>=64 and level+1<len(mips):level+=1
            im=Image.fromarray(decode(blob,level)).convert('RGBA').resize((64,64),Image.Resampling.BILINEAR)
            cut=round(scalar.get('DiscardValue',0)*255)
            rough=round(scalar['Roughness']*255);metal=round(scalar['Metallic']*255)
            if any(q<0 or q>255 for q in (cut,rough,metal)):continue
            a=int(np.argmax(np.linalg.norm(pos-pos.mean(0),axis=1)))
            b=int(np.argmax(np.linalg.norm(pos-pos[a],axis=1)))
            c=int(np.argmax(np.linalg.norm(np.cross(pos-pos[a],pos[b]-pos[a]),axis=1)))
            if np.linalg.norm(np.cross(pos[c]-pos[a],pos[b]-pos[a]))<.01:continue
            shape=len(shapes)
            shapes.append(struct.pack('<8I',sourceCount,n,cut,rough|(metal<<8),a,b,c,len(pos)//2))
            vert=np.empty((len(pos),4),'<f4');vert[:,:3]=pos
            uv=uv.copy();uv[:,1]=1.-uv[:,1];vert[:,3]=uv.astype('<f2').view('<f4').ravel()
            assert np.isfinite(pos).all() and np.isfinite(uv).all()
            positions.append(vert.tobytes());colors.append(im.tobytes());sourceCount+=n
            for mat in transforms[model]:
                instances.append(mat[:,:3].T.copy().tobytes()+struct.pack('<4I',shape,0,0,0))
    if not shapes:return
    assert len(shapes)<=128 and len(instances)<=8192
    target=dest/(pack.stem+'.grove')
    with target.open('wb') as f:
        f.write(struct.pack('<4s5I',b'GRVE',1,pack.stat().st_size,len(shapes),len(instances),sourceCount))
        for data in [shapes,instances,positions,colors]:f.writelines(data)
    print(pack.stem,len(shapes),'meshes',len(instances),'instances',sourceCount,'unique triangles',target.stat().st_size,'bytes','unsupported layouts',skipped,flush=True)
if __name__=='__main__':
    source,dest=map(Path,sys.argv[1:]);dest.mkdir(exist_ok=True)
    for p in source.iterdir():
        if p.is_file() and p.suffix!='.grove':shutil.copy2(p,dest/p.name)
    for pack in sorted((game/'archive/field/terrain').glob('ha_area??.gfpak')):export(pack,dest)
