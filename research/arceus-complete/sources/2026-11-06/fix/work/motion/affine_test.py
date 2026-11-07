"""Evaluate nonuniform screen motion and crossing a stationary foreground occluder."""
from pathlib import Path
import json, subprocess
import numpy as np
from fractional_replay import here, frame, fg, base, label, W, H

out=here/'deformation';out.mkdir(exist_ok=True)
y,x=np.mgrid[:H,:W].astype('f4')
cx=x[label].mean();cy=y[label].mean()
def render(angle,scale,translation,blocked):
    # Current screen pixel mapped to its location in the previous character pose.
    a=np.deg2rad(angle);co,si=np.cos(a)/scale,np.sin(a)/scale
    dx=x-cx-translation[0];dy=y-cy-translation[1]
    oldx=co*dx+si*dy+cx;oldy=-si*dx+co*dy+cy
    nx=np.floor(oldx+.5).astype(int).clip(0,W-1);ny=np.floor(oldy+.5).astype(int).clip(0,H-1)
    visible=label[ny,nx]&(oldx>=0)&(oldx<W)&(oldy>=0)&(oldy<H)
    result=[b.copy() for b in base]
    X=oldx[visible].clip(0,W-1);Y=oldy[visible].clip(0,H-1)
    x0=X.astype(int);y0=Y.astype(int);fx=(X-x0)[:,None];fy=(Y-y0)[:,None]
    x1=np.minimum(x0+1,W-1);y1=np.minimum(y0+1,H-1)
    result[0][visible]=((1-fy)*((1-fx)*fg[0][y0,x0]+fx*fg[0][y0,x1])+fy*((1-fx)*fg[0][y1,x0]+fx*fg[0][y1,x1])).astype('f2')
    for i in (1,2):result[i][visible]=fg[i][ny[visible],nx[visible]]
    occluder=(x>cx-22)&(x<cx+28)&(y>cy-160)&(y<cy+205) if blocked else np.zeros((H,W),bool)
    if blocked:
        result[0][occluder]=[.07,.06,.04,1];result[1][occluder]=.92;result[2][occluder]=[0,0,3,212]
        visible[occluder]=False
    truth=np.zeros((H,W,2),'f4');truth[visible]=np.stack((oldx[visible]-x[visible],oldy[visible]-y[visible]),axis=-1)
    return result,visible,truth,occluder
def save(stem,images):
    for suffix,image in zip(('color','depth','material'),images):image.tofile(out/(stem+'.'+suffix))
cases={'turn':(5,1.,(8,4),False),'fast_turn':(12,1.,(4,2),False),'approach':(0,1.06,(8,-2),False),
       'occluded_walk':(0,1.,(16,5),True),'occluded_turn':(6,1.04,(12,6),True)}
metrics={};expected={};lines=[]
for name,(angle,scale,move,blocked) in cases.items():
    previous,old,_,_=render(0,1,(0,0),blocked)
    current,visible,truth,occluder=render(angle,scale,move,blocked)
    save('start_'+name,previous);save(name,current)
    lines.extend([f'start_{name} - 0 0 0 0 0',f'{name} {name} 0 0 0 0 1'])
    expected[name]=(old,visible,truth,occluder)
(out/'sequence.txt').write_text('\n'.join(lines)+'\n')
run=subprocess.run([here/'replay_gpu.exe',str(W),str(H),'sequence.txt'],cwd=out,capture_output=True)
run.check_returncode()
for name,(old,visible,truth,occluder) in expected.items():
    motion=np.fromfile(out/(name+'.flow'),'<f2').reshape(H,W,2).astype('f4')
    reactive=np.fromfile(out/(name+'.mask'),'u1').reshape(H,W)/255
    error=np.linalg.norm(motion-truth,axis=2);baseline=np.linalg.norm(truth,axis=2)
    static=~(old|visible);exposed=old&~visible
    metrics[name]={'camera_mean_error':float(baseline[visible].mean()),'estimate_mean_error':float(error[visible].mean()),
        'within_two_px':float((error[visible]<=2).mean()),'static_false_fraction':float((error[static]>1).mean()),
        'occluder_false_fraction':float((error[occluder]>1).mean()) if occluder.any() else None,
        'newly_revealed_reactive':float(reactive[exposed].mean())}
    assert metrics[name]['estimate_mean_error']<metrics[name]['camera_mean_error']
    assert metrics[name]['occluder_false_fraction'] in (None,0.)
print(json.dumps(metrics,indent=2));(out/'metrics.json').write_text(json.dumps(metrics,indent=2))
