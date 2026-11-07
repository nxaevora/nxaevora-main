import sys
sys.path.pop(0)
from pathlib import Path
import struct, json, subprocess,re
root=Path(r'C:/Users/brett/Documents/Codex/work')
b=(root/'arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
sizes=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
def word(p):return struct.unpack_from('<I',b,p)[0]
def signed(v,n):return v-(1<<n) if v&(1<<(n-1)) else v
for p in [0x2518ba8,0x2518ca4]:
    assert sizes[p]==0xfc and word(p)==0xf81e0ff3
    assert word(p+0x40)==0x71000508 and word(p+0xf8)==0x17ffffd5
    old=word(p+0xf8); new=0xb4fffabf
    assert signed(old&0x3ffffff,26)*4==signed((new>>5)&0x7ffff,19)*4
    assert new&31==31 and new>>24==0xb4  # CBZ XZR: always branches
p=0x1059a78
assert sizes[p]==8 and word(p)==0xb9408800 and word(p+4)==0xd65f03c0
old=word(p);new=0xb8488000
assert ((old>>10)&0xfff)*4==signed((new>>12)&0x1ff,9)==0x88
assert old&1023==new&1023==0  # W0 from [X0], no writeback, no flags
p=0x2f39630
assert sizes[p]==0xed8 and word(p)==0xa9ba6ffc and word(p+0xd4)==0xf94002a8
assert word(p+0xed4)==0x17ffffc4
assert signed(0x17ffffc4&0x3ffffff,26)*4==signed((0xb4fff89f>>5)&0x7ffff,19)*4
# Independent disassembly of the replacement and unchanged RET.
Path('work/probe-guard-words.bin').write_bytes(struct.pack('<4I',new,0xd65f03c0,0xb4fffabf,0xd65f03c0))
bindir=root/'llvm-mingw-20261104-ucrt-x86_64/bin'
subprocess.run([str(bindir/'llvm-objcopy.exe'),'-I','binary','-O','elf64-littleaarch64','--rename-section','.data=.text,alloc,load,readonly,code','work/probe-guard-words.bin','work/probe-guard-words.elf'],check=True)
text=subprocess.check_output([str(bindir/'llvm-objdump.exe'),'-d','--no-show-raw-insn','--triple=aarch64','work/probe-guard-words.elf'],text=True)
assert 'ldur\tw0, [x0, #0x88]' in text and 'cbz\txzr,' in text
Path('work/probe-guard-disassembly.txt').write_text(text)
source=(root/'nvn-src/src/nx/runtime/arceus_clock.c').read_text()
checked=0
for fn,n in [('engine_clock_research_init',4),('frame_timer_research_init',5),('sequence_clock_research_init',2)]:
    block=source.split('static void '+fn+'(void) {',1)[1].split('const GuestFn wrappers',1)[0]
    descriptors=re.findall(r'\{(0x[0-9a-f]+(?:,0x[0-9a-f]+){7})\}',block)
    assert len(descriptors)==n
    for descriptor in descriptors:
        p,length,head,patch,original,replacement,extra,extra_word=[int(v,16) for v in descriptor.split(',')]
        assert sizes[p]==length and word(p)==head and word(p+patch)==original and word(p+extra)==extra_word
        if fn in ('frame_timer_research_init','sequence_clock_research_init'):
            assert original&0xffffffe0==0xaa0003e0 and replacement==0x91000000|(original&31)
        checked+=1
print(f'{checked} source-matched exact function guards and equivalent-instruction checks passed.')
