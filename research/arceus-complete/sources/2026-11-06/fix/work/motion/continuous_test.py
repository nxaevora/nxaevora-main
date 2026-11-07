"""Exercise accumulation, stopping, reversal and an explicit cut over a jitter cycle."""
from pathlib import Path
import json, subprocess
import numpy as np
from fractional_replay import frame

here=Path(__file__).resolve().parent
exe=here.parents[1]/'outputs/upscale-runtime/TemporalReplay.exe'
out=here/'continuous';out.mkdir(exist_ok=True)
def nearest(a,w,h):
    if a.shape[:2]==(h,w):return a
    x=np.minimum(((np.arange(w)+.5)*a.shape[1]/w).astype(int),a.shape[1]-1)
    y=np.minimum(((np.arange(h)+.5)*a.shape[0]/h).astype(int),a.shape[0]-1)
    return a[y[:,None],x]
def halton(index,base):
    scale=1;value=0
    while index:
        scale/=base;value+=(index%base)*scale;index//=base
    return value-.5
poses=[(0,0),(3.6,1.5),(8.4,3.1),(13.7,4.5),(18.9,5.1),(22.3,5.5),(22.3,5.5),
       (22.3,5.5),(18.4,3.4),(11.8,1.5),(5.1,-.4),(128,-24)]
selected={3,5,7,8,10,11}
positions=[];labels={}
for index,pose in enumerate(poses):
    camera=np.array([.65*index,-.22*index]);jitter=np.array([halton(1+index,2),halton(1+index,3)])
    scene,actor=frame(pose,camera,jitter);labels[index]=nearest(actor,2560,1440)
    prevCam=positions[-1][0] if positions else camera
    prevPose=np.array(poses[max(0,index-1)])
    cameraDelta=prevCam-camera
    positions.append((camera,jitter,cameraDelta))
    for w,h in ((1920,1080),(2560,1440)):
        folder=out/str(w);folder.mkdir(exist_ok=True)
        for a,suffix in zip(scene,('color','depth','material')):nearest(a,w,h).tofile(folder/f'{index}.{suffix}')
        motion=np.zeros((h,w,2),'<f2');motion[:]=cameraDelta*(w/1920)
        motion[nearest(scene[1],w,h)<=0]=0;motion.tofile(folder/f'{index}-camera.flow')
        motion[nearest(actor,w,h)]=(cameraDelta+prevPose-pose)*(w/1920)
        motion.tofile(folder/f'{index}-oracle.flow')
def display(path):
    a=np.fromfile(path,'<f2').reshape(1440,2560,4)[:,:,:3].astype('f4')
    assert np.isfinite(a).all()
    return np.maximum(a,0)/(1+np.maximum(a,0))
metrics={}
for backend in ('TAA','FSR','DLSS'):
    w,h=(2560,1440) if backend=='TAA' else (1920,1080)
    folder=out/str(w);ratio=w/1920
    for name,mode in [('camera','supplied'),('oracle','supplied'),('candidate','estimate' if backend=='DLSS' else 'mask')]:
        lines=[]
        for i,(_,jitter,cam) in enumerate(positions):
            dx,dy=cam*ratio;jx,jy=jitter*ratio
            motion='oracle' if name=='oracle' else 'camera'
            prefix=f'{backend}-{i}-{name}' if i in selected else '-'
            lines.append(f'{folder/str(i)} {folder}/{i}-{motion} {prefix} {dx} {dy} {jx} {jy} {int(i not in (0,11))}')
        # A second reset on the identical final input tests reset independently of the previous sequence.
        lines.append(lines[-1].replace(f'{backend}-11-{name}',f'{backend}-cold-{name}'))
        sequence=out/f'{backend}-{name}.txt';sequence.write_text('\n'.join(lines)+'\n')
        proc=subprocess.run([exe,backend,mode,str(w),str(h),sequence],cwd=out,capture_output=True)
        (out/f'{backend}-{name}.log').write_bytes(proc.stdout+proc.stderr);proc.check_returncode()
    results={}
    for i in sorted(selected):
        oracle=display(out/f'{backend}-{i}-oracle.raw')
        entry={}
        for name in ('camera','candidate'):
            value=display(out/f'{backend}-{i}-{name}.raw')
            pixel=np.abs(value-oracle).mean(axis=2)
            entry[name]={'actor':float(pixel[labels[i]].mean()),'background':float(pixel[~labels[i]].mean())}
        results[str(i)]=entry
    cut=display(out/f'{backend}-11-candidate.raw');fresh=display(out/f'{backend}-cold-candidate.raw')
    results['reset_max_error']=float(np.abs(cut-fresh).max())
    results['mean_actor_error']={name:float(np.mean([results[str(i)][name]['actor'] for i in sorted(selected-{11})])) for name in ('candidate','camera')}
    print(backend,results['mean_actor_error'],results['reset_max_error'],flush=True)
    metrics[backend]=results;(out/'metrics.json').write_text(json.dumps(metrics,indent=2))
    assert results['mean_actor_error']['candidate']<results['mean_actor_error']['camera']
    assert results['reset_max_error']<.001

