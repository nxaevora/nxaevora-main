import sys
sys.path.pop(0)
from pathlib import Path
import json,struct,hashlib
p=Path('work/pass4-ai/make-fixtures.py');s=p.read_text()
s=s.replace("import struct\n", "import struct,json,hashlib\n")
s=s.replace("Path('work/pass4-ai/ai-fixtures.h').write_text", "assert struct.unpack_from('<I',b,0x40e7bf8)[0]==1\nassert struct.unpack_from('<I',b,0x40e7c04)[0]==4\nPath('work/pass4-ai/ai-fixtures.h').write_text")
s+='''\nprovenance={
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
Path('work/pass4-ai/fixture-provenance.json').write_text(json.dumps(provenance,indent=2)+'\\n')
'''
p.write_text(s)
