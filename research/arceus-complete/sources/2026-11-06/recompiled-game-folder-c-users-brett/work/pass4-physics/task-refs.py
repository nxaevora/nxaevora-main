import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for t in [0x41b49e0,0x41b3ac8,0x41b5000,0x41b4410,0x41b43d0]:
 print('TABLE',hex(t))
 for p in range(t,t+0x28,8):
  print(hex(p),hex(D['rel'].get(p,0)))
for t in [0x2fc6594,0x2aefa20,0x2fc3454]:
 print('FUNCTION',hex(t),'call PCs',[hex(x) for x in D['calls'].get(t,[])],'relocrefs',[hex(x) for x in D['rev'].get(t,[])])
