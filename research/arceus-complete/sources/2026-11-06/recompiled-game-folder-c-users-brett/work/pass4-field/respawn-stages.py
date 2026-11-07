import sys
sys.path.pop(0)
from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for i in range(5): print('respawn state',i+1,hex(0x24ee2ac+4*struct.unpack_from('<H',b,0x397d8f2+i*2)[0]))
