import sys
sys.path.pop(0)
from pathlib import Path
import json
old=json.loads(Path('work/pass8-render/draws.json').read_text());new=json.loads(Path('work/pass9-render/draws.json').read_text())
fields=['guest','image','viewport','scissor','colors','depth','hash']
key=lambda d:tuple(d.get(k) for k in fields)
a={key(d):d for d in old};b={key(d):d for d in new}
r={'old_config_count':len(old),'new_config_count':len(new),'exact_config_common':len(a.keys()&b.keys()),'old_only':[{k:v for k,v in a[t].items() if k!='inputs'} for t in a.keys()-b.keys()],'new_only':[{k:v for k,v in b[t].items() if k!='inputs'} for t in b.keys()-a.keys()]}
Path('work/pass9-render/trace-difference.json').write_text(json.dumps(r,indent=2))
print('matched configurations',r['exact_config_common'],'old-only',len(r['old_only']),'new-only',len(r['new_only']))
