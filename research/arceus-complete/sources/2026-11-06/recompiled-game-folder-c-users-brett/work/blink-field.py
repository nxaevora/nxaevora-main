from pathlib import Path
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for o in range(0x1900000,0x1970000,4):
 w=struct.unpack_from('<I',b,o)[0]
 if w&0xff000000 in (0xb9000000,0xbd000000) and ((w>>10)&4095)*4==0x68c: print(hex(o),hex(w))
