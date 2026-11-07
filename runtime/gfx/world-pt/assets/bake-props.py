from texture import texture,entries,game,here
from PIL import Image
import numpy as np,runpy,struct
read=runpy.run_path(str(here/'static-scenes.py'))['read']
groups=[]
for f in sorted((here/'static').glob('*.bin')):
    camera=np.fromfile(f,'<f4',20)[16:19]
    for name,cutoff,p,uv,rough,metal in read(f,True):
        if not (name.startswith('objects_area00_house') or name.startswith('objects_area00_fence') or name.startswith('ground_rock')):continue
        if len(p)<24 or not np.isfinite(p).all() or not np.isfinite(uv).all():continue
        found=None
        for g in groups:
            if g['name']==name and g['cut']==cutoff and g['p'].shape==p.shape and np.max(np.abs(g['p'][::79]-p[::79]))<.02:
                found=g;break
        if found is None:
            groups.append(dict(name=name,cut=cutoff,p=p.copy(),uv=uv.copy(),camera=camera,verified=False,properties=set()));found=groups[-1]
        if rough:found['properties'].add((rough,metal))
        if np.linalg.norm(found['camera']-camera)>1 and np.max(np.abs(found['p']-p))<.025 and np.max(np.abs(found['uv']-uv))<.002:
            found['verified']=True
groups=[g for g in groups if g['verified']]
for g in groups:
    pair=next(iter(g['properties'])) if len(g['properties'])==1 else ('','')
    g['material']=(g['name'],*pair)
materials=sorted(set(g['material'] for g in groups))
names={name for material in materials for name in material if name}
colors={}
for i,k,b in entries(game/'archive/field/model/pack/ha_area00.gfpak'):
    if b[:4]!=b'BNTX':continue
    n,fmt,w,h,mips=texture(b)
    if n not in names:continue
    level=0
    while max(w,h)>64 and level+1<len(mips):
        w=max(1,w//2);h=max(1,h//2);level+=1
    kind={26:1,27:2,28:3,29:4,32:7}[fmt>>8]
    block=8 if kind in (1,4) else 16
    y,x=np.indices(((h+3)//4,((w+3)//4)*block))
    # The GOB height shrinks with each mip level. A 64x64 BC texture has up to two GOB rows.
    brti=struct.unpack_from('<Q',b,struct.unpack_from('<Q',b,0x28)[0])[0]
    bh=struct.unpack_from('<I',b,brti+0x34)[0]&7
    while bh and (8<<(bh-1))>=y.shape[0]:bh-=1
    offset=((y>>(3+bh))*((x.shape[1]+63)//64)+(x>>6))*(512<<bh)+(((y>>3)&((1<<bh)-1))*512)+((x&32)<<3)+((y&6)<<5)+((x&16)<<1)+((y&1)<<4)+(x&15)
    linear=np.frombuffer(b,'u1')[mips[level]+offset].tobytes()
    im=Image.frombytes('L' if kind==4 else 'RGBA',(w,h),linear,'bcn',(kind,'BC'+str(kind)))
    colors[n]=im.convert('RGBA').resize((64,64),Image.Resampling.BILINEAR)
assert all(name in colors for name in names)
verts=[];tags=[];checks=[]
for order,g in enumerate(groups):
    p=g['p'];uv=g['uv'];n=len(p)//3;ids=np.array([0,len(p)//2,len(p)-1])
    # Full 3-D positions at three distant vertices identify each placed draw on a new visit.
    p0=np.argmax(np.linalg.norm(p-p.mean(0),axis=1))
    p1=np.argmax(np.linalg.norm(p-p[p0],axis=1))
    p2=np.argmax(np.linalg.norm(np.cross(p-p[p0],p[p1]-p[p0]),axis=1))
    ids=[p0,p1,p2]
    checks.append(struct.pack('<4I',n,*ids)+b''.join(struct.pack('<4f',*p[j],0) for j in ids))
    buf=np.empty((len(p),4),'<f4');buf[:,:3]=p;buf[:,3]=uv.astype('<f2').view('<f4').ravel();verts.append(buf.tobytes())
    tags.append(np.full(n,materials.index(g['material'])|(g['cut']<<8)|(order<<16),'<u4').tobytes())
count=sum(len(v)//48 for v in verts)
properties=[]
for albedo,rough,metal in materials:
    image=np.zeros((64,64,4),dtype=np.uint8)
    if rough:
        combined=rough==metal
        image[:,:,0]=np.asarray(colors[rough])[:,:,1 if combined else 0]
        if metal:image[:,:,1]=np.asarray(colors[metal])[:,:,2 if combined else 0]
        image[:,:,3]=255
    properties.append(image.tobytes())
head=struct.pack('<4s5I',b'PROP',2,count,len(materials),len(groups),(game/'archive/field/terrain/ha_area00.gfpak').stat().st_size)
out=here/'static/ha_area00.props';out.write_bytes(head+b''.join(verts)+b''.join(tags)+b''.join(colors[a].tobytes() for a,_,_ in materials)+b''.join(properties)+b''.join(checks))
print('groups',len(groups),'triangles',count,'materials',len(materials),'with measured roughness',sum(bool(r) for _,r,m in materials),'bytes',out.stat().st_size)
