from pathlib import Path
exec(Path('work/relocs.py').read_text().split('for off in range')[0])
for off,v in rel.items():
 if v==0x2738230:
  print('vtable',hex(off))
  for p in range(off-0x100,off+0x40,8):print(hex(p),hex(rel.get(p,0)))
