import sys
sys.path.pop(0)
from pathlib import Path
import json,re,csv,io,collections,struct
root=Path('C:/Users/brett/Documents/Codex')
run=Path(sys.argv[1]) if len(sys.argv)>1 else root/'work/arceus-runs/timing-20261106-181617-047'
out=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else Path(__file__).resolve().parent
out.mkdir(parents=True,exist_ok=True)
records={}
for line in (run/'game-err.txt').read_text(errors='replace').splitlines():
    if not line.startswith('material-trace: '):continue
    fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
    if 'id' not in fields:continue
    rec=records.setdefault(fields['id'],{'outputs':[],'slots':{}})
    cat=line.split()[1]
    if cat=='draw':rec['draw']=fields
    elif cat=='output':rec['outputs'].append(fields)
    else:rec['slots'].setdefault(fields['slot'],{})[cat]=fields
(out/'materials.json').write_text(json.dumps(records,indent=2))
focused=[]
for ident,r in records.items():
    d=r.get('draw',{})
    if int(d.get('present',0))<6000:continue
    focused.append({'id':ident,'present':d['present'],'fs':d['fs'],'count':d['count'],
                    'outputs':[f"{v['slot']}:{v['address']}" for v in r['outputs']],
                    'inputs':[{k:v.get('input',{}).get(k) for k in ['slot','address','guest','image','fkey','vkfmt','srgb','map']} for v in r['slots'].values()]})
(out/'late-draws.json').write_text(json.dumps(focused,indent=2))
for f in focused:print(json.dumps(f))
print('records',len(records))
# Capture attachment metadata in temporal write order.
manifest=Path(sys.argv[3]) if len(sys.argv)>3 else root/'work/arceus-games/ArceusTest/captures/screenshot_20261106_181800_buffers_4_attachments.tsv'
rows=list(csv.DictReader(io.StringIO('\n'.join(l for l in manifest.read_text().splitlines() if not l.startswith('#'))),delimiter='\t'))
(out/'attachments.json').write_text(json.dumps(rows,indent=2))
print('Recent full-scene attachments:')
for r in sorted(rows,key=lambda r:int(r['wseq'])):
    if int(r['image_w'])>=1280:print({k:r[k] for k in ['index','gva','vkformat','image_w','image_h','drawn_at','wseq']})
