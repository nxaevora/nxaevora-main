import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import struct
out=[]
for p in range(0,max(a+z for a,z in fns),4):
 w=struct.unpack_from('<I',b,p)[0];typ=w&0xffc00000;imm=(w>>10)&4095
 label=None
 if typ in [0xb9000000,0xb9400000] and imm*4==0x5b8 and 0x2f00000<=p<0x3010000:label='WORLD5B8'
 if typ==0x91000000 and imm in [0x120,0x121] and 0x2ae0000<=p<0x2b10000:label='ADDR120121'
 if typ in [0xb9000000,0xf9000000,0x79000000] and imm*(4 if typ==0xb9000000 else 8 if typ==0xf9000000 else 2)==0x120 and 0x2ae0000<=p<0x2b10000:label='PACKED120'
 if typ==0xf9400000 and imm*8==0x230 and 0x2af0000<=p<0x2b10000:label='WRAPPER_PTR'
 if label:
  f=owner(p);out.append(f'{label} pc={p:X} fn={f:X} word={w:08X} strings={vals(f)[:5]}' if f else f'{label} {p:X} nofn')
Path('work/pass6-physics/fields.txt').write_text('\n'.join(out)+'\n');print('\n'.join(out))
