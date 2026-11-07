"""Package the GPU splat map and repeating terrain textures with unambiguous material bindings."""
import collections,struct,sys,shutil,runpy
from pathlib import Path
import numpy as np
from PIL import Image
from models import Asset
from texture import texture,entries,game,here
decode=runpy.run_path(str(here/'terrain-uv.py'))['native_mip']
def table(blob):
    f=Asset(blob);items={}
    for t in f.tables(f.root(),1):
        if f.string(t,0)!='TerrainMaterial':continue
        names={f.string(b,0):f.string(b,1) for b in f.tables(t,2)}
        scalar={f.string(b,0):struct.unpack_from('<f',blob,f.field(b,1))[0] if f.field(b,1) else 0. for b in f.tables(t,4)}
        vec={f.string(b,0):struct.unpack_from('<4f',blob,f.field(b,1)) for b in f.tables(t,7) if f.field(b,1)}
        for j in range(8):
            name=names.get('SplatColor'+str(j));st=vec.get('SplatST'+str(j))
            if name and st and min(st[:2])>0:
                items[j]=(Path(name).stem,Path(names.get('SplatNormal'+str(j),'')).stem,st,st==vec.get('SplatST'+str(j)+'Far'),scalar.get('Roughness' if not j else 'RoughnessLayer'+str(j),-1.))
    return items
def linear_mip(blob):
    name,fmt,w,h,mips=texture(blob);level=0
    while min(w,h)>>(level+1)>=128 and level+1<len(mips):level+=1
    base=Image.fromarray(decode(blob,level)).convert('RGB').resize((128,128),Image.Resampling.BILINEAR)
    rgb=np.asarray(base)/255
    if (fmt&255)==6:rgb=np.where(rgb>.04045,((rgb+.055)/1.055)**2.4,rgb/12.92)
    data=[]
    for size in [128,64,32,16,8,4,2,1]:
        q=np.rint(np.clip(rgb,0,1)*1023).astype('<u4')
        data.append((q[:,:,0]|(q[:,:,1]<<10)|(q[:,:,2]<<20)).tobytes())
        if size>1:rgb=rgb.reshape(size//2,2,size//2,2,3).mean((1,3))
    return b''.join(data)
def normal_mips(blob):
    name,fmt,w,h,mips=texture(blob)
    assert fmt==0x1e01 and w==h and w>=256,(name,hex(fmt),w,h)
    level=(w//256).bit_length()-1
    data=[]
    for mip in range(9):
        xy=decode(blob,level+mip)[:,:,:2].astype('<u2')
        data.append((xy[:,:,0]|(xy[:,:,1]<<8)).tobytes())
    return b''.join(data)+bytes(2)  # 87381 uint16 samples plus word alignment

def export(pack,dest,catalog):
    layers=collections.defaultdict(set);alpha=None;local={}
    for _,_,blob in entries(pack):
        if blob[:4]==b'BNTX':
            name,fmt,w,h,mips=texture(blob)
            if 'splat_alpha' in name:
                if alpha is not None:return
                alpha=blob
            elif name.endswith(('_alb','_nrm')):local[name]=blob
        elif len(blob)<65536 and b'SplatColor0' in blob:
            for idx,params in table(blob).items():layers[idx].add(params)
    if not alpha:return
    name,fmt,side,h,mips=texture(alpha)
    brti=struct.unpack_from('<Q',alpha,struct.unpack_from('<Q',alpha,0x28)[0])[0]
    banks=struct.unpack_from('<I',alpha,brti+0x30)[0]
    assert fmt==0x2001 and h==side and side in (1024,2048) and banks in (1,2) and len(mips)==1,(name,fmt,side,banks)
    mipPtr=struct.unpack_from('<Q',alpha,brti+0x70)[0];data=bytearray(alpha);splat=[]
    for i in range(banks):
        struct.pack_into('<Q',data,mipPtr,mips[0]+side*side*i)
        splat.append(decode(bytes(data),0).tobytes())
    meta=[];atlas=[];normals=[];mask=0
    for i in range(4*banks):
        name=normal='';st=(1,1,0,0);rough=1.;fixed=False
        candidates=layers[i]
        identities={(name,normal,rough) for name,normal,st,fixed,rough in candidates}
        if len(identities)==1:
            name,normal,rough=next(iter(identities))
            if len(candidates)==1:_,_,st,fixed,_=next(iter(candidates))
        blob=local.get(name,catalog.get(name))
        normal_blob=local.get(normal,catalog.get(normal))
        eligible=blob is not None and 0<=rough<=1
        detail=eligible and fixed and i!=0 and normal_blob is not None and texture(normal_blob)[2]>=256
        meta.append(struct.pack('<8f',1/st[0],1/st[1],st[2]/st[0],st[3]/st[1],rough if eligible else 1.,float(eligible),float(detail),float(eligible and not fixed)))
        atlas.append(linear_mip(blob) if eligible else bytes(21845*4))
        normals.append(normal_mips(normal_blob) if detail else bytes(43691*4))
        mask|=int(eligible)<<i
    target=dest/(pack.stem+'.splat')
    with target.open('wb') as f:
        f.write(struct.pack('<4s5I',b'SPLT',2,pack.stat().st_size,side,banks,mask));f.writelines(meta+splat+atlas+normals)
    print(pack.stem,side,banks,hex(mask),target.stat().st_size,flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:]);dst.mkdir(exist_ok=True)
    for p in src.iterdir():
        if p.is_file():shutil.copy2(p,dst/p.name)
    packs=list(sorted((game/'archive/field/terrain').glob('ha_area??.gfpak')))
    catalog={}
    for pack in packs:
        for _,_,b in entries(pack):
            if b[:4]!=b'BNTX':continue
            name=texture(b)[0]
            if not name.endswith(('_alb','_nrm')):continue
            if name not in catalog:catalog[name]=b
            elif catalog[name]!=b:catalog[name]=None
    for pack in packs:export(pack,dst,catalog)
