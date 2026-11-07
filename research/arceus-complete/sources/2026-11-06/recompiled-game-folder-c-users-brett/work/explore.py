from pathlib import Path
import json,bisect,sys,struct
D=json.loads(Path('work/timing-index.json').read_text());fns=D['fns'];starts=[a for a,z in fns];size=dict(fns)
for k in ['calls','refs','rel','rev','srefs']: D[k]={int(a):v for a,v in D[k].items() if a!='null'}
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
def owner(p):
 i=bisect.bisect_right(starts,p)-1
 return fns[i][0] if i>=0 and p<fns[i][0]+fns[i][1] else None
def vals(f):return list(dict.fromkeys(v for p,t,v in D['srefs'].get(f,[])))
def callers(f):return sorted({owner(p) for p in D['calls'].get(f,[])+D['refs'].get(f,[]) if owner(p)!=None and owner(p)!=f})
def describe(f,depth=1):
 print(' FN',hex(f),'size',size.get(f),'STR',str(vals(f))[:850])
 if depth:
  for c in callers(f)[:15]:print('  CALLER',hex(c),str(vals(c))[:800])
for arg in sys.argv[1:]:
 p=int(arg,16);f=owner(p);print('\nTARGET',arg);describe(f)
 for vt in D['rev'].get(f,[])[:2]:
  print(' VTABLE',hex(vt))
  for t,rs in D['refs'].items():
   if vt-0x600<=t<=vt:
    for ref in rs[:3]:
     c=owner(ref)
     print('  CTOR REF',hex(t),hex(ref));describe(c)
