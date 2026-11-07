from pathlib import Path
import itertools,runpy,numpy as np
from collections import Counter
r=runpy.run_path("work/assets/resident-placement.py"); recover=runpy.run_path("work/assets/placed-structures.py")["recover"]
models=r["tables"]()["ha_area00"];rows=[]
for model,instances in recover().items():
    for inst in instances:
        tr=inst["tr"];source=models.get(model,[])
        if not source:continue
        xyz,rot,scale,cond,desc=min(source,key=lambda v:np.linalg.norm(v[0]-tr[3]))
        if np.max(abs(xyz-tr[3]))>.001:continue
        if np.count_nonzero(abs(np.sin(np.deg2rad(rot)))>.001)<2:continue
        errors={''.join(o):np.max(abs(r["transform"](xyz,rot,scale,o)-tr)) for o in itertools.permutations('xyz')}
        print(model,'angles',rot.round(2),'order errors',{k:round(float(v),5) for k,v in errors.items()},'live captures',len(inst['frames']),flush=True)
        rows.append(errors)
print('distinctive tilted placements',len(rows),flush=True)
