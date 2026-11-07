import sys
sys.path.pop(0)
from pathlib import Path
import csv,io,json,hashlib
import numpy as np
here=Path(__file__).resolve().parent
captures=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
def packed_rgb(raw):
 out=np.empty((*raw.shape,3),np.float32)
 for i,(shift,m) in enumerate([(0,6),(11,6),(22,5)]):
  v=(raw>>shift)&((1<<(m+5))-1);mant=v&((1<<m)-1);exp=(v>>m)&31
  a=np.where(exp==0,np.ldexp(mant.astype(np.float32),-14-m),np.ldexp(1+mant.astype(np.float32)/(1<<m),exp.astype(np.int32)-15))
  out[:,:,i]=np.where(exp==31,np.where(mant==0,np.inf,np.nan),a)
 return out
report=[]
for mode,prefix in [('ON','screenshot_20261106_181800_buffers_4'),('OFF','screenshot_20261106_184316_buffers_1')]:
 p=captures/(prefix+'_attachments.tsv');text=p.read_text();rows=list(csv.DictReader(io.StringIO('\n'.join(l for l in text.splitlines() if not l.startswith('#'))),delimiter='\t'))
 def read(gva,fmt):
  r=next(r for r in rows if r['gva']==gva and r['is_rt']=='1');assert int(r['vkformat'])==fmt
  w,h=int(r['image_w']),int(r['image_h']);b=Path(r['file']).read_bytes()
  return np.frombuffer(b,np.uint8).reshape(h,w,4) if fmt in [37,43] else packed_rgb(np.frombuffer(b,'<u4').reshape(h,w))
 base=read('552a80000',43);mat=read('5519a0000',37);normal=read('552210000',37);light=read('5508c0000',122)
 # Same approximate tabletop ROI, not pixel-registered across camera animations.
 sel=np.zeros(base.shape[:2],bool);sel[720:820,700:1800]=True;sel &= base[:,:,3]==70
 purple=(light[:,:,2]>1.5*light[:,:,1])&(light[:,:,0]>1.5*light[:,:,1])&(light[:,:,1]>.001)
 groups={}
 for name,mask in [('all',sel),('purple',sel&purple),('nonpurple',sel&~purple),('material_green_0_to_15',sel&(mat[:,:,1]<=15)),('material_green_200_to_255',sel&(mat[:,:,1]>=200))]:
  n=int(mask.sum());groups[name]={'pixels':n,'purple_pixels':int((mask&purple).sum()),'purple_fraction':float(purple[mask].mean()) if n else None,
    'mean_light_rgb':light[mask].mean(axis=0).tolist() if n else None,
    'material_rg_mean':mat[mask,:2].mean(axis=0).tolist() if n else None,
    'normal_alpha_quantiles':np.quantile(normal[:,:,3][mask],[0,.25,.5,.75,1]).tolist() if n else None}
 report.append({'mode':mode,'capture':prefix,'manifest_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'groups':groups,
 'shadow_producers':[{'address':r['gva'],'format':r['vkformat'],'dimensions':[int(r['image_w']),int(r['image_h'])]} for r in rows if r['is_rt']=='1' and r['gva'] in ['54a8a0000','54aca0000','53aca0000','53ada0000']]})
result={'scope':'Approximate tabletop ROI; basecolor-alpha70 mask; not pixel registration or shader causality proof. Purple classifier R/G>1.5 and B/G>1.5, G>.001 in raw linear scene lighting. Material-green interpretation is inferred separately from shader arithmetic.','results':report}
(here/'shadow-comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
