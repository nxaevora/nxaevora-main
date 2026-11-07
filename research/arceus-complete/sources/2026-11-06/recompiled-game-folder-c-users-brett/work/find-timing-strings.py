from pathlib import Path
import re,struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for m in re.finditer(rb'[ -~]{5,}\x00',b):
 s=m.group()[:-1].decode()
 if any(t in s.lower() for t in ['blink','cameraacc','camerarot','messagewait','messagespeed','messagetext','buttonwait','skipwait']): print(hex(m.start()),s[:160])
