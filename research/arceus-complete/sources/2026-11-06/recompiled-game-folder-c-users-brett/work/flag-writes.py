exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for lo,hi,off in [(0x2780000,0x27a0000,0x150),(0x2aef000,0x2b05000,0x121)]:
 print('\nflag',hex(off))
 for p in range(lo,hi,4):
  w=struct.unpack_from('<I',b,p)[0]
  if w&0xffc00000==0x39000000 and ((w>>10)&4095)==off:
   print('write',hex(p));describe(owner(p))
