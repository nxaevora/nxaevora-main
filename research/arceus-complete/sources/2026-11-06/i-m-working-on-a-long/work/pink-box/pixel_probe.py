import sys
sys.path.pop(0)
from pathlib import Path
import json,struct,math
import numpy as np
from PIL import Image
here=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parent
rows=json.loads((here/'attachments.json').read_text())
arrays={}
for idx in [33,34,35,42,43,44,45,46,48]:
    r=rows[idx];w,h=int(r['image_w']),int(r['image_h']);b=Path(r['file']).read_bytes();fmt=int(r['vkformat'])
    if fmt in [37,43]:a=np.frombuffer(b,np.uint8).reshape(h,w,4)
    elif fmt==97:a=np.frombuffer(b,'<f2').astype(np.float32).reshape(h,w,4)
    elif fmt==129:a=(np.frombuffer(b,'<u4')&0xffffff).reshape(h,w,1)/0xffffff
    elif fmt==122:
        raw=np.frombuffer(b,'<u4').reshape(h,w);a=np.zeros((h,w,3),np.float32)
        for channel,(shift,mantbits) in enumerate([(0,6),(11,6),(22,5)]):
            v=(raw>>shift)&((1<<(mantbits+5))-1);m=v&((1<<mantbits)-1);e=(v>>mantbits)&31
            a[:,:,channel]=np.where(e==0,np.ldexp(m.astype(np.float32),-14-mantbits),np.ldexp(1+m.astype(np.float32)/(1<<mantbits),e.astype(np.int32)-15))
            a[:,:,channel]=np.where(e==31,np.where(m==0,np.inf,np.nan),a[:,:,channel])
    arrays[idx]=a
    if idx in [33,34,35,42,43,48]:
        rgb=a[:,:,:3];rgb=np.repeat(rgb,3,axis=2) if rgb.shape[2]==1 else rgb
        # Raw data previews use a simple display transfer only; retain original arrays for conclusions.
        rgb=np.clip(rgb/255 if idx==43 else rgb,0,1)
        if idx not in [43]:rgb=rgb**(1/2.2)
        im=Image.fromarray((rgb*255).astype(np.uint8));im=im.resize((2560,1440))
        im.crop((580,650,1990,900)).save(here/f'crop-{idx}.png')
scene=arrays[48];region=scene[720:820,700:1800];rat=region[:,:,2]/np.maximum(region[:,:,1],1e-6)
order=np.argsort(rat.ravel());ys,xs=np.unravel_index(order,rat.shape)
sample=[]
for pos in [0,len(xs)//4,len(xs)//2,3*len(xs)//4,len(xs)-20,len(xs)-1]:
    x,y=int(xs[pos])+700,int(ys[pos])+720
    sample.append({'x':x,'y':y,'blue_green_ratio':float(rat[y-720,x-700]),'values':{str(i):a[y if a.shape[0]==1440 else y//2,x if a.shape[1]==2560 else x//2].tolist() for i,a in arrays.items()}})
(here/'pixel-samples.json').write_text(json.dumps(sample,indent=2))
print(json.dumps(sample,indent=2))
print('NaN/inf scene',int(np.count_nonzero(~np.isfinite(scene))))
