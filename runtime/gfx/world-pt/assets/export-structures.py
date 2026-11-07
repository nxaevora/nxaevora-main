"""Build gated native structure groups from verified rigid placements and archive materials."""
import collections,shutil,struct,sys,runpy
from pathlib import Path
import numpy as np
from PIL import Image
from texture import texture,entries,here,game
placed=runpy.run_path(str(here/'placed-structures.py'))
decode=runpy.run_path(str(here/'terrain-uv.py'))['native_mip']
def original(path):
    blob=path.read_bytes();magic,ver,nt,nm,ng,archive=struct.unpack_from('<4s5I',blob)
    assert magic==b'PROP' and ver==2
    v=np.frombuffer(blob,'<f4',12*nt,24).reshape(-1,4)
    tags=np.frombuffer(blob,'<u4',nt,24+48*nt)
    start=24+52*nt;size=16384*nm
    palette=[(blob[start+16384*i:start+16384*(i+1)],blob[start+size+16384*i:start+size+16384*(i+1)]) for i in range(nm)]
    checks=blob[start+2*size:];groups=[];i=0
    for g in range(ng):
        n=struct.unpack_from('<I',checks,64*g)[0];tag=int(tags[i])
        assert np.all(tags[i:i+n]==tag)
        groups.append((v[3*i:3*(i+n)],tag&255,(tag>>8)&255,0));i+=n
    assert i==nt
    return palette,groups
def export(area,src,dst,paths=None):
    archive=(game/'archive/field/terrain'/(area+'.gfpak')).stat().st_size
    pack=game/'archive/field/model/pack'/(area+'.gfpak')
    images={texture(b)[0]:b for i,_,b in entries(pack) if b[:4]==b'BNTX'}
    cache={}
    def tex(name):
        if name in cache:return cache[name]
        b=images[name];_,fmt,w,h,mips=texture(b);level=0
        while max(w,h)>>(level+1)>=64 and level+1<len(mips):level+=1
        out=np.asarray(Image.fromarray(decode(b,level)).convert('RGBA').resize((64,64),Image.Resampling.BILINEAR))
        cache[name]=out;return out
    def material(mat,t):
        binds={mat.string(o,0):Path(mat.string(o,1)).stem for o in mat.tables(t,2)}
        scalar={mat.string(o,0):struct.unpack_from('<f',mat.b,mat.field(o,1))[0] if mat.field(o,1) else 0. for o in mat.tables(t,4)}
        opts={mat.string(o,0):mat.number(o,1) for o in mat.tables(t,9)}
        vec={mat.string(o,0):struct.unpack_from('<4f',mat.b,mat.field(o,1)) for o in mat.tables(t,7) if mat.field(o,1)}
        shaders=[mat.string(o,0) for o in mat.tables(t,1)]
        if shaders!=['Standard'] or (scalar.get('EmissionIntensity',0)!=0 and '--emitters' not in sys.argv) or (area=='ha_area00' and opts.get('CastShadow')!=1) or 'WindMaskMap' in binds:return
        if area.endswith('_s01') and opts.get('CategoryLabel')!=3:return
        if vec.get('UVScaleOffset',(1,1,0,0))!=(1,1,0,0):return
        if 'BaseColorMap' not in binds or not set(binds)<=set(('BaseColorMap','NormalMap','MetallicMap','RoughnessMap','PackedMap','EmissionColorMap','AOMap')):return
        try:
            color=tex(binds['BaseColorMap'])
            props=np.zeros_like(color);props[:,:,3]=255
            if 'PackedMap' in binds:props[:,:,:2]=tex(binds['PackedMap'])[:,:,1:3]
            else:
                for j,label in enumerate(('Roughness','Metallic')):
                    if label+'Map' in binds:props[:,:,j]=tex(binds[label+'Map'])[:,:,0]
                    elif 0<=scalar.get(label,-1)<=1:props[:,:,j]=round(scalar[label]*255)
                    else:return
        except (KeyError,ValueError):return
        strength=scalar.get('EmissionIntensity',0.)
        emitter=None
        if strength:
            name=binds.get('EmissionColorMap')
            if not name or name not in images or not 0<strength<=64:return
            image=images[name];_,fmt,_,_,_=texture(image)
            if fmt&255!=6:return
            emitter=(name,strength)
        return color.tobytes(),props.tobytes(),round(scalar.get('DiscardValue',0)*255),emitter
    palette,legacy=original(src/(area+'.props')) if (src/(area+'.props')).exists() else ([],[])
    bymat={v:i for i,v in enumerate(palette)};native=[];classes=0;predicted=0
    emission={}
    matches=placed['recover'](area,paths)
    for model,parts,mat,tables in placed['catalog'](area):
        if model not in matches:continue
        profiles=[material(mat,tables[name]) for _,name,_,_ in parts]
        for inst in matches[model]:
            if not any(profiles[k] for k in inst['textures']):continue
            classes+=1;tr=inst['tr']
            for k,(_,label,pos,uv) in enumerate(parts):
                if k not in inst['textures'] or not profiles[k] or len(pos)<12 or len(pos)%3:continue
                c,pr,cut,source=profiles[k];known=inst['cutoffs'][k]
                if len(known)>1:continue
                if known:cut=next(iter(known))
                mid=bymat.setdefault((c,pr),len(palette))
                if mid==len(palette):palette.append((c,pr))
                v=np.empty((len(pos),4),'<f4');v[:,:3]=pos@tr[:3]+tr[3]
                st=uv.copy();st[:,1]=1-st[:,1];v[:,3]=st.astype('<f2').view('<f4').ravel()
                ix=np.linspace(0,len(v)-1,21,dtype=int)
                if any(len(old)==len(v) and np.max(abs(old[ix,:3]-v[ix,:3]))<.002 and
                       np.max(abs(old[:,:3]-v[:,:3]))<.004 and np.array_equal(old[:,3].view('<u4'),v[:,3].view('<u4'))
                       for old,_,_,_ in native):continue
                if source:emission[id(v)]=source
                native.append((v,mid,cut,classes))
                if k not in inst['parts']:predicted+=1
    kept=[]
    for group in legacy:
        p=group[0][:,:3];ix=np.linspace(0,len(p)-1,47,dtype=int)
        if any(len(v)==len(p) and np.max(abs(v[ix,:3]-p[ix]))<.006 and np.max(abs(v[:,:3]-p))<.01 for v,_,_,_ in native):continue
        kept.append(group)
    groups=kept+native;nt=sum(len(v)//3 for v,m,cut,c in groups)
    assert groups and classes and nt<=1000000 and len(palette)<=255 and len(groups)<=1024,(nt,len(groups),len(palette))
    tags=[];checks=[];start=0
    for i,(v,m,cut,c) in enumerate(groups):
        xyz=v[:,:3];n=len(v)//3
        a=int(np.argmax(np.linalg.norm(xyz-xyz.mean(0),axis=1)))
        b=int(np.argmax(np.linalg.norm(xyz-xyz[a],axis=1)))
        d=int(np.argmax(np.linalg.norm(np.cross(xyz-xyz[a],xyz[b]-xyz[a]),axis=1)))
        w=(0.,float(start+1),0.)
        checks.append(struct.pack('<4I',n,a,b,d)+b''.join(struct.pack('<4f',*xyz[k],x) for k,x in zip([a,b,d],w)))
        tags.append(np.full(n,m|(cut<<8)|(i<<16),'<u4').tobytes());start+=len(v)
    with (dst/(area+'.props')).open('wb') as f:
        f.write(struct.pack('<4s5I',b'PROP',2,nt,len(palette),len(groups),archive))
        for v,_,_,_ in groups:f.write(v.tobytes())
        f.writelines(tags);f.writelines(x[0] for x in palette);f.writelines(x[1] for x in palette);f.writelines(checks)
    if emission:
        imagesOut=[];bank={};table=bytearray()
        for v,_,_,_ in groups:
            source=emission.get(id(v))
            if not source:
                table+=bytes(16);continue
            name,strength=source
            if name not in bank:
                image=images[name];_,fmt,w,h,mips=texture(image);level=0
                while max(w,h)>>(level+1)>=128 and level+1<len(mips):level+=1
                rgb=np.asarray(Image.fromarray(decode(image,level)).convert('RGB'),dtype=float)/255.
                linear=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
                rgba=np.zeros((128,128,4),'<f2')
                for j in range(3):
                    rgba[:,:,j]=np.asarray(Image.fromarray(linear[:,:,j].astype('f4')).resize((128,128),Image.Resampling.BILINEAR))
                bank[name]=len(imagesOut)+1;imagesOut.append(rgba.tobytes())
            table+=struct.pack('<IfII',bank[name],strength,0,0)
        (dst/(area+'.lighting')).write_bytes(struct.pack('<I',len(bank))+table+b''.join(imagesOut))
        print(area,'emissive groups',len(emission),'maps',len(bank),flush=True)
    print(area,'triangles',nt,'materials',len(palette),'groups',len(groups),'legacy',len(kept),'placements',classes,'inferred sibling groups',predicted,flush=True)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:3]);dst.mkdir(exist_ok=True)
    for f in src.iterdir():
        if f.is_file():shutil.copy2(f,dst/f.name)
    export('ha_area00',src,dst)
    if len(sys.argv)>3:export('ha_area00_s01',src,dst,Path(sys.argv[3]).glob('room-*.bin'))
