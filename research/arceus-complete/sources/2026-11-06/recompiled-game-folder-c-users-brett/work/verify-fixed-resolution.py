from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
p=0xcc35a4
fns=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
assert fns[p]==0x2c8
expected={0:0xd10103ff,0x20:0x51000509,0x24:0x7100093f,0x28:0x54000308,0x94:0xd65f03c0}
for off,value in expected.items():assert struct.unpack_from('<I',b,p+off)[0]==value
replacement=0x52800069
assert replacement&31==9 and (replacement>>5)&0xffff==3
branch=expected[0x28]
assert branch&15==8 # HI, unsigned greater than
imm=(branch>>5)&0x7ffff
if imm&0x40000:imm-=0x80000
assert 0x28+imm*4==0x88
assert struct.unpack_from('<4I',b,p+0x88)==(0xa9437bfd,0xf94013f3,0x910103ff,0xd65f03c0)
source=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c').read_text()
fn=source.split('static void fixed_resolution_init(void) {')[1].split('void arceus_clock_post_jit')[0]
assert fn.count('gw32(')==1
assert 'gw32(pc + 0x20, 0x52800069)' in fn
assert 'jit_code_changed(pc, 0x2c8)' in fn
print('Exact binary guards and early-return epilogue verified; no scene-mode setter patch.')
