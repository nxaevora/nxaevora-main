import sys
sys.path.pop(0)
from pathlib import Path
import csv,json,hashlib
p=Path('work/pass10-texture/capture');lines=(p/'snapshot_attachments.tsv').read_text().splitlines();rows=list(csv.DictReader([x for x in lines if not x.startswith('#')],delimiter='\t'))
assert len(rows)==12
results=[]
for r in rows:
 assert r['status']=='ok'
 k=int(r['fkey'],16);exp=(p/f'expected-{k}.bin').read_bytes();got=Path(r['file']).read_bytes()
 assert len(got)==len(exp)==int(r['bytes'])
 assert int(r['row_bytes'])==int(r['image_w'])*int(r['bpp'])
 assert int(r['layer_bytes'])==int(r['image_w'])*int(r['image_h'])*int(r['bpp'])
 assert int(r['bytes'])==int(r['layer_bytes'])*int(r['layers'])
 if k==9:
  bad=[i for i,(a,b)in enumerate(zip(exp,got)) if i%4!=3 and a!=b]
  unused=sorted(set(got[3::4]))
 else:bad=[i for i,(a,b)in enumerate(zip(exp,got))if a!=b];unused=None
 assert not bad,(k,bad[:10])
 results.append({'fixture':k,'format':int(r['vkformat']),'bytes':len(got),'layers':int(r['layers']),'saved_meaningful_bytes_exact':True,'D24_unused_byte_values':unused,'sha256':hashlib.sha256(got).hexdigest()})
result={'manifest_rows':len(rows),'total_bytes':sum(r['bytes']for r in results),'all_saved_payloads_match':True,'source_bytes_unchanged':'C GPU readback test checks source post-capture, including stencil','results':results}
(p/'verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

