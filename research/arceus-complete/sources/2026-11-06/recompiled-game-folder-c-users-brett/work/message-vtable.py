from pathlib import Path
exec(Path('work/relocs.py').read_text().split('for off in range')[0])
for off,v in rel.items():
 if v==0x120ae38:
  print('message candidate vtable',hex(off))
  for p in range(off-0x70,off+0x90,8):print(hex(p),hex(rel.get(p,0)))
