import sys
sys.path.pop(0)
from pathlib import Path
import json
D=json.loads(Path('work/timing-index.json').read_text())
rel={int(k):v for k,v in D['rel'].items() if k!='null'}
for vt,expected in [(0x41b5000,0x2fe2604),(0x41b3ac8,0x2f9158c)]:
 assert rel[vt+0x18]==expected,(hex(vt),rel.get(vt+0x18))
 print(f'vtable main+{vt:X}, solve slot +18 -> main+{expected:X}: verified')
for d,name in [('pass4-attention','attention-checks.txt'),('pass4-physics','motion-checks.txt'),('pass4-ai','ai-checks.txt'),('pass4-field','field-checks.txt')]:
 p=Path('work')/d/name
 raw=p.read_bytes();s=raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
 print(d,s.splitlines()[-1])
