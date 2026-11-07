import sys
sys.path.pop(0)
from pathlib import Path
import json,re,collections
root=Path('work/pass8-render')
log=Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-164839-938/game-err.txt').read_text(errors='replace')
manifest={}
for line in Path('work/shipped-fragment-manifest.txt').read_text().splitlines():
 h,n,p=line.split(' ',2);manifest[h]=p.split('\\')[-1]
changed={x['hash'] for x in json.loads(Path('work/shipped-fragment-changes.json').read_text())}
draws={}
for lineno,line in enumerate(log.splitlines(),1):
 if 'resolution-trace: draw ' in line:
  d=dict(re.findall(r'(\w+)=([^ ]+)',line));d['line']=lineno;d['inputs']=[]
  d['container']=manifest.get(d['hash'],'unknown');d['screen_fetch_changed']=d['hash'] in changed
  draws[d['id']]=d
 if 'resolution-trace: input ' in line:
  d=dict(re.findall(r'(\w+)=([^ ]+)',line));d['line']=lineno
  draws[d['id']]['inputs'].append(d)
(root/'draws.json').write_text(json.dumps(list(draws.values()),indent=2))
lines=[]
for d in draws.values():
 if d['screen_fetch_changed'] or 'ha_ao' in d['container'] or d['id'] in ['85','96']:
  lines.append(f"draw{d['id']} line{d['line']} present{d['present']} hash{d['hash']} {d['container']} changed={d['screen_fetch_changed']} guest={d['guest']} image={d['image']} viewport={d['viewport']} scissor={d['scissor']} target={d['target']} count={d['count']}")
  for t in d['inputs']:lines.append(f"  slot{t['slot']} {t['target']} guest={t['guest']} image={t['image']} rt={t['rt']}")
(root/'changed-pass-chain.txt').write_text('\n'.join(lines)+'\n')
print('drawconfigs',len(draws),'limit reached','draw configuration limit reached' in log)
print('changed hashes observed',sorted({d['hash'] for d in draws.values() if d['screen_fetch_changed']}))
print('known AO observed',[(d['hash'],d['id']) for d in draws.values() if 'ha_ao' in d['container']])
print('main geometry',[(d['id'],d['viewport'],d['scissor'],d['present']) for d in draws.values() if d['hash']=='a336734bf0d4c9c0'])
