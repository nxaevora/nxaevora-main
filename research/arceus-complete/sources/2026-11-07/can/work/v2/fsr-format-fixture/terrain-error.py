from pathlib import Path
import sys, struct
import numpy as np
sys.path.insert(0,str(Path("work/assets").resolve()))
from fb import FB,entries,game

def source_height(path):
    for _,_,blob in entries(path):
        if len(blob)<4096 or blob[:4] in (b"BNTX",b"GFLX") or blob[4:8]==b"TAG0": continue
        f=FB(blob)
        try:
            t=f.fields(f.u32(0))
            w,h=(f.u32(t[j]) for j in (3,4))
            if w!=h or w not in (513,1025,2049,4097): continue
            off=f.ref(t[5])
            if f.u32(off)!=w*h: continue
            def v3(i):
                fields=f.fields(f.ref(t[i]))
                return [struct.unpack_from("<f",blob,p)[0] if p else 0. for p in (fields+[None]*3)[:3]]
            scales=v3(1); origin=v3(0)
            hgt=np.frombuffer(blob,"<f4",w*h,off+4).reshape(h,w)
            return origin[1]+hgt*scales[1]
        except (ValueError,IndexError,TypeError,struct.error): continue
    raise ValueError(path)

for archive in sorted((game/"archive/field/terrain").glob("ha_area??.gfpak")):
    h=source_height(archive)
    dst=Path("work/v2/pbr-terrain")/(archive.stem+".land")
    coarse=np.fromfile(dst,"<f4",513*513,offset=40).reshape(513,513)
    step=(h.shape[0]-1)//512
    assert np.max(np.abs(coarse-h[::step,::step]))<1e-4
    coords=np.arange(h.shape[0])/step
    i=np.minimum(511,coords.astype(int)); frac=coords-i
    errors=[]
    for z in range(len(i)):
        a,b=coarse[i[z],i],coarse[i[z],i+1]
        c,d=coarse[i[z]+1,i],coarse[i[z]+1,i+1]
        u,v=frac,frac[z]
        predicted=np.where(u+v<=1,a+u*(b-a)+v*(c-a),d+(1-u)*(c-d)+(1-v)*(b-d))
        errors.append(np.abs(predicted-h[z]).astype("float32"))
    q=np.percentile(np.stack(errors),[95,99,100])
    print(f"{archive.stem} {h.shape[0]} native samples/side; coarse height error p95={q[0]:.3f} p99={q[1]:.3f} max={q[2]:.3f}")
