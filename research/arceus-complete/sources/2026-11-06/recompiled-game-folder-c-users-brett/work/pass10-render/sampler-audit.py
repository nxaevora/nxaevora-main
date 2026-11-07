import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,collections
j=json.loads(Path('work/pass10-render/materials.json').read_text());norm=[];shadow=[];bias=collections.Counter()
def tsc(v):
 t=[int(x,16) for x in v.split(',')];b=(t[1]>>12)&8191;b=b-8192 if b&4096 else b
 return dict(wrap=[t[0]>>i&7 for i in [0,3,6]],compare=t[0]>>9&1,compare_op=t[0]>>10&7,srgb_bit=t[0]>>13&1,mag=t[1]&3,min=t[1]>>4&3,mip=t[1]>>6&3,bias=b/256,min_lod=(t[2]&4095)/256,max_lod=(t[2]>>12&4095)/256,aniso_code=t[0]>>20&7,reduction=t[1]>>10&3)
for id,r in j['records'].items():
 d=r['draw'];bias[d['bias_raw']+'|'+d['bias_enable']]+=1
 if d['fs'] in ['815803708190e019','b75440cca42d33f5']:shadow.append(dict(id=id,draw=d,outputs=r['outputs']))
 for si,s in r['slots'].items():
  v=s.get('input',{});desc=s.get('descriptors',{})
  if v.get('fkey')!='24928':continue
  tic=[int(x,16) for x in desc['tic'].split(',')]
  norm.append(dict(id=id,fs=d['fs'],present=d['present'],slot=si,cbuf_word=s['shader-input']['cbuf_word'],address=v['address'],guest=v['guest'],levels=v['levels'],conversion=v['conversion'],srgb=v['srgb'],map=v['map'],upload_valid=v['upload_valid'],tic_base=tic[7]&15,tic_max=tic[7]>>4&15,tic_glevels=(tic[3]>>28&15)+1,sampler=tsc(desc['tsc'])))
def groups(fields):
 a=collections.Counter(tuple(str(n[k]) for k in fields) for n in norm);return [dict(zip(fields,k),n=v) for k,v in a.most_common()]
sam=collections.Counter(json.dumps(n['sampler'],sort_keys=True) for n in norm)
r=dict(normal_like_BC5_binding_observations=len(norm),unique_BC5_addresses=len(set(n['address'] for n in norm)),states=groups(['guest','levels','tic_base','tic_max','tic_glevels','conversion','srgb','map','upload_valid']),samplers=[dict(state=json.loads(k),n=v) for k,v in sam.items()],bias_states=dict(bias),shadow_count=len(shadow),shadow_draws=shadow,normal_records=norm)
Path('work/pass10-render/sampler-audit.json').write_text(json.dumps(r,indent=2));print(json.dumps({k:r[k] for k in ['normal_like_BC5_binding_observations','unique_BC5_addresses','states','samplers','bias_states','shadow_count']},indent=2))
