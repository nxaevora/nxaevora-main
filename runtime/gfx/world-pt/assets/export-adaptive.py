"""Build native-height meshes and verify their topology and height error before writing each tile."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import numpy as np
from fb import FB, entries, game

def source_height(path):
    for _,_,blob in entries(path):
        if len(blob)<4096 or blob[:4] in (b"BNTX",b"GFLX") or blob[4:8]==b"TAG0":
            continue
        f=FB(blob)
        try:
            t=f.fields(f.u32(0))
            w,h=(f.u32(t[j]) for j in (3,4))
            if w!=h or w not in (513,1025,2049,4097):
                continue
            off=f.ref(t[5])
            if f.u32(off)!=w*h: continue
            def v3(i):
                fields=f.fields(f.ref(t[i]))
                return [struct.unpack_from("<f",blob,p)[0] if p else 0. for p in (fields+[None]*3)[:3]]
            scales=v3(1); origin=v3(0)
            samples=np.frombuffer(blob,"<f4",w*h,off+4).reshape(h,w)
            return origin[1]+samples*scales[1]
        except (ValueError,IndexError,TypeError,struct.error):
            continue
    raise ValueError(f"Missing native height grid in {path}")

def interpolate(grid, side):
    # Same diagonal convention as the retained regular-grid BLAS.
    axis=np.linspace(0,len(grid)-1,side)
    i=np.minimum(len(grid)-2,axis.astype(int))
    u=axis-i
    result=np.empty((side,side),np.float32)
    for z,v in enumerate(u):
        a,b=grid[i[z],i],grid[i[z],i+1]
        c,d=grid[i[z]+1,i],grid[i[z]+1,i+1]
        result[z]=np.where(u+v<=1,a+u*(b-a)+v*(c-a),d+(1-u)*(c-d)+(1-v)*(b-d))
    return result

def verify_topology(vertices, triangles, side):
    points=np.stack((vertices%side,vertices//side),axis=1).astype(np.int64)
    a,b,c=points[triangles].transpose(1,0,2)
    twice_area=(b[:,0]-a[:,0])*(c[:,1]-a[:,1])-(b[:,1]-a[:,1])*(c[:,0]-a[:,0])
    assert np.all(twice_area<0) and -twice_area.sum()==2*(side-1)**2
    edges=np.sort(np.concatenate([triangles[:,[0,1]],triangles[:,[1,2]],triangles[:,[2,0]]]),axis=1)
    keys=(edges[:,0].astype("uint64")<<32)|edges[:,1]
    unique,counts=np.unique(keys,return_counts=True)
    assert counts.max()<=2 and len(vertices)-len(unique)+len(triangles)==1
    boundary=unique[counts==1]
    p,q=points[boundary>>32],points[boundary&0xffffffff]
    assert np.all(np.any(((p==0)&(q==0))|((p==side-1)&(q==side-1)),axis=1))
    return points

def convert(path,src,dst,tool,temp,vertex_colors):
    blob=(src/(path.stem+".land")).read_bytes()
    magic,version,tiles,archive,w,h,ox,oz,sx,sz=struct.unpack_from("<4sIIIII4f",blob)
    assert (magic,version,tiles,w,h)==(b"LAND",2,1,513,513) and archive==path.stat().st_size
    full=source_height(path)
    side=len(full);step=(side-1)//512
    coarse=np.frombuffer(blob,"<f4",w*h,40).reshape(h,w)
    assert np.isfinite(full).all() and np.max(np.abs(coarse-full[::step,::step]))<1e-4
    rgba=np.frombuffer(blob,np.uint8,offset=40+w*h*4).reshape(h,w,4)
    colors=np.stack([np.rint(interpolate(channel.astype(np.float32),side)).astype(np.uint8)
                     for channel in rgba.transpose(2,0,1)],axis=2)
    native=temp/"native.bin"; mesh=temp/"mesh.bin"
    with native.open("wb") as f: full.astype("<f4").tofile(f);colors.tofile(f)
    subprocess.run([tool,str(side),native,mesh,str(int(vertex_colors))],check=True)
    with mesh.open("rb") as f:
        nv,nt=struct.unpack("<II",f.read(8))
        assert 2<=nt<=524288 and 4<=nv<=524290
        vertices=np.fromfile(f,"<u4",nv)
        triangles=np.fromfile(f,"<u4",nt*3).reshape(nt,3)
        fit=np.fromfile(f,"<f4",side*side).reshape(side,side)
        color_error=np.fromfile(f,"<f4",side*side)
        assert len(color_error)==side*side and np.isfinite(color_error).all()
        assert not f.read(1) and np.isfinite(fit).all()
    points=verify_topology(vertices,triangles,side)
    base_error=np.percentile(np.abs(interpolate(coarse,side)-full),[95,99,100])
    error=np.percentile(np.abs(fit-full),[95,99,100])
    if error[1]>base_error[1]+.02 or error[2]>base_error[2]+.02:
        raise ValueError(f"Height regression in {path.stem}: {base_error} -> {error}")
    out=np.empty((nv,4),"<f4")
    out[:,0]=ox+points[:,0]*(sx/step);out[:,1]=full.ravel()[vertices];out[:,2]=oz+points[:,1]*(sz/step)
    out[:,3].view("<u4")[:]=colors.reshape(-1,4).view("<u4").ravel()[vertices]
    with (dst/(path.stem+".land")).open("wb") as f:
        f.write(struct.pack("<4sIIIII",b"LAND",3 if vertex_colors else 4,1,archive,nv,nt))
        if not vertex_colors: f.write(struct.pack("<4f",ox,oz,sx,sz))
        out.tofile(f);triangles.tofile(f)
        if not vertex_colors: rgba.tofile(f)
    if not vertex_colors:
        with (dst/(path.stem+".land")).open("rb") as tile:
            tile.seek(40+16*nv+12*nt)
            assert tile.read()==rgba.tobytes()
        material_coords=(out[:,[0,2]].astype("float64")-[ox,oz])/[sx,sz]
        assert np.max(np.abs(material_coords-points/step))<.001
    print(path.stem,"p95/p99/max",base_error,"->",error,"color error in 8-bit levels",np.percentile(color_error,[95,99,100]) if vertex_colors else "source texels verified",flush=True)

if __name__=="__main__":
    source,destination,tool=map(Path,sys.argv[1:4])
    destination.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as scratch:
        for path in sorted((game/"archive/field/terrain").glob("ha_area??.gfpak")):
            convert(path,source,destination,tool.resolve(),Path(scratch),"--vertex-colors" in sys.argv[4:])
    for prop in source.glob("*.props"): shutil.copy2(prop,destination)
