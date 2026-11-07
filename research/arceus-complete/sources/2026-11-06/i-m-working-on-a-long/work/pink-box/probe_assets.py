import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
import numpy as np
from PIL import Image
here=Path(__file__).resolve().parent
root=Path('C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/system_resource/gfx2/texture')
results=[]
for p in root.glob('*prb.bntx'):
 b=p.read_bytes();u=lambda o:struct.unpack_from('<I',b,o)[0];q=lambda o:struct.unpack_from('<Q',b,o)[0];h16=lambda o:struct.unpack_from('<H',b,o)[0]
 t=q(q(0x28));w,h=u(t+0x24),u(t+0x28);fmt=u(t+0x1c);layers=u(t+0x30);size=u(t+0x50);bh=u(t+0x34)&7;off=q(q(t+0x70));levels=h16(t+0x16)
 assert fmt==0x1505 and layers==6
 stride=size//layers
 gx=(w*8+63)//64
 faces=[]
 for face in range(6):
  linear=bytearray(w*h*8)
  for y in range(h):
   for x in range(w):
    xb=x*8;addr=(y//(8<<bh))*gx*(512<<bh)+(xb//64)*(512<<bh)+((y%(8<<bh))//8)*512+((xb&32)<<3)+((xb&16)<<1)+(xb&15)+(((y&7)>>1)<<6)+((y&1)<<4)
    linear[(y*w+x)*8:(y*w+x+1)*8]=b[off+face*stride+addr:off+face*stride+addr+8]
  a=np.frombuffer(linear,'<f2').astype(np.float32).reshape(h,w,4)
  faces.append(a)
 allface=np.concatenate(faces,axis=1)
 np.save(here/(p.stem+'.npy'),np.stack(faces))
 Image.fromarray((np.clip(allface[:,:,:3],0,1)**(1/2.2)*255).astype(np.uint8)).resize((w*6*2,h*2)).save(here/(p.stem+'.png'))
 results.append({'asset':str(p),'sha256':hashlib.sha256(b).hexdigest(),'format':hex(fmt),'shape':[w,h,layers],'levels':levels,'face_stride':stride,'finite':bool(np.isfinite(allface).all()),'min':allface.min(axis=(0,1)).tolist(),'max':allface.max(axis=(0,1)).tolist(),'mean':allface.mean(axis=(0,1)).tolist(),'max_blue_green_ratio':float((allface[:,:,2]/np.maximum(allface[:,:,1],1e-7)).max())})
(here/'default-probes.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
