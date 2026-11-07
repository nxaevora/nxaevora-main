import sys
sys.path.pop(0)
from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for p in [0x40e7bf8,0x40e7bfc,0x40e7c00,0x40e7c04]: print(hex(p),hex(struct.unpack_from('<I',b,p)[0]))
