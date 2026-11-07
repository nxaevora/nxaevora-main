import sys
sys.path.pop(0)
from pathlib import Path
import json,hashlib
rows={}
for tag in ['shipped','current']:
 a=Path('work/pass8-shader')/(tag+'-original');b=a.with_name(tag+'-fixed')
 changed=[]
 for p in a.glob('*.spv'):
  q=b/p.name
  if p.read_bytes()!=q.read_bytes():changed.append(p.name)
 rows[tag]={'total':len(list(a.glob('*.spv'))),'changed':changed,'count':len(changed)}
print(json.dumps(rows,indent=2))
Path('work/pass8-shader/corpus-scope.json').write_text(json.dumps(rows,indent=2))
