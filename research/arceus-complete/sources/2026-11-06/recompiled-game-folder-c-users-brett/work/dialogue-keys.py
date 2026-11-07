from pathlib import Path
import re
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for m in re.finditer(rb'[ -~]{5,}\x00',b):
 s=m.group()[:-1].decode();low=s.lower()
 if len(s)<70 and any(k in low for k in ['textspeed','waitframe','skip','msgspeed','messagespeed','nextpage','messageview','messagewindow']):print(hex(m.start()),s)
