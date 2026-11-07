from pathlib import Path
exec(Path('work/relocs.py').read_text().split('for off in range')[0])
for off,v in rel.items():
 if 0x2481000<v<0x2485200:print(hex(off),hex(v))
