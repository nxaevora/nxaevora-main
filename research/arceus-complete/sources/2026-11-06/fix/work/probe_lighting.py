from pathlib import Path
import csv,json,struct
import numpy as np
cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures');prefix='screenshot_20261106_190711_buffers_1'
rows=list(csv.DictReader((s for s in (cap/(prefix+'_attachments.tsv')).read_text().splitlines() if not s.startswith('#')),delimiter='\t'))
def load(addr):
 r=next(r for r in rows if r['gva']==addr);w,h=int(r['image_w']),int(r['image_h']);fmt=int(r['vkformat']);raw=Path(r['file']).read_bytes()
 if fmt in [37,43]:return np.frombuffer(raw,'u1').reshape(h,w,4)
 if fmt==97:return np.frombuffer(raw,'<f2').astype('f4').reshape(h,w,4)
 if fmt==122:
  v=np.frombuffer(raw,'<u4').reshape(h,w);a=[]
  for shift,m in [(0,6),(11,6),(22,5)]:
   bits=(v>>shift)&((1<<(m+5))-1);mant=bits&((1<<m)-1);exp=bits>>m
   a.append(np.where(exp==0,np.ldexp(mant.astype('f4'),-14-m),np.ldexp(1+mant.astype('f4')/(1<<m),exp.astype('i4')-15)))
  return np.stack(a,axis=-1)
 raise ValueError(fmt)
if __name__=='__main__':
 scene=load('5508c0000');roi=scene[735:820,700:1800];mask=(roi[:,:,0]>roi[:,:,1]*1.6)&(roi[:,:,2]>roi[:,:,1]*1.6)&(roi[:,:,1]>.03)
 ys,xs=np.where(mask);results=[]
 cb=(cap/(prefix+'_light_0_cb16.bin')).read_bytes()
 for k in [0,len(xs)//4,len(xs)//2,3*len(xs)//4,len(xs)-1]:
  x,y=int(xs[k])+700,int(ys[k])+735
  vals={addr:load(addr)[y,x].tolist() for addr in ['5508c0000','552a80000','5519a0000','552210000']}
  # ISCADD word200: R25=(R0<<8)+R1; R0,R1 are material blue/alpha bytes.
  mat=vals['5519a0000'];idx=(mat[2]<<8)+mat[3];vals['LUT_index']=idx;vals['LUT_bytes']=cb[idx*16:idx*16+16].hex();vals['x']=x;vals['y']=y;results.append(vals)
 Path('work/lighting-pixels.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
