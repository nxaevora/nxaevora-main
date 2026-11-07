import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
sizes=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
sites=[(0x193633c,0x1936378,0x193644c),(0x9bad68,0x9bae34,0x9bad94),
       (0x1d482b0,0x1d48374,0x1d4893c),(0x1d49144,0x1d4919c,0x1d49848),
       (0x1d498b4,0x1d4999c,0x1d49f08)]
def word(p):return struct.unpack_from('<I',b,p)[0]
for p,patch,extra in sites:
    old=word(patch)
    assert old&0xffffffe0==0xaa0003e0 # MOV Xd,X0, not SP, no flag writes
    new=0x91000000|(old&31) # ADD Xd,X0,#0
    print(f'{{0x{p:x},0x{sizes[p]:x},0x{word(p):08x},0x{patch-p:x},0x{old:08x},0x{new:08x},0x{extra-p:x},0x{word(extra):08x}}},')
