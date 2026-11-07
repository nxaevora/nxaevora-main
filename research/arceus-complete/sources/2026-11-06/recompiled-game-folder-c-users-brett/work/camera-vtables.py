from pathlib import Path
exec(Path('work/relocs.py').read_text().split('for off in range')[0])
for off,v in rel.items():
 if 0x16ed000<=v<=0x16eea5c: print(hex(off),hex(v))
