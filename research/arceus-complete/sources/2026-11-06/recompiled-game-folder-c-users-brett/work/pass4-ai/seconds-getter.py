import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,n in fns:
 if 0x1059a30<=f<0x1059ac0:
  print(hex(f),hex(n),'calls',len(D['calls'].get(f,[])))
  if n<=12:print([hex(struct.unpack_from('<I',b,f+i)[0]) for i in range(0,n,4)])
for f,n in fns:
 if 0xb35400<=f<0xb37700 and vals(f):print(hex(f),vals(f))
