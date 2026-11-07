import sys
sys.path.pop(0)
from pathlib import Path
src=Path('work/check-screen-fetch-semantics.py').read_text().split("fixtures=Path(")[0]
src=src.replace('if op==95:self.fetch.append(defs[a[3]])','if op==87:self.fetch.append(defs[a[3]])')
src=src.replace('def coordinates(self,x,y,scale,mask,fetch=0):','def coordinates(self,x,y,scale,mask,fetch=0,w=1.,uv=(0.,0.),transform=(1.,1.,0.,0.)):')
src=src.replace('(f32(x),f32(y),0.0,1.0)','(f32(x),f32(y),0.0,f32(w))')
src=src.replace("raise ValueError(f'unhandled input {ptr} {dec}')",'''if dec.get(30)==(0,):return (f32(uv[0]),f32(uv[1]),0.,1.)
                if dec.get(33)==(8,):
                    assert ptr[1:3]==(0,1),ptr
                    return bits(f32(transform[ptr[3]]))
                raise ValueError(f'unhandled input {ptr} {dec}')''')
src=src.replace("if a[0]==43:return max(vs[1],min(vs[2],vs[0]))", "if a[0]==43:return max(vs[1],min(vs[2],vs[0]))\n                if a[0]==50:return f32(math.fma(*vs))")
src=src.replace('if op==129:return f32(v[0]+v[1])','if op==127:return f32(-v[0])\n            if op==129:return f32(v[0]+v[1])\n            if op==131:return f32(v[0]-v[1])\n            if op==136:return f32(v[0]/v[1])')
Path('work/pass10-material/uv-emitted-evaluator.py').write_text(src)
exec(src)
s=Shader('work/shipped-fragment-candidate/a336734bf0d4c9c0_4.spv')
rows=[];checks=0;maxerr=0;fail=[]
for uv in [(0.,0.),(.125,.875),(.333,.666),(1.,1.),(15.,17.),(-2.,3.)]:
 for w in [.0001,.001,.01,.1,1.,10.,100.]:
  for tr in [(1.,1.,0.,0.),(2.,3.,.1,.2),(.25,.5,-1.,2.)]:
   for sample in range(6):
    got=s.coordinates(.5,.5,4/3,0,sample,w,uv,tr)
    expect=(uv[0],1-uv[1]) if sample==3 else ((uv[0]-tr[2])*tr[0],1-(uv[1]-tr[3])*tr[1])
    err=max(abs(got[i]-expect[i]) for i in range(2));maxerr=max(maxerr,err);checks+=1
    if err>1e-5:fail.append((uv,w,tr,sample,got,expect))
report=dict(cases=checks,failures=len(fail),max_absolute_uv_error=maxerr,first_failures=fail[:10],scope='Restricted evaluation of all six actual a336 emitted implicit-sample coordinate expressions with varied perspective W, UV and material transform; no GPU derivatives or texture sampling.')
Path('work/pass10-material/uv-emitted-checks.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))

