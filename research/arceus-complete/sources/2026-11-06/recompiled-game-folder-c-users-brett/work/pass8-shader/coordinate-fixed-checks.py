import sys
sys.path.pop(0)
from pathlib import Path
src=Path('work/check-screen-fetch-semantics.py').read_text();exec(src[:src.index("fixtures=Path(")])
a=Shader('work/pass8-shader/e9-original.spv');b=Shader('work/pass8-shader/e9-fixed.spv')
checks=0
for slot in range(3):
 for scale in [.5,.75,1.,4/3,1.5,2.,3.]:
  for px in list(range(32))+[127,128,1919,2559]:
   for py in [0,1,2,47,719,1439]:
    expected=(px,py)
    got=b.coordinates(px+.5,py+.5,scale,1<<slot,slot)
    assert got==expected,(slot,scale,px,py,got)
    assert b.coordinates(px+.5,py+.5,scale,0,slot)==a.coordinates(px+.5,py+.5,scale,0,slot)
    assert b.coordinates(px+.5,py+.5,scale,0xffffffff^(1<<slot),slot)==a.coordinates(px+.5,py+.5,scale,0xffffffff^(1<<slot),slot)
    if scale==1:assert got==a.coordinates(px+.5,py+.5,scale,1<<slot,slot)
    checks+=1
row=dict(coordinate_cases=checks,failures=0,old_depth_x=[a.coordinates(x+.5,.5,4/3,4,2)[0] for x in range(16)],fixed_depth_x=[b.coordinates(x+.5,.5,4/3,4,2)[0] for x in range(16)],limits='Restricted expression evaluator executes emitted SPIR-V coordinate arithmetic only; no full shader/GPU execution.')
Path('work/pass8-shader/coordinate-fixed-checks.json').write_text(json.dumps(row,indent=2));print(json.dumps(row,indent=2))
