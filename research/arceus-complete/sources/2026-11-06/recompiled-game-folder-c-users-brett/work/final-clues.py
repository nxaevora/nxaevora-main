exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f in [0x1b47d10,0x1d1b454,0x167f988]:
 print('strings',hex(f),D['srefs'].get(f))
for t in [0x4272cf0,0x4272ce0,0x427afd0]:
 r=D['rel'].get(t);print('GOT',hex(t),hex(r) if r else None,struct.unpack_from('<f',b,r)[0] if r and r<len(b)-4 else '')
for f,z in fns:
 if 0x167d000<f<0x167f988:
  c=callers(f)
  if c:print('pre timer fn',hex(f),'callers',','.join(hex(x) for x in c[:6]))
