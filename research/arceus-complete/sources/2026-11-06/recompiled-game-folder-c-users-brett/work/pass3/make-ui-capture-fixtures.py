import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
def word(p):return struct.unpack_from('<I',b,p)[0]
groups={
 'fade':list(range(0xc21490,0xc21cfc,4)),
 'capture_wait':list(range(0x180c94c,0x180ca00,4)),
 'ui_clock':[0x2b43454,0x2b43458,0x2b4345c,0x2b43460,0x2b43468,0x2b43474,0x2b43478]}
lines=['/* Local-only 1.1.1 instruction fixtures. Full fade update/helpers; two isolated slices. */']
for name,pcs in groups.items():
    words=[0xd65f03c0 if name=='capture_wait' and p in (0x180c960,0x180c9fc) else word(p) for p in pcs]
    if name=='ui_clock':words.append(0xd65f03c0)
    lines.append('static const u32 '+name+'_code[]={'+','.join(f'0x{w:08x}' for w in words)+'};')
lines.append('static const u64 ui_denominators[]={'+','.join(str(struct.unpack_from('<Q',b,0x39848e0+i*8)[0]) for i in range(5))+'};')
Path('work/pass3/ui-capture-fixtures.h').write_text('\n'.join(lines)+'\n')
Path('work/pass3/ui-capture-fixtures.json').write_text(json.dumps({
 'fade':{'range':['C21490','C21CFC'],'altered_instructions':0,'runtime_conditions':'Null UI pane handles; state 3; color/crossfade modes'},
 'capture_wait':{'range':['180C94C','180CA00'],'return_substitutions':['180C960','180C9FC']},
 'ui_clock':{'offsets':[hex(p) for p in groups['ui_clock']],'return_substitution':'appended'}} ,indent=2))
print('Full fade/helper block and capture/UI arithmetic fixtures generated locally.')
print('Capture stage dispatch:')
for i in range(19):
    target=0x180c640+4*struct.unpack_from('<H',b,0x397c6aa+i*2)[0]
    print(f'{i:2}: main+{target:X}')
