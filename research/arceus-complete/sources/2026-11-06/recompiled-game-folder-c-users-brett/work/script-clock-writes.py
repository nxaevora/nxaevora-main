exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
describe(0x1059a78)
for p in range(0x1055000,0x105c000,4):
 w=struct.unpack_from('<I',b,p)[0]
 if w&0xffc00000==0xb9000000 and ((w>>10)&4095)*4==0x88:
  print('WRITE',hex(p));describe(owner(p))
