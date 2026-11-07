exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for arg in sys.argv[1:]:
 f=owner(int(arg,16)); seen={f};level=[f];print('\nTARGET',arg)
 for depth in range(5):
  nxt=[]
  for a in level:
   for c in callers(a):
    if c in seen:continue
    seen.add(c);nxt.append(c)
    vs=vals(c)
    if vs:print(depth+1,hex(c),str(vs)[:550])
  level=nxt
  if len(seen)>400:break
