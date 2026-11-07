"""Extend verified trunk models to matching field archives. A live witness activates each model in game."""
import hashlib,re,struct,sys,shutil,runpy
from pathlib import Path
import numpy as np
from models import Asset,game,here,entries,strings
from pak_paths import name_hash,directory_index
from texture import texture
v=runpy.run_path(str(here/'export-trees.py'))
reference={}
for model,transforms,(mesh,buff,material) in v['tree']['village']():
    reference[model]=(mesh.b,buff.b,material.b)
def export(pack,dest):
    index=directory_index(pack);names=set();materials={};instances={};components={}
    for model,ref in reference.items():
        ids=[index.get(name_hash(model+ext),[]) for ext in ['.trmsh','.trmbf','.trmtr']]
        if all(len(group)==1 for group in ids):components[model]=[g[0] for g in ids]
    allowed={i for ids in components.values() for i in ids}
    for i,_,b in entries(pack):
        if i in allowed:materials[i]=b
        if len(b)<1000000 and b'.trins' in b:names.update(n for n in strings(b) if n.endswith('.trins'))
    for n in sorted(names):
        m=re.search(r'_(plants_tree01|plants_tree02|plants_area02_tree01)_X\d+Z\d+_d0.trins$',n)
        if not m:continue
        name=m.group(1)
        if name not in components:continue
        if tuple(materials[i] for i in components[name])!=reference[name]:continue
        member=index[name_hash(n)]
        if len(member)!=1:raise ValueError(n)
        if member[0] in instances:raise ValueError('Ambiguous tree placement')
        instances[member[0]]=name
    if not instances:return 0
    transforms={};albedos={}
    for i,_,b in entries(pack):
        if i in instances:
            f=Asset(b);t=f.ref(f.field(f.root(),4));off,size=f.vec(t,0)
            assert size%64==0
            x=np.frombuffer(b,'<f4',size//4,off).reshape(-1,4,4)
            assert np.isfinite(x).all() and np.max(abs(x[:,:,3]-[0,0,0,1]))<1e-6
            assert np.all(np.abs(np.linalg.det(x[:,:3,:3]))>.01)
            transforms.setdefault(instances[i],[]).extend(x)
        elif b[:4]==b'BNTX':
            name=texture(b)[0]
            if name.startswith(tuple(components)) and name.endswith('_alb'):albedos[name]=b
    vertices=[];tags=[];atlas=[];properties=[];witness=[]
    for model,tr in sorted(transforms.items()):
        m,b,mt=map(Asset,reference[model])
        tables={mt.string(t,0):t for t in mt.tables(mt.root(),1)}
        for shape,name,p,uv in v['tree']['primitives'](m,b):
            table=tables[name]
            bind={mt.string(z,0):mt.string(z,1) for z in mt.tables(table,2)}
            if 'WindMaskMap' in bind:continue
            scalar={mt.string(z,0):struct.unpack_from('<f',mt.b,mt.field(z,1))[0] if mt.field(z,1) else 0. for z in mt.tables(table,4)}
            ident=len(atlas);cut=round(scalar['DiscardValue']*255);n=len(p)//3
            atlas.append(v['atlas'](albedos[Path(bind['BaseColorMap']).stem]))
            properties.append(bytes([round(scalar['Roughness']*255),round(scalar['Metallic']*255),0,255])*4096)
            uv=uv.copy();uv[:,1]=1-uv[:,1];packedUV=uv.astype('<f2').view('<f4').ravel()
            a=int(np.argmax(np.linalg.norm(p-p.mean(0),axis=1)));b1=int(np.argmax(np.linalg.norm(p-p[a],axis=1)))
            c=int(np.argmax(np.linalg.norm(np.cross(p-p[a],p[b1]-p[a]),axis=1)))
            for transform in tr:
                points=p@transform[:3,:3]+transform[3,:3]
                buf=np.empty((len(p),4),'<f4');buf[:,:3]=points;buf[:,3]=packedUV
                tags.append(np.full(n,(len(witness)<<16)|(cut<<8)|ident,'<u4').tobytes())
                witness.append(struct.pack('<4I',n,a,b1,c)+b''.join(struct.pack('<4f',*points[j],float(ident+1) if k==0 else 0.) for k,j in enumerate((a,b1,c))))
                vertices.append(buf.tobytes())
    nt=sum(len(t)//4 for t in tags)
    assert nt<=1000000 and 0<len(witness)<=1024 and len(atlas)<=255
    out=dest/(pack.stem+'.props')
    with out.open('wb') as file:
        file.write(struct.pack('<4s6I',b'PROP',4,nt,len(atlas),len(witness),pack.stat().st_size,0))
        for group in [vertices,tags,atlas,properties,witness]:file.writelines(group)
    print(pack.stem,nt,'triangles',len(witness),'placements',len(atlas),'classes',flush=True)
    return len(witness)
if __name__=='__main__':
    src,dest=map(Path,sys.argv[1:]);dest.mkdir(exist_ok=True)
    for p in src.iterdir():
        if p.is_file():shutil.copy2(p,dest/p.name)
    total=0
    for p in sorted((game/'archive/field/terrain').glob('ha_area??.gfpak')):
        if p.stem!='ha_area00':total+=export(p,dest)
    print('additional field placements',total)
