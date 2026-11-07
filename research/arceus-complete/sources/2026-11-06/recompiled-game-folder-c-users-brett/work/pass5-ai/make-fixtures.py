import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
groups={'round':(0x1051784,0x1051798,None),'bucket':(0x10517c0,0x10517e0,None),'context':(0x1051830,0x10518e0,0x1051880)}
lines=['/* Local-only guest instruction fixtures. */'];manifest={}
for name,(start,end,ret) in groups.items():
 words=[0xd65f03c0 if p==ret else struct.unpack_from('<I',b,p)[0] for p in range(start,end,4)]
 if ret is None:words.append(0xd65f03c0)
 lines.append('static const u32 guest_'+name+'[] = {'+','.join(f'0x{w:08x}' for w in words)+'};')
 manifest[name]={'original_start':hex(start),'original_end_exclusive':hex(end),'ret_substitution':hex(ret) if ret else None,'appended_ret':ret is None}
Path('work/pass5-ai/scheduler-fixtures.h').write_text('\n'.join(lines)+'\n')
Path('work/pass5-ai/fixture-provenance.json').write_text(json.dumps({'main_sha256':hashlib.sha256(b).hexdigest(),'slices':manifest,'relocation':'round -> 0x1000; bucket -> 0x2000; context -> 0x3000; original internal relative branches preserved','stubs':'None. Context slice returns before reference-release instructions; pointer +58 is supplied readable controlled memory.','scope':'Only round-robin index, bucket current/previous flag selection, and context reset/accumulation. No task queue, actor action, resources, or game process.'},indent=2)+'\n')
