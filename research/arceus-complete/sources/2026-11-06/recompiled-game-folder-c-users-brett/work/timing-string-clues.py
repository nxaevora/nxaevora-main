exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import re
pat=re.compile('|'.join(sys.argv[1:]),re.I)
seen=set()
for m in re.finditer(rb'[ -~]{5,240}\x00',b[0x3790000:0x39d0000]):
 s=m.group()[:-1].decode();p=m.start()+0x3790000
 if pat.search(s):
  fs=sorted({owner(q) for q in D['refs'].get(p,[]) if owner(q)})
  print(f'{p:x}',s,'OWNERS',','.join(f'{f:x}' for f in fs))
