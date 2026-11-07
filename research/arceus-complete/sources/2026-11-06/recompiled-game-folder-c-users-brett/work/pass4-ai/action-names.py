import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a,z in [(0xb41c00,0xb42600),(0xaf1900,0xaf2e64),(0xaf4c00,0xaf5c40)]:
 print('RANGE',hex(a),hex(z))
 for f,n in fns:
  if a<=f<z and vals(f): print(hex(f),vals(f))
for f in [0x1059a78,0x1050e00]: print('rel/rev',hex(f),[hex(p) for p in D['rev'].get(f,[])])
