import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,n in fns:
 if 0xb32700<=f<0xb33e90: print(hex(f),hex(n),vals(f))
for f in [0xb35ae0,0xb32cd4,0xb338dc,0xaf5438]:
 print('VT',hex(f),[hex(x) for x in D['rev'].get(f,[])])
for pc in D['calls'].get(0x1059a78,[]):print('CALL',hex(pc),'OWNER',hex(owner(pc)))
