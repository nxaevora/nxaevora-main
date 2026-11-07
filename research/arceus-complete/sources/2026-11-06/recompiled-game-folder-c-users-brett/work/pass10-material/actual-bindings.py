import sys
sys.path.pop(0)
from pathlib import Path
import json,collections
c=json.loads(Path('work/pass9-captures/timing-20261106-172820-124/materials.json').read_text())
inv={r['hash']:r for r in json.loads(Path('work/pass9-material/material-inventory.json').read_text())}
r=[r for r in c['records'].values() if r['draw']['fs']=='a336734bf0d4c9c0']
rows=[]
for v in r:
 rows.append({'id':v['draw']['id'],'present':v['draw']['present'],'normal_input':v['slots']['1']['input'],'normal_output':next((o for o in v['outputs'] if o['slot']=='2' and o['depth']=='0'),None)})
a={r['draw']['fs'] for r in c['records'].values() if r['draw']['fs'] in inv}
report={'run':c['run'],'a336_configurations':len(rows),'active_standard_shaders':len(a),'active_standard_screen_xy':[h for h in a if inv[h]['screen_xy']],'active_standard_quad_ops':[h for h in a if inv[h]['quad_ops']],'a336_records':rows}
Path('work/pass10-material/actual-binding-scope.json').write_text(json.dumps(report,indent=2))
print('a336',len(rows),'normal outputs',collections.Counter((x['normal_output']['fkey'],x['normal_output']['vkfmt']) if x['normal_output'] else None for x in rows))
