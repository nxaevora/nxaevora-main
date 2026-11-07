from pathlib import Path
import re
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for m in re.finditer(rb'[ -~]{5,}\x00',b):
 s=m.group()[:-1].decode();low=s.lower()
 if ('camera' in low and any(t in low for t in ['speed','accel','stick','input','rotate','control'])) or ('message' in low and any(t in low for t in ['speed','skip','wait','input','finish'])): print(hex(m.start()),s[:120])
