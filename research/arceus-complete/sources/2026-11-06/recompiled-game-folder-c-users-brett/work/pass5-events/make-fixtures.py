import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
root=Path('work/pass5-events')
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
groups={
 'wait':(0x167fc90,0x167ff30,{0x167fca0:0xd65f03c0}),
 'recovery':(0x16832a4,0x1683458,{0x1683304:0xd61f0380,0x168343c:0xd61f0380,0x168344c:0xd61f0380}),
 'fallback':(0x1683528,0x1683884,{}),
 'reset':(0x1682f70,0x168300c,{})}
lines=[];provenance={}
for name,(lo,hi,changes) in groups.items():
 data=bytearray(b[lo:hi])
 for p,w in changes.items():struct.pack_into('<I',data,p-lo,w)
 lines.append('static const u8 '+name+'_code[]={'+','.join(hex(v) for v in data)+'};')
 provenance[name]={'start':hex(lo),'end_exclusive':hex(hi),'entry':hex(0x167ff0c if name=='wait' else lo),'changes':{hex(p):{'original':hex(struct.unpack_from('<I',b,p)[0]),'replacement':hex(w),'reason':'Return at extracted continuation; BR X28 uses preserved zero sentinel after BL' if w==0xd61f0380 else 'Return before unrelated position-cache work'} for p,w in changes.items()}}
for name,lo,hi in [('fade_constant',0x378a290,0x378a2a0),('fallback_jump',0x397c590,0x397c594)]:
 lines.append('static const u8 '+name+'[]={'+','.join(hex(v) for v in b[lo:hi])+'};')
 provenance[name]={'start':hex(lo),'end_exclusive':hex(hi),'changes':{}}
(root/'event-fixtures.h').write_text('\n'.join(lines)+'\n')
(root/'event-fixtures.json').write_text(json.dumps(provenance,indent=2)+'\n')
print('Extracted local event fixtures; original branches and float operations retained.')
