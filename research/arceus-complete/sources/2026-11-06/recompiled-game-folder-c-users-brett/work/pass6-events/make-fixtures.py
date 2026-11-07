import sys
sys.path.pop(0)
from pathlib import Path
import json,hashlib
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
root=Path('work/pass6-events')
regions={'wrapper':(0xfbfbd0,0xfbfd2c),'advance_sound':(0xfc1078,0xfc10e4),'advance':(0xfc10e4,0xfc1130),'rate_set':(0x2b320c4,0x2b320d0),'rate_get':(0x2b320d0,0x2b320dc),'base':(0x2b315e8,0x2b31fac),'colors':(0x2b31fac,0x2b320c4),'parent':(0x27379b8,0x2737abc),'drain':(0x2737abc,0x2737bec),'state':(0x2737d94,0x2737e70),'timed_close':(0x2485390,0x24853e0),'close_forward':(0x2484ecc,0x2484ed8)}
tables={'base_tables':(0x39847f8,0x3984812),'owner_tables':(0x397e422,0x397e434)}
lines=[]
for name,(a,z) in (regions|tables).items():lines.append('static const u8 '+name+'_code[]={'+','.join(f'0x{x:02x}' for x in b[a:z])+'};')
(root/'message-fixtures.h').write_text('\n'.join(lines)+'\n')
(root/'fixture-provenance.json').write_text(json.dumps({'main_sha256':hashlib.sha256(b).hexdigest(),'functions':{k:[hex(x) for x in v] for k,v in regions.items()},'tables':{k:[hex(x) for x in v] for k,v in tables.items()},'changes':'None; every byte copied unchanged at original main-relative address.','stubs':'Color query virtual+78 returns false; virtual+80 post-layout noop; owner animation/start/cursor callbacks controlled. Sound is not exercised (empty event string). Timed-close target2737C00 is a stub checking receiver/close request and setting message state5. Full glyph parsing/rendering, close sound/animation and resource setup not exercised.','scope':'Original state/status handling, input predicate/latch, automatic-delay counter, parent/drain ordering, selected terminal text-boundary paths and timed-subtype post-completion counter. Synthetic object fields; no live objects or game process.'},indent=2)+'\n')
print('Extracted twelve original message functions and jump tables without changes')
