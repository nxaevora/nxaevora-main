exec(open('work/explore.py').read().split('for arg in sys.argv')[0])
from collections import defaultdict
# Require related fields in the same function, not one coincidental member offset.
hits=defaultdict(set);sites=defaultdict(list)
for o in range(0,0x32a0000,4):
 w=struct.unpack_from('<I',b,o)[0]
 # scalar FP32 unsigned-immediate loads/stores
 if w&0xffc00000 in (0xbd000000,0xbd400000):
  field=((w>>10)&4095)*4
  if field in (0x38c,0x390,0x424,0x428,0x42c,0x430,0x434,0x438,0x43c,0x440):
   f=owner(o)
   if f is not None:hits[f].add(field);sites[f].append((o,field,w))
out=['Whole-text copy audit (static evidence only)','']
for f,fields in hits.items():
 if {0x38c,0x390,0x434,0x438}.issubset(fields):
  out.append(f'Camera-response candidate main+{f:x}, size {size[f]:x}, fields '+','.join(hex(v) for v in sorted(fields)))
# Match countdown load/sub/store, same base and result register.
for o in range(0,0x32a0000-12,4):
 a,v,c=struct.unpack_from('<III',b,o)
 if a&0xffc00000!=0xb9400000 or ((a>>10)&4095)*4!=0x68c:continue
 # Permit intervening compare/conditional branch (as in real blink body).
 for d in (4,8,12,16):
  v,c=struct.unpack_from('<II',b,o+d)
  r=a&31
  if v&0xfffffc00==0x71000400 and v&31==r and ((v>>5)&31)==r and c&0xffc00000==0xb9000000 and ((c>>10)&4095)*4==0x68c and c&31==r and ((c>>5)&31)==((a>>5)&31):
   f=owner(o);out.append(f'Blink countdown candidate main+{f:x}: load {o:x}, decrement {o+d:x}, store {o+d+4:x}')
Path('work/whole-text-copy-audit.txt').write_text('\n'.join(out)+'\n')
print('\n'.join(out))
