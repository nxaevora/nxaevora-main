import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 p=int(arg,16);print('\nTARGET',hex(p),'OWNER',owner(p))
 if p in size:
  describe(p)
  print('REVERSE DATA',[(hex(v),hex(D['rel'].get(v,0))) for v in D['rev'].get(p,[])])
 print('CALLS', [hex(x) for x in D['calls'].get(p,[])])
 print('REFS', [(hex(x),hex(owner(x)) if owner(x)!=None else None) for x in D['refs'].get(p,[])])
 for x in range(p-16,p+0x100,8):
  if x in D['rel']:
   t=D['rel'][x];print('DATA',hex(x),hex(t),str(vals(t))[:500])
