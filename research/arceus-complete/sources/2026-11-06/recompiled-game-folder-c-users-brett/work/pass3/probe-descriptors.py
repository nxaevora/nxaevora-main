import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
f=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
for p,patch,extra in [(0xc21490,0x3c,0xd0),(0x180c338,0x30,0x61c)]:
 w=lambda o:struct.unpack_from('<I',b,p+o)[0]
 print('{'+','.join(hex(x) for x in [p,f[p],w(0),patch,w(patch),0x91000000|(w(patch)&31),extra,w(extra)])+'}')
s=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
Path('work/pass3/arceus_clock-before-pass3.c').write_bytes(s.read_bytes())
