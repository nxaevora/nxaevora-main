import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
root=Path('work/pass5-dialogue')
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
groups=[('update',0xfbfbd0,0x15c),('advance',0xfc10e4,0x4c),('advance_sound',0xfc1078,0x6c),('tag',0xfc1170,0x90),('get_rate',0x2b320d0,0x14),('set_rate',0x2b320c4,0xc),('base_step',0x2b315e8,0x9c4),('drain',0x2737abc,0x130)]
lines=['/* Local-only guest instruction fixtures. Do not upload. */']
changes=[]
for name,start,length in groups:
 data=bytearray(b[start:start+length])
 for pc,target in ({0xfbfc20:0x7000} if name=='update' else {0xfc10cc:0x7010} if name=='advance_sound' else {}).items():
  old=struct.unpack_from('<I',data,pc-start)[0]
  struct.pack_into('<I',data,pc-start,0x94000000|(((target-pc)//4)&0x3ffffff))
  changes.append({'pc':hex(pc),'original':hex(old),'change':'BL relocation to external host fixture','target':hex(target)})
 lines.append(f'static const u8 {name}_code[]={{'+','.join(str(x) for x in data)+'};')
lines.append('static const u8 switch_tables[]={'+','.join(str(x) for x in b[0x39847f8:0x3984830])+'};')
root.joinpath('dialogue-fixtures.h').write_text('\n'.join(lines)+'\n')
root.joinpath('fixture-provenance.json').write_text(json.dumps({'main_sha256':hashlib.sha256(b).hexdigest(),'addressing':'main-relative; all original PC-relative code/data locations retained','ranges':[{'name':n,'start':hex(s),'size':hex(z)} for n,s,z in groups],'alterations':changes,'host_stubs':{'0x7000':'Base text step: records byte delay/quota; configured bounded status/return sequence; defaults out-status1/W0=4','0x7010':'Sound event observer','0x7020':'External wait condition: returns configured busy flag'},'scope':'Full selected wrapper/tag/advance/drain methods. Original full base text method executed only on non-parsing delay paths. Inner-loop tests supply explicit sequential base-step statuses/returns. Text glyph rendering, full message UI, input collector, live scheduling and asset/event callbacks not executed.'},indent=2)+'\n')
print('Extracted dialogue fixtures from verified local main.bin')
