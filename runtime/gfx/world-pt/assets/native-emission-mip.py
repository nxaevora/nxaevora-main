from pathlib import Path
import hashlib,runpy,shutil,struct,sys
import numpy as np
from PIL import Image
from texture import texture,entries,here,game
from models import Asset
decode=runpy.run_path(str(here/'terrain-uv.py'))['native_mip']

def linear_image(data,level):
    img=np.asarray(Image.fromarray(decode(data,level)).convert('RGB'),dtype=float)/255.
    return np.where(img<=.04045,img/12.92,((img+.055)/1.055)**2.4)

def convert(src,dst):
    b=src.read_bytes()
    magic,v,n,m,g,arc,first,maps,banks,tri=struct.unpack_from('<4s9I',b)
    assert magic==b'PROP' and v==7
    at=40+52*n+32768*m+64*g+24*n+32772*maps+16*g
    bankbytes=128*128*8
    targets=[hashlib.sha256(b[at+i*bankbytes:at+(i+1)*bankbytes]).digest() for i in range(banks)]
    assert len(b)==at+banks*bankbytes+tri*8+4
    pack=game/'archive/field/model/pack'/(src.stem+'.gfpak')
    textures={};names=set();found={}
    for i,_,blob in entries(pack):
        if blob[:4]==b'BNTX':
            textures[texture(blob)[0]]=blob
        elif b'EmissionColorMap' in blob:
            asset=Asset(blob)
            for mat in asset.tables(asset.root(),1):
                for t in asset.tables(mat,2):
                    if asset.string(t,0)=='EmissionColorMap':names.add(Path(asset.string(t,1)).stem)
    for name in sorted(names):
        if name not in textures:continue
        blob=textures[name];_,fmt,w,h,mips=texture(blob);level=0
        if fmt&255!=6:continue
        while max(w,h)>>(level+1)>=128 and level+1<len(mips):level+=1
        mip=linear_image(blob,level)
        coarse=np.zeros((128,128,4),'<f2')
        for j in range(3):
            coarse[:,:,j]=np.asarray(Image.fromarray(mip[:,:,j].astype('f4')).resize((128,128),Image.Resampling.BILINEAR))
        key=hashlib.sha256(coarse.tobytes()).digest()
        if key not in targets:continue
        native=linear_image(blob,min(1,len(mips)-1));height,width=native.shape[:2]
        rgba=np.zeros((height,width,4),'<f2');rgba[:,:,:3]=native
        record=width,height,rgba.tobytes()
        if key in found and record!=found[key]:raise ValueError(('ambiguous emission texture',src,name))
        found[key]=record;print(src.stem,name,width,height,flush=True)
    if set(targets)!=set(found):raise ValueError(('unmatched emissive bank',src))
    cursor=4*banks;records=[];pixels=[]
    for key in targets:
        w,h,data=found[key];records.append(struct.pack('<4I',cursor,w,h,0));pixels.append(data);cursor+=len(data)//4
    with dst.open('wb') as f:
        f.write(struct.pack('<4s10I',magic,8,n,m,g,arc,first,maps,banks,tri,cursor))
        f.write(b[40:]);f.writelines(records);f.writelines(pixels)

if __name__=='__main__':
    source,dest=map(Path,sys.argv[1:]);dest.mkdir(exist_ok=True)
    for p in source.iterdir():
        if p.suffix=='.props':convert(p,dest/p.name)
        elif p.is_file():shutil.copy2(p,dest/p.name)
