import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
sites=[('ready',0x296863c,0x1e4,0x1000),('invoke',0x29684dc,0x160,0x3000)]
lines=[]
for name,pc,size,virtual in sites:
 words=struct.unpack('<'+'I'*(size//4),b[pc:pc+size])
 lines.append('static const u32 '+name+'_code[]={'+','.join(hex(x) for x in words)+'};')
 lines.append(f'#define {name.upper()} {hex(virtual)}')
 lines.append(f'#define {name.upper()}_LISTENER {hex(virtual+0x2968a08-pc)}')
 if name=='ready':lines.append(f'#define ENQUEUE {hex(virtual+0x296b764-pc)}')
Path('work/pass5-physics/dispatcher-fixture.h').write_text('\n'.join(lines)+'\n')
Path('work/pass5-physics/dispatcher-fixture.json').write_text(json.dumps({'functions':[{'name':n,'main_offset':hex(p),'length':hex(s),'fixture_address':hex(v),'instruction_modifications':0} for n,p,s,v in sites],'stubs':['clock virtual0','listener2968A08','enqueue296B764','physics callback virtual70'],'tested_path':'No optional owner; dependency list has only cardinality, downstream list empty'},indent=2)+'\n')
