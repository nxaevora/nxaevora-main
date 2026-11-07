import sys
sys.path.pop(0)
from pathlib import Path
p=Path('work/pass6-physics');p.mkdir(exist_ok=True)
p.joinpath('disasm.py').write_text(Path('work/pass5-physics/disasm.py').read_text().replace('work/pass5-physics','work/pass6-physics'))
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import struct
out=[]
for p in range(0,0x3520000,4):
 w=struct.unpack_from('<I',b,p)[0]
 # unsigned byte store at +121, halfword store at+120, and adjacent-word stores in physics area
 match=((w&0xffc00000)==0x39000000 and ((w>>10)&4095)==0x121)
 match=match or (0x2ae0000<=p<0x2b05000 and ((w&0xffc00000)==0x79000000 and ((w>>10)&4095)*2==0x120))
 if match:
  f=owner(p);out.append(f'{p:X} fn={f:X} w={w:08X} {vals(f)[:6]}' if f else f'{p:X} unknown')
out.append('DYNAMIC FPS STRINGS')
for f,refs in D['srefs'].items():
 ss=[s for p,t,s in refs if ('dynamic' in s.lower() and ('fps' in s.lower() or 'frame' in s.lower()))]
 if ss:out.append(f'{f:X}: {ss}')
Path('work/pass6-physics/writers.txt').write_text('\n'.join(out)+'\n')
print('\n'.join(out))
