import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
word=lambda p:struct.unpack_from('<I',b,p)[0]
groups={
 'climb_start':(list(range(0x254b254,0x254b2ec,4)),set(),False),
 'dash_ramp':(list(range(0x2587bf0,0x2588448,4)),{0x2587bfc,0x2588078},False),
 'wormhole_tick':([0x1b5bc38,0x1b5bc3c,0x1b5bc40],set(),True),
 'respawn_tick':([0x24ee2ac,0x24ee2b0,0x24ee2b4,0x24ee3f4],set(),True)}
lines=[]
for name,(pcs,rets,append) in groups.items():
 words=[0xd65f03c0 if p in rets else word(p) for p in pcs]
 if append:words.append(0xd65f03c0)
 lines.append('static const u32 '+name+'_code[]={'+','.join(f'0x{w:08x}' for w in words)+'};')
Path('work/pass4-field/field-fixtures.h').write_text('\n'.join(lines)+'\n')
Path('work/pass4-field/field-fixtures.json').write_text(json.dumps({n:{'offsets':[hex(p) for p in x[0]],'return_replacements':[hex(p) for p in x[1]],'appended_return':x[2]} for n,x in groups.items()},indent=2))
print('Local field-timer fixtures extracted.')
