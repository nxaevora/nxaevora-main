from pathlib import Path
import json,re,collections
import sys
d=json.loads(Path(sys.argv[1]).read_text());rows=collections.defaultdict(collections.Counter)
for l in Path(sys.argv[2]).open(errors='replace'):
 m=re.search(r'world-mat: frame=(\d+) draw=(\d+) fs=(\w+) cb=(\w+) size=(\d+)x(\d+) signature=(\w+)',l)
 if m:
  frame,draw,shader,cb,w,h,sig=m.groups();names=[n for n,x,y,f in d.get(sig,[]) if (x,y)==(int(w),int(h))]
  labels=frozenset(x.rsplit('_',1)[-1] for x in names)
  rows[shader][(cb,','.join(sorted(labels)) or '?')]+=1
for k,r in sorted(rows.items(),key=lambda x:-sum(x[1].values()))[:12]:print(k,sorted(r.items()))
