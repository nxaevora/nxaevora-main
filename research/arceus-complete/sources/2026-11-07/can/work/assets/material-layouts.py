from pathlib import Path
import re,struct,collections
base=Path('../../2026-11-06/recompiled-game-folder-c-users-brett/work')
sources=base/'shipped-fragment-corpus'
def classify(path):
    blob=path.read_bytes()
    _,_,header,_,length,_=struct.unpack_from('<4s5I',blob)
    code=struct.unpack_from('<'+'Q'*(length//8),blob,24+header)
    regs={};good_uv=False;predicate=0;mode=0
    for i,w in enumerate(code[11:80],11):
        if (i-10)%4==0:continue
        dst=w&255;a=(w>>8)&255;b=(w>>20)&255;c=(w>>39)&255
        before=regs.copy()
        regs.pop(dst,None)
        hi=w>>32
        if hi==0x0103f800 and (w>>20)&0xffffffff==0x3f800000:regs[dst]='one'
        elif hi&0xfffffffc in (0xe043ff88,0xe043ff84):
            regs[dst]={0x80:'u',0x84:'v'}.get(((w>>30)&255)*4)
        elif hi==0x4c583020 and (w>>34)&31==8 and ((w>>20)&0x3fff,before.get(a)) in [(6,'u'),(7,'v')]:
            regs[dst]=before[a]+'-offset'
        elif hi==0x4c681020 and b==4 and before.get(a)=='u-offset':regs[dst]='U'
        elif (w>>48)==0x49a1 and ((w>>34)&31)==8 and b==5 and before.get(c)=='one' and before.get(a)=='v-offset':regs[dst]='V'
        elif hi==0x3859103f and b==0 and before.get(a)=='v':regs[dst]='1-v'
        elif (w>>48)==0xd832 and (w>>36)&8191==8 and ((w>>53)&15)==1:
            uv=before.get(a),before.get(b)
            if uv in [('U','V'),('u','1-v')]:
                good_uv=uv==('U','V')
                second=(w>>28)&255
                if second<254:regs[second+1]='alpha'
        elif before.get(a)=='alpha':
            opcode=w&~0xff00
            if opcode==(0x4bb1839c00670707&~0xff00):predicate=1
            elif opcode==(0x36b183bf00070707&~0xff00):predicate=2
        if w==0xe33000000000000f:
            mode=predicate;break
        if predicate and not (w&~0xff00) in [(0x4bb1839c00670707&~0xff00),(0x36b183bf00070707&~0xff00)]:predicate=0
    return int(good_uv)|(mode<<1)
rows={}
for p in sources.glob('*_4.snxs'):
    h=int(p.stem.split('_')[0],16);mode=classify(p)
    if mode:rows[h]=mode
print('features',collections.Counter(rows.values()))
print('reference',[(hex(h),rows.get(h)) for h in [0xa336734bf0d4c9c0,0xd2e4ec657c077cc6,0x7aed9f6d02199fec]])
assert rows.get(0xa336734bf0d4c9c0)==3 and rows.get(0xd2e4ec657c077cc6)==4
dest=Path('work/v2/material-layouts.inc')
with dest.open('w') as f:
    f.write('/* Generated from the shipped fragment programs by material-layouts.py. */\nstatic unsigned world_material_layout(u64 shader){\n    switch(shader){\n')
    for v in sorted(set(rows.values())):
        for h in sorted(h for h,k in rows.items() if k==v):f.write(f'    case 0x{h:016x}ull:\n')
        f.write(f'        return {v};\n')
    f.write('    default:return 0;\n    }\n}\n')
