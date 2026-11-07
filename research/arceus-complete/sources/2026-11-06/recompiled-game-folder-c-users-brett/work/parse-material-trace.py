"""Summarize a completed local SNX_MATERIAL_TRACE capture without changing it."""
import sys
sys.path.pop(0)
from pathlib import Path
import collections, json, re

if len(sys.argv) != 2:
    raise SystemExit('usage: python parse-material-trace.py RUN_DIRECTORY')
run = Path(sys.argv[1]).resolve()
records = {}
limit_reached = False
for line in (run/'game-err.txt').read_text(errors='replace').splitlines():
    if not line.startswith('material-trace: '):
        continue
    if 'distinct configuration limit reached' in line:
        limit_reached = True
    category = line.split()[1]
    fields = dict(re.findall(r'(\w+)=([^ ]+)',line))
    if 'id' not in fields:
        continue
    identifier = int(fields['id'])
    record = records.setdefault(identifier,dict(draw=None,outputs=[],slots={}))
    if category == 'draw':
        record['draw'] = fields
    elif category == 'output':
        record['outputs'].append(fields)
    else:
        slot = record['slots'].setdefault(fields['slot'],{})
        slot[category] = fields
formats = collections.Counter()
shaders = collections.Counter()
for record in records.values():
    draw = record['draw']
    if draw:
        shaders[draw['fs']] += 1
    for slot in record['slots'].values():
        info = slot.get('input',{})
        if 'fkey' in info:
            formats[(info['fkey'],info['vkfmt'],info['conversion'],info['block'],info['srgb'])] += 1
out = Path('work/pass9-captures')/run.name
out.mkdir(parents=True,exist_ok=True)
report = dict(run=str(run),configuration_count=len(records),limit_reached=limit_reached,
              shaders=dict(shaders),formats=[dict(fkey=k[0],vkfmt=k[1],conversion=k[2],block=k[3],srgb=k[4],slots=v)
                                             for k,v in formats.most_common()],records=records,
              scope='Resolved binding configurations, not complete texture content or GPU frame capture.')
(out/'materials.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:report[k] for k in ['run','configuration_count','limit_reached','shaders','formats']},indent=2))
print(out/'materials.json')
