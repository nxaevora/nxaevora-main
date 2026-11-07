import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import struct
out=[]
for off in [0,0x2c,0x64,0x90,0x380,0x408,0x4bc,0x534]:
 p=0x2f269e0+off;out.append(f'main+{p:X} offset+{off:X} word={struct.unpack_from("<I",b,p)[0]:08X}')
out.append('Replacement at +2C: AA0103FA MOV X26,X1 -> 9100003A ADD X26,X1,#0; NZCV unchanged')
out.append('Field accesses in scheduler region:')
for p in range(0x2960000,0x2972000,4):
 w=struct.unpack_from('<I',b,p)[0]
 if w&0xffc00000 in [0xb9400000,0xb9000000] and ((w>>10)&4095)*4 in [0x37c,0x370,0x1e8,0x5d4]:
  out.append(f'{p:X} function={owner(p):X} word={w:08X} offset={((w>>10)&4095)*4:X}')
Path('work/pass5-physics/guards-fields.txt').write_text('\n'.join(out)+'\n')
print('\n'.join(out))
