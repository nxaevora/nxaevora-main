import sys
sys.path.pop(0)
from pathlib import Path
import json
p=Path('work/pass8-shader/corpus-scope.json');rows=json.loads(p.read_text())
for tag in ['shipped','current']:
 a=Path('work/pass8-shader')/('off-'+tag+'-original');b=a.with_name('off-'+tag+'-fixed')
 changed=[f.name for f in a.glob('*.spv') if f.read_bytes()!=(b/f.name).read_bytes()]
 rows['off-'+tag]={'total':len(list(a.glob('*.spv'))),'count':len(changed),'changed':changed}
p.write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
