import sys
sys.path.pop(0)
from pathlib import Path
import json,collections
root=Path('work/pass10-render')
j=json.loads((root/'materials.json').read_text())
manifest={}
for line in Path('work/shipped-fragment-manifest.txt').read_text().splitlines():
 h,n,p=line.split(' ',2);manifest[h.lstrip('0')]=p.split('\\')[-1]
out=[]
for id,r in j['records'].items():
 for si,s in r['slots'].items():
  v=s.get('input',{})
  if v.get('address') not in ['552210000','5519a0000','552a80000','54fc10000']:continue
  d=r['draw'];desc=s['descriptors'];
  out.append(dict(id=id,fs=d['fs'],container=manifest.get(d['fs'],'?'),present=d['present'],count=d['count'],viewport=d['viewport'],scissor=d['scissor'],scaled=d['scaled'],slot=si,shader_input=s['shader-input'],input=v,descriptors=desc,outputs=r['outputs']))
(root/'gbuffer-consumers.json').write_text(json.dumps(out,indent=2))
for v in out:
 if v['input']['address']!='552210000':continue
 print(f"id={v['id']} p={v['present']} fs={v['fs']} {v['container']} slot={v['slot']} cb={v['shader_input']['cbuf_word']} vp={v['viewport']} sc={v['scissor']} scaled={v['scaled']} input={v['input']} tsc={v['descriptors']['tsc']}")
 print(' outputs',[(p['slot'],p.get('address'),p.get('image'),p.get('vkfmt')) for p in v['outputs']])
print('normal consumer hashes',collections.Counter(v['fs'] for v in out if v['input']['address']=='552210000'))
