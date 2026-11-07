"""Add unconditional native rock, stone and fallen-tree placements to existing field instance tables."""
from pathlib import Path
from collections import defaultdict
import runpy,struct,shutil,sys
import numpy as np
from PIL import Image
from models import Asset,game,here,entries
from pak_paths import directory_index,name_hash
from texture import texture
scene=runpy.run_path(str(here/'resident-placement.py'))
parts=runpy.run_path(str(here/'tree-instances.py'))['primitives']
decode=runpy.run_path(str(here/'terrain-uv.py'))['native_mip']
def existing_mesh(path):
    result=defaultdict(list)
    if not path.exists():return result
    b=path.read_bytes();_,version,n,m,g=struct.unpack_from('<4s4I',b)
    if version not in (5,6,7):raise ValueError(path)
    offset={5:32,6:36,7:40}[version]
    a=np.frombuffer(b,'<f4',12*n,offset).reshape(-1,4)
    check=offset+52*n+32768*m;start=0
    for j in range(g):
        k=3*struct.unpack_from('<I',b,check+64*j)[0]
        result[k].append(a[start:start+k]);start+=k
    assert start==3*n
    return result
def export(src,dest,area,models,tilted=False):
    path=src/(area+'.grove');b=path.read_bytes()
    magic,version,archive,ns,ni,nt=struct.unpack_from('<4s5I',b)
    assert magic==b'GRVE' and version==1
    a=24;partsOut=[]
    for size in [ns*32,ni*64,nt*48,ns*16384]:
        partsOut.append([b[a:a+size]]);a+=size
    assert a==len(b)
    shapes,placements,mesh,color=partsOut;rough=[bytes(ns*16384)]
    old=(ns,ni,nt);geometry=existing_mesh(src/(area+'.props'))
    filtered={}
    for name,instances in models.items():
        rock=name.startswith('ground_') and any(k in name for k in ('rock','stone','fallentree'))
        additional=(name.startswith('ground_') and any(k in name for k in ('cliff','stump','cave'))) or name.startswith(('objects_stairs','objects_stonepaving','objects_stonepillar'))
        if not rock and not (tilted and additional):continue
        filtered[name]=[row for row in instances if not any(row[3]) and (tilted or np.max(abs(row[1][[0,2]]))<.001)]
    pack=game/'archive/field/model/pack'/(area+'.gfpak')
    idx=directory_index(pack);needed={};assets={};names=set()
    for name in sorted(filtered):
        if not filtered[name]:continue
        ids=[idx.get(name_hash(name+ext),[]) for ext in ('.trmsh','.trmbf','.trmtr')]
        if all(len(k)==1 for k in ids):needed[name]=[k[0] for k in ids]
    wanted={i for row in needed.values() for i in row}
    for i,_,data in entries(pack):
        if i in wanted:assets[i]=Asset(data)
    for ids in needed.values():
        mat=assets[ids[2]]
        for t in mat.tables(mat.root(),1):
            names.update(Path(mat.string(o,1)).stem for o in mat.tables(t,2))
    textures={}
    for _,_,data in entries(pack):
        if data[:4]!=b'BNTX':continue
        name,fmt,w,h,mips=texture(data)
        if name not in names:continue
        lv=0
        while max(w,h)>>(lv+1)>=64 and lv+1<len(mips):lv+=1
        textures[name]=fmt&255,np.array(Image.fromarray(decode(data,lv)).convert('RGBA').resize((64,64),Image.Resampling.BILINEAR))
    dropped=0
    for name,ids in needed.items():
        sh,buf,mat=[assets[i] for i in ids]
        if any(sh.number(a,1) in (7,8) for shape in sh.tables(sh.root(),1) for layout in sh.tables(shape,3) for a in sh.tables(layout,0)):continue
        table={mat.string(t,0):t for t in mat.tables(mat.root(),1)}
        for _,label,pos,uv in parts(sh,buf):
            t=table[label];n=len(pos)//3
            shaders=[mat.string(z,0) for z in mat.tables(t,1)]
            binds={mat.string(z,0):Path(mat.string(z,1)).stem for z in mat.tables(t,2)}
            opt={mat.string(z,0):mat.number(z,1) for z in mat.tables(t,9)}
            scalar={mat.string(z,0):struct.unpack_from('<f',mat.b,mat.field(z,1))[0] if mat.field(z,1) else 0. for z in mat.tables(t,4)}
            vectors={mat.string(z,0):struct.unpack_from('<4f',mat.b,mat.field(z,1)) for z in mat.tables(t,7) if mat.field(z,1)}
            if shaders!=['Standard'] or opt.get('CastShadow')!=1 or scalar.get('EmissionIntensity',0)!=0 or scalar.get('DiscardValue',0)!=0:continue
            if set(binds)-{'BaseColorMap','NormalMap','PackedMap','AOMap','MetallicMap','RoughnessMap','EmissionColorMap'} or vectors.get('UVScaleOffset',(1,1,0,0))!=(1,1,0,0):continue
            if not 0<n<16384 or len(pos)!=3*n or not np.isfinite(pos).all() or not np.isfinite(uv).all():continue
            if binds.get('BaseColorMap') not in textures:continue
            fmt,alb=textures[binds['BaseColorMap']]
            if fmt!=6:continue
            properties=np.zeros_like(alb);properties[:,:,3]=255
            try:
                if 'PackedMap' in binds:properties[:,:,:2]=textures[binds['PackedMap']][1][:,:,1:3]
                else:
                    for j,key in enumerate(('Roughness','Metallic')):
                        if key+'Map' in binds:properties[:,:,j]=textures[binds[key+'Map']][1][:,:,0]
                        elif 0<=scalar.get(key,-1)<=1:properties[:,:,j]=round(255*scalar[key])
                        else:raise KeyError(key)
            except KeyError:continue
            xyz=pos.astype(float);ia=int(np.argmax(np.linalg.norm(xyz-xyz.mean(0),axis=1)))
            ib=int(np.argmax(np.linalg.norm(xyz-xyz[ia],axis=1)))
            ic=int(np.argmax(np.linalg.norm(np.cross(xyz-xyz[ia],xyz[ib]-xyz[ia]),axis=1)))
            if np.linalg.norm(np.cross(xyz[ib]-xyz[ia],xyz[ic]-xyz[ia]))<.01:continue
            vert=np.empty((3*n,4),'<f4');vert[:,:3]=pos;st=uv.copy();st[:,1]=1-st[:,1];vert[:,3]=st.astype('<f2').view('<f4').ravel()
            instances=[];seen=set()
            for x,angles,scale,_,_ in filtered[name]:
                tr=scene['transform'](x,angles,scale,'zxy' if tilted else 'xyz')
                raw=tr.T.astype('<f4').tobytes()
                if raw in seen:continue
                seen.add(raw)
                world=xyz@tr[:3]+tr[3]
                if any(abs(v[ia,:3]-world[ia]).max()<.01 and abs(v[:,:3]-world).max()<.01 and np.array_equal(v[:,3].view('<u4'),vert[:,3].view('<u4')) for v in geometry[3*n]):
                    dropped+=1;continue
                instances.append(raw+struct.pack('<4I',ns,1,0,0))
            if not instances:continue
            shapes.append(struct.pack('<8I',nt,n,0,0x80000000,ia,ib,ic,len(pos)//2))
            mesh.append(vert.tobytes());color.append(alb.tobytes());rough.append(properties.tobytes());placements.extend(instances)
            ns+=1;ni+=len(instances);nt+=n
    assert ns<=128 and ni<=8192 and nt<=1000000,(ns,ni,nt)
    with (dest/path.name).open('wb') as f:
        f.write(struct.pack('<4s5I',magic,2,archive,ns,ni,nt))
        for rows in [shapes,placements,mesh,color,rough]:f.writelines(rows)
    print(area,'new shapes, placements, unique triangles',ns-old[0],ni-old[1],nt-old[2],'existing groups excluded',dropped,flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:3]);dst.mkdir(exist_ok=True)
    for f in src.iterdir():
        if f.is_file():shutil.copy2(f,dst/f.name)
    for area,models in scene['tables']().items():export(src,dst,area,models,'--tilted-static' in sys.argv)
