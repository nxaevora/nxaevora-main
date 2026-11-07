import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for p in [0x3788d10,0x37897c0,0x3789ac0,0x3789ad0]: print(hex(p),struct.unpack_from('<4f',b,p),b[p:p+16].hex())
groups=[('wait',0xb35ae0,0x90),('success',0xbd4380,0xc),('running',0xbd43a4,0xc),('getter',0x1059a78,8),('turn',0xb32e34,0xb32fbc-0xb32e34+4),('turn2',0xb33ad4,0xb33c5c-0xb33ad4+4),('count1',0xb32e00,0x10),('count2',0xb33aa0,0x10)]
lines=['/* Local-only Arceus 1.1.1 instructions. */']
for name,start,length in groups:
 data=bytearray(b[start:start+length])
 if name in ('turn','turn2','count1','count2'):struct.pack_into('<I',data,len(data)-4,0xd65f03c0)
 lines.append('static const u8 '+name+'_code[]={'+','.join(str(x) for x in data)+'};')
lines.append('static const u8 turn_constants[]={'+','.join(str(x) for x in b[0x3788000:0x378b000])+'};')
assert struct.unpack_from('<I',b,0x40e7bf8)[0]==1
assert struct.unpack_from('<I',b,0x40e7c04)[0]==4
Path('work/pass4-ai/ai-fixtures.h').write_text('\n'.join(lines)+'\n')

provenance={
 'source':'Local main.bin, Arceus 1.1.1; no game files uploaded',
 'main_sha256':hashlib.sha256(b).hexdigest(),
 'addresses':'main-relative; unchanged relative code/data addressing in test memory',
 'ranges':[{'name':name,'start':hex(start),'end_exclusive':hex(start+length),
            'ret_substitution':hex(start+length-4) if name in ('turn','turn2','count1','count2') else None}
           for name,start,length in groups],
 'constants_range':['0x3788000','0x378b000'],
 'status_globals':{'0x40e7bf8':1,'0x40e7c04':4,'verified_from_source':True},
 'stubs':{'0xa5f8a8':'Context acquisition writes CONTEXT into [X8], then returns to X30.',
          '0x2943a5c':'Reference release returns W0=0 (not last reference), then returns to X30.'},
 'scope':'Complete WaitFrame control flow with real getter/status helpers; isolated Turn/TurnKey counter and quaternion/direction blocks. The actual actor rotation setter and resource/lifetime operations are not executed.'}
Path('work/pass4-ai/fixture-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
