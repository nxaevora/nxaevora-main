from fb import *
import numpy as np,hashlib
destination=Path(os.environ['ARCEUS_TERRAIN_DIR'])
destination.mkdir(parents=True,exist_ok=True)
def vector3(f,root,idx):
    slots=f.fields(f.ref(root[idx]))
    return tuple(struct.unpack_from('<f',f.b,slots[i])[0] if i<len(slots) and slots[i] else 0. for i in range(3))
def height_tiles(path):
    result=[]
    for i,t,data in entries(path):
        if len(data)<4096 or data[:4] in (b'BNTX',b'GFLX') or data[4:8]==b'TAG0':continue
        f=FB(data)
        try:
            root=f.fields(f.u32(0))
            if len(root)<10 or not all(root[j] for j in range(6)):continue
            width,height=(f.u32(root[j]) for j in (3,4))
            if width<33 or height<33 or width>4097 or height>4097:continue
            off=f.ref(root[5])
            if f.u32(off)!=width*height:continue
            a=np.frombuffer(data,'<f4',width*height,off+4).reshape(height,width)
            origin,scale,extent=(vector3(f,root,j) for j in range(3))
            if not (np.isfinite(a).all() and 0<=a.min() and a.max()<=1.01):continue
            if min(scale)<=0 or abs(scale[0]*(width-1)-extent[0])>.05 or abs(scale[2]*(height-1)-extent[2])>.05:continue
            # Coarse triangles extend rays beyond the already-submitted native terrain.
            stride=(width-1)//512;z=np.arange(0,height,stride);x=np.arange(0,width,stride)
            if x[-1]!=width-1 or z[-1]!=height-1:continue
            heights=(origin[1]+scale[1]*a[::stride,::stride]).astype('<f4')
            sampled=heights.repeat(2,0).repeat(2,1)
            result.append((origin[0],origin[2],scale[0]*stride,scale[2]*stride,heights))
            print(path.stem,i,width,height,origin,extent)
        except (ValueError,struct.error,TypeError,IndexError,OverflowError):continue
    return result
for path in sorted((game/'archive/field/terrain').glob('ha_area??.gfpak')):
    tiles=height_tiles(path)
    if not tiles:continue
    with (destination/(path.stem+'.land')).open('wb') as out:
        out.write(struct.pack('<4sIII',b'LAND',1,len(tiles),path.stat().st_size))
        for ox,oz,sx,sz,h in tiles:
            out.write(struct.pack('<II4f',h.shape[1],h.shape[0],ox,oz,sx,sz))
            out.write(h.tobytes())
