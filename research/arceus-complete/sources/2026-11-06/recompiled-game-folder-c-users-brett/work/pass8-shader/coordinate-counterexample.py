import sys
sys.path.pop(0)
from pathlib import Path
src=Path('work/check-screen-fetch-semantics.py').read_text()
exec(src[:src.index("fixtures=Path(")])
rows=[]
for name in ['e9e7255e7594a006_4.spv','e02c1a109a77fac4_4.spv']:
 old=Shader(Path('work/shipped-fragment-baseline')/name)
 new=Shader(Path('work/shipped-fragment-candidate')/name)
 print(name, 'fetches',len(new.fetch))
 for i in range(min(len(new.fetch),6)):
  try:
   a=[old.coordinates(x+.5,.5,4/3,0xffffffff,i) for x in range(16)]
   b=[new.coordinates(x+.5,.5,4/3,0xffffffff,i) for x in range(16)]
   row=dict(shader=name,fetch=i,old=a,new=b)
   rows.append(row)
   print('fetch',i,'old',a,'new',b)
  except Exception as e:print('fetch',i,type(e).__name__,str(e))
Path('work/pass8-shader/coordinate-counterexample.json').write_text(json.dumps(rows,indent=2))
