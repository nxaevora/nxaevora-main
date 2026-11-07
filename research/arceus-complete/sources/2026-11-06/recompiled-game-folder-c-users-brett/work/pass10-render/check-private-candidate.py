import sys
sys.path.pop(0)
from pathlib import Path
import json,collections,math
root=Path('work/pass10-render')
evaluator=Path('work/check-screen-fetch-semantics.py').read_text().split('fixtures=Path(')[0]
evaluator=evaluator.replace("if a[0]==3:return float(math.trunc(vs[0]))", "if a[0]==3:return float(math.trunc(vs[0]))\n                if a[0]==4:return abs(vs[0])")
evaluator=evaluator.replace("if op==190:return v[0]>=v[1]", "if op==190:return v[0]>=v[1]\n            if op==184:return v[0]<v[1]")
exec(evaluator)
comparisons={}
for local,old in [('shipped-on','shipped-on'),('shipped-off','shipped-off'),('cached-on','cached-replay-on'),('cached-off','cached-replay-off')]:
 a={p.name:p.read_bytes() for p in(root/local).glob('*.spv')};b={p.name:p.read_bytes() for p in(Path('work/pass8-final-corpus')/old).glob('*.spv')}
 assert a.keys()==b.keys()
 comparisons[local]=dict(total=len(a),changed=[n for n in a if a[n]!=b[n]])
fixture_same=[]
for p in Path('work/pass8-integration/fixtures').glob('*.spv'):
 q=root/'fixtures'/p.name
 assert q.read_bytes()==p.read_bytes(),p
 fixture_same.append(p.name)
old=Shader(root/'ArceusTest.vkshipped-1d0676ab12f2bfeb.spv')
new=Shader(root/'shipped-on/1d0676ab12f2bfeb_4.spv')
checks=0;rows=[]
for scale,w,h in [(1.,960,540),(4/3,1280,720),(1.5,1440,810),(2.,1920,1080)]:
 deviations=collections.Counter();axis_bad=[]
 for axis,extent in [(0,w),(1,h)]:
  bad=0
  for p in range(extent):
   x,y=(p+.5,10.5) if axis==0 else (10.5,p+.5)
   a=old.coordinates(x,y,scale,3,0);b=new.coordinates(x,y,scale,3,0)
   expect=(math.floor(2*x),math.floor(2*y));assert b==expect
   assert new.coordinates(x,y,scale,2,0)==old.coordinates(x,y,scale,2,0)
   assert new.coordinates(x,y,scale,3,1)==old.coordinates(x,y,scale,3,1)
   if scale==1:assert a==b
   delta=a[axis]-b[axis];deviations[str(delta)]+=1;bad+=delta!=0;checks+=3+(scale==1)
  axis_bad.append(bad)
 rows.append(dict(scale=scale,viewport=[w,h],old_per_axis_error_histogram=dict(deviations),old_wrong_x=axis_bad[0],old_wrong_y=axis_bad[1],old_pixels_wrong_either=axis_bad[0]*h+axis_bad[1]*w-axis_bad[0]*axis_bad[1],pixels=w*h,new_errors=0))
# New synthetic changed fixtures: mathematical2x map across arbitrary selected
# positive framebuffer pixels and scales; mask-off must preserve emitter.
for name in ['twice-tld','twice-tlds','twice-float-moves','twice-integer-moves']:
 a=Shader(root/'fixtures'/(name+'-0.spv'));b=Shader(root/'fixtures'/(name+'-1.spv'))
 for scale in [.5,.75,1,4/3,1.5,2,3]:
  for x in [0,1,2,3,7,31,127,959,1279]:
   for y in [0,1,2,17,539,719]:
    p=(x+.5,y+.5);assert b.coordinates(*p,scale,1)==(2*x+1,2*y+1)
    assert b.coordinates(*p,scale,0)==a.coordinates(*p,scale,0)
    checks+=2
result=dict(comparisons=comparisons,old_fixture_spv_byte_identical=len(fixture_same),coordinate_assertions=checks,actual_shader_coordinate_results=rows,scope='Restricted evaluation of emitted SPIR-V coordinate graph; not full shader execution/GPU validation or visual proof.')
(root/'candidate-checks.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
