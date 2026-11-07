from pathlib import Path
import json, subprocess
import numpy as np
from PIL import Image
from actor_replay import captured, W, H

here=Path(__file__).resolve().parent
fixtures=here/'fixtures'
root=here/'fractional'
root.mkdir(exist_ok=True)
cases={
    'jitter_only':((0,0),(0,0),(.375,-.3889)),
    'subpixel_actor':((2.35,-.65),(0,0),(.375,-.3889)),
    'walk_jitter':((12,5),(0,0),(-.25,.2778)),
    'camera_fractional':((0,0),(6.25,-2.5),(-.375,.0556)),
    'camera_actor':((7.3,-3.7),(6.25,-2.5),(-.375,.0556)),
    'long_step':((45,-13),(0,0),(0,0)),
    'teleport':((160,0),(0,0),(0,0)),
}
def shift(a,x,y,linear):
    # Edge replication only affects image borders, excluded from the controls.
    xp=np.clip(np.arange(W)-x,0,W-1);yp=np.clip(np.arange(H)-y,0,H-1)
    if not linear:return a[np.floor(yp+.5).astype(int)[:,None],np.floor(xp+.5).astype(int)]
    x0=xp.astype(int);y0=yp.astype(int);fx=(xp-x0).astype('f4')[None,:,None];fy=(yp-y0).astype('f4')[:,None,None]
    x1=np.minimum(x0+1,W-1);y1=np.minimum(y0+1,H-1)
    line0=a[y0[:,None],x0]*(1-fx)+a[y0[:,None],x1]*fx
    line1=a[y1[:,None],x0]*(1-fx)+a[y1[:,None],x1]*fx
    return (line0*(1-fy)+line1*fy).astype(a.dtype)
def write_images(name,images):
    for a,ext in zip(images,('color','depth','material')):a.tofile(root/(name+'.'+ext))
base=captured('192104')
fg=[np.fromfile(fixtures/('start_stationary.'+ext),dt).reshape(shape) for ext,dt,shape in
    [('color','<f2',(H,W,4)),('depth','<f4',(H,W)),('material','u1',(H,W,4))]]
label=np.load(fixtures/'stationary-truth.npz')['label']!=0
previous_jitter=(-.25,.1667)
def frame(motion,camera,jitter):
    bgShift=np.array(camera)+jitter;fgShift=bgShift+motion
    current=shift(label,*fgShift,False)
    background=[shift(a,*bgShift,i==0) for i,a in enumerate(base)]
    for i in range(3):
        moved=shift(fg[i],*fgShift,i==0)
        background[i][current]=moved[current]
    return background,current
def main():
    src,old=frame((0,0),(0,0),previous_jitter)
    write_images('previous',src)
    lines=[];regions={}
    for name,(motion,camera,jitter) in cases.items():
        scene,actor=frame(motion,camera,jitter);write_images(name,scene)
        lines.extend([f'previous - 0 0 {previous_jitter[0]} {previous_jitter[1]} 0',
                      f'{name} {name} {-camera[0]} {-camera[1]} {jitter[0]} {jitter[1]} 1'])
        oldHere=shift(old,*(np.array(camera)+jitter-previous_jitter),False)
        steady=~(actor|oldHere)
        steady[:10]=False;steady[-10:]=False;steady[:,:10]=False;steady[:,-10:]=False
        regions[name]=(actor,steady,oldHere&~actor)
    (root/'sequence.txt').write_text('\n'.join(lines)+'\n')
    run=subprocess.run([here/'replay_gpu.exe',str(W),str(H),'sequence.txt'],cwd=root,capture_output=True)
    run.check_returncode();(root/'timings.tsv').write_bytes(run.stdout)
    metrics={}
    for name,(motion,camera,jitter) in cases.items():
        actor,steady,exposed=regions[name]
        flow=np.fromfile(root/(name+'.flow'),'<f2').reshape(H,W,2).astype('f4')
        mask=np.fromfile(root/(name+'.mask'),'u1').reshape(H,W)/255
        assert np.isfinite(flow).all()
        error=np.linalg.norm(flow+np.array(camera)+motion,axis=2)
        bg_error=np.linalg.norm(flow+np.array(camera),axis=2)
        metrics[name]={'mean_actor_error':float(error[actor].mean()),'within_one':float((error[actor]<=1).mean()),
           'false_static':float((bg_error[steady]>1).mean()),'dynamic_estimate_fraction':float((bg_error[actor]>1).mean()),
           'reactive_actor':float(mask[actor].mean()),'reactive_exposed':float(mask[exposed].mean()) if exposed.any() else None}
    (root/'metrics.json').write_text(json.dumps(metrics,indent=2))
    print(json.dumps(metrics,indent=2))
    assert all(v['false_static']<.0001 for v in metrics.values())
    for control in ('jitter_only','camera_fractional'):
        assert metrics[control]['mean_actor_error']<.001
    assert metrics['subpixel_actor']['within_one']>.97
    assert metrics['teleport']['dynamic_estimate_fraction']==0
    assert metrics['teleport']['reactive_actor']>.7

if __name__=='__main__':
    main()
