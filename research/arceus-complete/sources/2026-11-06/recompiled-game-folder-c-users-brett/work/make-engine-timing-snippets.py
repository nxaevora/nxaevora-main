import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
def word(p):return struct.unpack_from('<I',b,p)[0]
# Keep the actual relevant instructions, in their original data-flow order.
# Removed instructions only perform unrelated loads/locks; the scheduler block
# keeps its original branches and has RET substituted after the final store.
select=[0x2af25ec,0x2af25f0,0x2af25f4,0x2af25f8,0x2af25fc,0x2af2620,0x2af2a10,0x2af2a18]
solver=[0x2f39c78,0x2f39c7c,0x2f39c80,0x2f39c84,0x2f39c88,
        0x2f39c94,0x2f39ca8,0x2f39cac,0x2f39cb0,0x2f39cb4,0x2f39cb8,
        0x2f39cd0,0x2f39cd8,0x2f39cdc,0x2f39ce0,0x2f39ce4,0x2f39d90]
context=list(range(0x1051830,0x10518e0,4))
groups={'select':select,'solver':solver,'context':context}
groups.update({
    'throw1':list(range(0x1d4892c,0x1d48944,4)),
    'throw2':list(range(0x1d49838,0x1d49850,4)),
    'throw3':list(range(0x1d49ef8,0x1d49f10,4)),
    'recenter':list(range(0x193643c,0x1936594,4)),
    'battle':list(range(0x9bad90,0x9bad9c,4))})
lines=['/* Local research fixtures extracted from the user-owned 1.1.1 main. */']
manifest={}
for name,pcs in groups.items():
    returns={'context':0x1051880,'recenter':0x193645c}
    words=[word(p) if p!=returns.get(name) else 0xd65f03c0 for p in pcs]
    if name not in returns:words.append(0xd65f03c0)
    lines.append('static const u32 guest_'+name+'[] = {'+','.join(f'0x{w:08x}' for w in words)+'};')
    manifest[name]={'original_offsets':[hex(p) for p in pcs],'return_inserted_at':hex(returns[name]) if name in returns else 'end',
                    'scope':'Isolated arithmetic/control-flow slice; not the full subsystem.'}
Path('work/engine-timing-snippets.h').write_text('\n'.join(lines)+'\n')
Path('work/engine-timing-snippets.json').write_text(json.dumps(manifest,indent=2))
print(f'{len(groups)} instruction slices prepared; original game files read only.')
