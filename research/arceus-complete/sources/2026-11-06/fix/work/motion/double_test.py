"""Keep the cut and following frames fixed while changing every frame before the cut."""
from pathlib import Path
import json, subprocess
import numpy as np

here=Path(__file__).resolve().parent;p=here/'continuous'
exe=here.parents[1]/'outputs/upscale-runtime/ColdReplay.exe'
seq=[line.split() for line in (p/'DLSS-candidate.txt').read_text().splitlines()]
zero=str(here/'integrated/1920/camera')
for history in ('moving','alternate'):
    rows=[]
    for i,row in enumerate(seq[:12]):
        r=row.copy()
        if history=='alternate' and i<11:r[0]=seq[11][0];r[1]=zero;r[3:5]=['0','0']
        r[2]='-' if i<11 else 'double-'+history+'-0'
        if i==11:r[1]=zero;r[3:5]=['0','0']
        rows.append(r)
    for i in (1,2,3):
        r=rows[-1].copy();r[2]='double-'+history+'-'+str(i);r[-1]='1';rows.append(r)
    path=p/('double-'+history+'.txt')
    path.write_text('\n'.join(' '.join(r) for r in rows)+'\n')
    run=subprocess.run([exe,'DLSS','estimate','1920','1080',path],cwd=p,capture_output=True)
    (p/('double-'+history+'.log')).write_bytes(run.stdout+run.stderr);run.check_returncode()
def load(name):
    image=np.fromfile(p/name,'<f2').reshape(1440,2560,4)[:,:,:3].astype('f4')
    return image/(1+image)
result={}
for i in range(4):
    diff=np.abs(load(f'double-moving-{i}.raw')-load(f'double-alternate-{i}.raw')).max(axis=2)
    result[i]={'mean':float(diff.mean()),'max':float(diff.max()),'pixels_over_0.005':int((diff>.005).sum())}

(p/'double-isolation.json').write_text(json.dumps(result,indent=2));print(result)
