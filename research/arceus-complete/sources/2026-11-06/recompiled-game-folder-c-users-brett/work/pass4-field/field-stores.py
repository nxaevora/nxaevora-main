import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
regions=[(0x2540000,0x2550000,{0xd00,0xd04}),(0x1b59000,0x1b62000,{0x440,0x448}),(0x24e8000,0x24f4000,{0x240,0x2a0}),(0x2580000,0x2590000,{0xd08,0xd00,0xcf4})]
for lo,hi,fields in regions:
 print('REGION',hex(lo))
 for p in range(lo,hi,4):
  w=struct.unpack_from('<I',b,p)[0]
  if (w&0xffc00000==0xb9000000 and ((w>>10)&0xfff)*4 in fields) or (w&0xffc00000==0xf9000000 and ((w>>10)&0xfff)*8 in fields):
   print(f'{p:X} owner={owner(p):X} word={w:08X}')
  if w&0xffc00000==0x91000000 and ((w>>10)&0xfff) in fields and lo==0x1b59000:
   print(f'{p:X} ADD owner={owner(p):X} word={w:08X}')
for f in (0x1b5cd84,0x2549fa0,0x2587684,0x24ee204):
 print('FUNCTION',hex(f));describe(f)
