import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
# The original translation block consumes a duplicated timestep in V0. Extract
# the preceding load/dup, then the translation-only instructions. Quaternion
# integration is a separate calculation and is deliberately not exercised.
pcs=[0x2f26c84,0x2f26c88,0x2f26d54]+list(range(0x2f26d5c,0x2f26db0,4))
words=[struct.unpack_from('<I',b,p)[0] for p in pcs]+[0xd65f03c0]
Path('work/pass4-physics/motion-fixture.h').write_text('static const u32 motion_code[]={'+','.join(hex(w) for w in words)+'};\n')
Path('work/pass4-physics/motion-fixture.json').write_text(json.dumps({'source_function':'main+2F269E0','pcs':[hex(p) for p in pcs],'added_instruction':'RET','omissions':'Quaternion update; no full body/solver execution','input':'X24 points to world+530, X28 to motion; stack+130 supplied vector'},indent=2))
print('Extracted',len(pcs),'original translation instructions plus return.')
