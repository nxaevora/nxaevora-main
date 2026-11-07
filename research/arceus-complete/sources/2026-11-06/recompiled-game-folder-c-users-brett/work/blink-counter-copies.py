exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for p in range(0x1940000,0x1b10000,4):
 w=struct.unpack_from('<I',b,p)[0]
 if w&0xff800000==0xb9000000 and ((w>>10)&4095)*4 in (0x68c,0x644):
  f=owner(p)
  print(hex(p), 'load' if w&0x400000 else 'store',hex(((w>>10)&4095)*4), 'fn',hex(f),size.get(f))
