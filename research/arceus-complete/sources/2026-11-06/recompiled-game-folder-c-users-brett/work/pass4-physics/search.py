import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
print('STORES world+4A8')
for p in range(0x2e00000,0x3100000,4):
 w=struct.unpack_from('<I',b,p)[0]
 if w&0xffc00000==0xf9000000 and ((w>>10)&0xfff)*8==0x4a8:
  print(hex(p),'fn',hex(owner(p)),f'STR X{w&31},[X{(w>>5)&31},4a8]');describe(owner(p),0)
print('Scheduler callbacks',D['rev'].get(0x2b00ca8,[]))
for vt in D['rev'].get(0x2b00ca8,[]):
 for p in range(vt-64,vt+72,8):print(hex(p),hex(D['rel'].get(p,0)))
