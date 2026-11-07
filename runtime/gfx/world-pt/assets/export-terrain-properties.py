"""Attach terrain shading normals and measured per-layer roughness to adaptive v4 tiles."""
import struct
import sys
from pathlib import Path
import shutil
import numpy as np
from PIL import Image
from texture import texture
from models import Asset
from readpak import entries, game

def material_table(blob):
    material=Asset(blob)
    result={}
    for table in material.tables(material.root(),1):
        if material.string(table,0)!="TerrainMaterial":
            continue
        textures={material.string(t,0):material.string(t,1) for t in material.tables(table,2)}
        scalars={material.string(t,0):struct.unpack_from('<f',blob,material.field(t,1))[0] if material.field(t,1) else 0.
                 for t in material.tables(table,4)}
        for index in range(16):
            tex=textures.get('SplatColor'+str(index))
            rough=scalars.get('RoughnessLayer'+str(index) if index else 'Roughness')
            if tex and tex.endswith('_alb.bntx') and rough is not None and 0<=rough<=1:
                result[index]=(tex[:-5],rough)
    return result

def terrain_properties(source):
    layers={}
    palette={}
    terrain=None
    for _,_,blob in entries(source):
        if blob[:4]==b'BNTX':
            tex,fmt,width,height,mips=texture(blob)
            if tex.endswith('_alb') and (fmt>>8) in (26,27,28,32) and max(width,height)>>(len(mips)-1)<=4:
                bc={26:1,27:2,28:3,32:7}[fmt>>8]
                size=8 if bc==1 else 16
                image=Image.frombytes('RGBA',(4,4),blob[mips[-1]:mips[-1]+size],'bcn',(bc,'BC'+str(bc)))
                rgb=np.asarray(image,dtype='float64')[0,0,:3]/255
                palette[tex]=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4) if (fmt&255)==6 else rgb
        if len(blob)<262144 and blob[:4]!=b'BNTX' and b'TerrainMaterial' in blob and b'SplatColor0' in blob:
            for slot,pair in material_table(blob).items():
                layers.setdefault(slot,set()).add(pair)
        if terrain is None and len(blob)>65536 and blob[:4]!=b'BNTX' and blob[4:8]!=b'TAG0':
            asset=Asset(blob)
            try:
                root=asset.root()
                side=asset.number(root,3)
                if side not in (513,1025,2049,4097) or side!=asset.number(root,4):continue
                offset,count=asset.vec(root,5)
                if count!=side**2:continue
                scale=asset.fields(asset.ref(asset.field(root,1)))
                height=np.frombuffer(blob,'<f4',count,offset).reshape(side,side)*struct.unpack_from('<f',blob,scale[1])[0]
                terrain=(asset,height)
            except (IndexError,struct.error,ValueError,TypeError):
                continue
    if terrain is None:raise ValueError(f"No height data in {source}")
    return layers,palette,*terrain

def convert(source,destination):
    name=source.stem
    b=source.read_bytes()
    _,version,tiles,archive,nv,nt=struct.unpack_from('<4s5I',b)
    assert version==4 and tiles==1 and len(b)==40+16*nv+12*nt+4*513**2
    ox,oz,sx,sz=struct.unpack_from('<4f',b,24)
    native=game/'archive/field/terrain'/(name+'.gfpak')
    assert native.stat().st_size==archive
    layers,palette,asset,height=terrain_properties(native)
    side=len(height)
    step=(side-1)//512
    dz,dx=np.gradient(height.astype('float64'),sz/step,sx/step)
    normal=np.stack((-dx[::step,::step],np.ones((513,513)),-dz[::step,::step]),axis=2)
    normal/=np.linalg.norm(normal,axis=2,keepdims=True)
    # Encode the upper hemisphere using octahedral X/Z. Upward Y is reconstructed.
    octa=normal[:,:,[0,2]]/np.sum(np.abs(normal),axis=2,keepdims=True)
    pack=np.zeros((513,513,4),dtype='u1')
    pack[:,:,:2]=np.rint(255*(octa*.5+.5))
    decoded=2*pack[:,:,:2].astype('float64')/255-1
    full=np.stack((decoded[:,:,0],np.maximum(0,1-np.abs(decoded).sum(2)),decoded[:,:,1]),axis=2)
    full/=np.linalg.norm(full,axis=2,keepdims=True)
    error=np.rad2deg(np.arccos(np.clip((full*normal).sum(2),-1,1)))
    assert error.max()<1.0
    known=np.zeros((513,513),'f8');total=known.copy();weighted=known.copy()
    weighted_color=np.zeros((513,513,3),'f8');color_weight=known.copy()
    splats=asset.tables(asset.root(),8)
    assert set(layers)==set(range(len(splats))), (name,layers.keys(),len(splats))
    for slot,layer in enumerate(splats):
        off,n=asset.vec(layer,1);dim=round(n**.5)
        if dim<2 or dim**2!=n:continue
        axis=np.minimum(dim-1,np.rint(np.linspace(0,dim,513)).astype(int))
        weight=np.frombuffer(asset.b,'u1',n,off).reshape(dim,dim)[np.ix_(axis,axis)]
        total+=weight
        roughness={r for _,r in layers[slot]}
        if len(roughness)==1:
            known+=weight;weighted+=weight*roughness.pop()
        textures={tex for tex,_ in layers[slot]}
        if len(textures)==1:
            tex=textures.pop()
            if tex in palette:
                color_weight+=weight;weighted_color+=weight[:,:,None]*palette[tex]
    pack[:,:,2]=np.rint(255*np.divide(weighted,known,out=np.ones_like(known),where=known>0))
    pack[:,:,3]=np.rint(255*known/np.maximum(total,1))
    out=bytearray(b);struct.pack_into('<I',out,4,5)
    rgb=weighted_color/np.maximum(color_weight[:,:,None],1)
    rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*np.maximum(rgb,0)**(1/2.4)-.055)
    tint=np.zeros_like(pack)
    tint[:,:,:3]=np.rint(255*np.clip(rgb,0,1))
    tint[:,:,3]=np.rint(255*color_weight/np.maximum(total,1))
    # Use the previously verified colors where this archive shares its textures with another area.
    prior=np.frombuffer(b[-513*513*4:],'u1').reshape(513,513,4)
    use_prior=tint[:,:,3]<prior[:,:,3]
    tint[use_prior]=prior[use_prior]
    out[-tint.nbytes:]=tint.tobytes()
    target=destination/source.name
    with target.open('wb') as file:
        file.write(out);pack.tofile(file)
    with target.open('rb') as file:
        file.seek(len(b));assert file.read()==pack.tobytes()
    active=pack[:,:,3]>=166
    rates=(pack[:,:,2][active].min()/255,pack[:,:,2][active].max()/255) if active.any() else None
    print(name,'resolved color texels',int((tint[:,:,3]>=166).sum()),'known roughness texels',int(active.sum()),'range',rates,'normal error max degrees',round(error.max(),3),flush=True)

if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:3])
    dst.mkdir(parents=True,exist_ok=True)
    for p in sorted(src.glob('*.land')):convert(p,dst)
    for p in src.glob('*.props'):shutil.copy2(p,dst)
