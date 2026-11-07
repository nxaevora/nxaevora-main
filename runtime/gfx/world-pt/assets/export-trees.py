"""Export static native tree placements after checking full position and UV streams against live draws."""
import struct
from collections import defaultdict
from pathlib import Path
from PIL import Image
import numpy as np
import runpy, shutil, sys
from texture import texture, here, game, entries
tree=runpy.run_path(str(here/'tree-instances.py'))
read=runpy.run_path(str(here/'static-scenes.py'))['read']

def atlas(blob):
    name,fmt,w,h,mips=texture(blob)
    assert fmt==0x2006 and w==h and w>=64 and (w&(w-1))==0
    level=(w//64).bit_length()-1
    brti=struct.unpack_from('<Q',blob,struct.unpack_from('<Q',blob,0x28)[0])[0]
    bh=struct.unpack_from('<I',blob,brti+0x34)[0]&7
    while bh and (8<<(bh-1))>=16:bh-=1
    y,x=np.indices((16,256))
    off=((y>>(3+bh))*4+(x>>6))*(512<<bh)+(((y>>3)&((1<<bh)-1))*512)+((x&32)<<3)+((y&6)<<5)+((x&16)<<1)+((y&1)<<4)+(x&15)
    blocks=np.frombuffer(blob,'u1')[mips[level]+off].tobytes()
    return Image.frombytes('RGBA',(64,64),blocks,'bcn',(7,'BC7')).tobytes()

def extend(source,dest):
    captures=defaultdict(list)
    for path in (here/'static').glob('*.bin'):
        for name,cut,p,uv in read(path):captures[len(p)].append((p,uv))
    blob=(source/'ha_area00.props').read_bytes()
    magic,version,nt,nm,ng,archive=struct.unpack_from('<4s5I',blob)
    assert (magic,version)==(b'PROP',2)
    nv=nt*48
    def section(offset,n):return [blob[24+offset:24+offset+n]]
    vertices=section(0,nv);tags=section(nv,4*nt);off=nv+4*nt
    colors=section(off,16384*nm);properties=section(off+16384*nm,16384*nm)
    keys=section(off+32768*nm,64*ng)
    assert 24+off+32768*nm+64*ng==len(blob)
    pack=game/'archive/field/terrain/ha_area00.gfpak'
    assert pack.stat().st_size==archive
    textures={texture(b)[0]:b for _,_,b in entries(pack) if b[:4]==b'BNTX' and b'tree' in b[:4096]}
    firstPlaced=ng
    for model,transforms,(mesh,buff,material) in tree['village']():
        tables={material.string(t,0):t for t in material.tables(material.root(),1)}
        for shape,name,pos,uv in tree['primitives'](mesh,buff):
            t=tables[name]
            bindings={material.string(b,0):material.string(b,1) for b in material.tables(t,2)}
            if 'WindMaskMap' in bindings:continue
            scalar={material.string(b,0):struct.unpack_from('<f',material.b,material.field(b,1))[0] if material.field(b,1) else 0. for b in material.tables(t,4)}
            assert set(bindings)<= {'BaseColorMap','NormalMap'} and scalar['EmissionIntensity']==0 and len(pos)%3==0
            uv=uv.copy();uv[:,1]=1-uv[:,1]
            world=pos@transforms[:,:3,:3]+transforms[:,None,3,:3]
            checks=[]
            for i,p in enumerate(world):
                for live,liveUV in captures[len(p)]:
                    if np.max(abs(p[0]-live[0]))>.005:continue
                    error=np.abs(p-live).max();delta=np.abs(uv-liveUV).max()
                    if error<.005 and delta<.0001:checks.append((i,float(error),float(delta)))
            assert checks,('No full live position and UV match',model,name)
            print(model,name,len(transforms),'instances',len({i for i,_,_ in checks}),'live placements verified',max(e for _,e,_ in checks),max(d for _,_,d in checks))
            albedo=atlas(textures[Path(bindings['BaseColorMap']).stem]);colors.append(albedo)
            properties.append(bytes([round(scalar['Roughness']*255),round(scalar['Metallic']*255),0,255])*4096)
            cut=round(scalar['DiscardValue']*255)
            for p in world:
                far=int(np.argmax(np.linalg.norm(p-p.mean(0),axis=1)))
                end=int(np.argmax(np.linalg.norm(p-p[far],axis=1)))
                side=int(np.argmax(np.linalg.norm(np.cross(p-p[far],p[end]-p[far]),axis=1)))
                n=len(p)//3
                keys.append(struct.pack('<4I',n,far,end,side)+b''.join(struct.pack('<4f',*p[i],0) for i in [far,end,side]))
                v=np.empty((len(p),4),'<f4');v[:,:3]=p;v[:,3]=uv.astype('<f2').view('<f4').ravel()
                vertices.append(v.tobytes());tags.append(np.full(n,(ng<<16)|(cut<<8)|nm,'<u4').tobytes());ng+=1;nt+=n
            nm+=1
    assert ng-firstPlaced==60 and nm<256
    dest.mkdir(parents=True,exist_ok=True)
    for p in source.glob('*.land'):shutil.copy2(p,dest/p.name)
    (dest/'ha_area00.props').write_bytes(struct.pack('<4s6I',magic,3,nt,nm,ng,archive,firstPlaced)+b''.join(vertices+tags+colors+properties+keys))
    print(nt,'total triangles,',ng,'groups, archive-placed groups start at',firstPlaced)
if __name__=='__main__':extend(*map(Path,sys.argv[1:]))
