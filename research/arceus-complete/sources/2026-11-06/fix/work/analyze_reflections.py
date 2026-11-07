from pathlib import Path
import csv,subprocess,json,hashlib
import numpy as np
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parent
out=root/'decoded-reflections';out.mkdir(exist_ok=True)
cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
results=[]
for manifest in sorted(cap.glob('*1907*reflections.tsv')):
 rows=list(csv.DictReader((x for x in manifest.read_text().splitlines() if not x.startswith('#')),delimiter='\t'))
 for r in rows:
  w,h,l=map(int,(r['image_w'],r['image_h'],r['layers']));dst=out/(manifest.stem+'_'+r['gva']+'_'+r['mip']+'.half')
  subprocess.run([str(root/'decode_cube.exe'),r['file'],str(w),str(h),str(l),'8',str(dst)],check=True)
  a=np.fromfile(dst,'<f2').astype('f4').reshape(l,h,w,4);np.save(dst.with_suffix('.npy'),a)
  rgb=a[...,:3];ratio=(rgb[...,0]>1.5*rgb[...,1])&(rgb[...,2]>1.5*rgb[...,1])&(rgb[...,1]>.001)
  result={k:r[k] for k in ['gva','mip','present','image_w','levels']}
  result.update(sha256=hashlib.sha256(Path(r['file']).read_bytes()).hexdigest(),min=rgb.min((0,1,2)).tolist(),max=rgb.max((0,1,2)).tolist(),mean=rgb.mean((0,1,2)).tolist(),purple_texels=int(ratio.sum()),texels=int(ratio.size))
  results.append(result)
  if manifest==sorted(cap.glob('*1907*reflections.tsv'))[0]:
   img=Image.fromarray((np.clip(np.concatenate(list(rgb),axis=1),0,1)**(1/2.2)*255).astype('uint8')).resize((768,128),Image.Resampling.NEAREST)
   canvas=Image.new('RGB',(768,152));canvas.paste(img,(0,24));ImageDraw.Draw(canvas).text((5,5),f"{r['gva']} mip {r['mip']} (+X,-X,+Y,-Y,+Z,-Z)",fill='white');canvas.save(dst.with_suffix('.png'))
(out/'summary.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results[:15],indent=2))
