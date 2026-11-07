import sys
sys.path.pop(0)
from pathlib import Path
import json,hashlib
root=Path('work/pass6-resolution')
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
groups={'code':(0xcc386c,0xcc3e88),'constants':(0x3978732,0x3978778),'names':(0x3982428,0x3982468)}
lines=[]
for name,(lo,hi) in groups.items():
 data=b[lo:hi]
 lines.append('static const u8 '+name+'_bytes[]={'+','.join(hex(v) for v in data)+'};')
 lines.append(f'#define {name.upper()}_START 0x{lo:x}')
(root/'resolution-fixtures.h').write_text('\n'.join(lines)+'\n')
(root/'resolution-fixtures.json').write_text(json.dumps({'main_sha256':hashlib.sha256(b).hexdigest(),'unmodified_ranges':{k:[hex(x) for x in v] for k,v in groups.items()},'external_stubs':'Parameter service: read descriptor and float writes; no real GPU or render targets'},indent=2)+'\n')
