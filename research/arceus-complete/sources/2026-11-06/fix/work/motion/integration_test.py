"""Run the material pass and temporal consumer on one real Vulkan command buffer."""
from pathlib import Path
import json, os, subprocess
import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
data = here/'fixtures'
out = here/'integrated'
out.mkdir(exist_ok=True)
exe = here.parents[1]/'outputs/upscale-runtime/TemporalReplay.exe'
cases = ('stationary','walk','articulated','shared_material')
metrics = {}
def nearest(a,size):
    if a.shape[:2]==(size[1],size[0]):return a
    x=np.minimum((np.arange(size[0])+.5)*a.shape[1]/size[0],a.shape[1]-1).astype(int)
    y=np.minimum((np.arange(size[1])+.5)*a.shape[0]/size[1],a.shape[0]-1).astype(int)
    return a[y[:,None],x]
def display(path):
    image=np.fromfile(path,'<f2').reshape(1440,2560,4)[:,:,:3].astype('f4')
    assert np.isfinite(image).all(),path
    return np.maximum(image,0)/(1+np.maximum(image,0))
for backend in ('TAA','FSR','DLSS'):
    size=(2560,1440) if backend=='TAA' else (1920,1080)
    w,h=size
    fixtures=out / str(w)
    fixtures.mkdir(exist_ok=True)
    zeros=fixtures/'camera.flow'
    np.zeros((h,w,2),'<f2').tofile(zeros)
    for case in cases:
        truth=np.load(data/(case+'-truth.npz'))['truth']
        (nearest(truth,size)*(w/1920)).astype('<f2').tofile(fixtures/(case+'-oracle.flow'))
        if w==1920:continue
        for stem in ['start_'+case,case]:
            for suffix,dt,channels in [('color','<f2',4),('depth','<f4',1),('material','u1',4)]:
                target=fixtures/(stem+'.'+suffix)
                source=data/(stem+'.'+suffix)
                if target.exists() and target.stat().st_mtime>=source.stat().st_mtime:continue
                inp=np.fromfile(source,dt).reshape(1080,1920,channels)
                nearest(inp,size).tofile(target)
    for mode in ('oracle','camera','estimate','mask','cold'):
        lines=[]
        for case in cases:
            src=fixtures if w==2560 else data
            # All processes get identical scene and frame time. Reset between cases.
            # The last independent reset provides a current-frame comparison for exposed pixels.
            zero=str(zeros.with_suffix(''))
            oracle=str(fixtures/(case+'-oracle'))
            lines += [f'{src/("start_"+case)} {zero} - 0 0 0 0 0',f'{src/("start_"+case)} {zero} - 0 0 0 0 1',
                      f'{src/case} {oracle if mode=="oracle" else zero} {backend}-{case}-{mode} 0 0 0 0 {int(mode!="cold")}']
        sequence=out/(backend+'-'+mode+'.txt')
        sequence.write_text('\n'.join(lines)+'\n')
        env=os.environ.copy()
        if mode==('estimate' if backend=='DLSS' else 'mask'):env['ARCEUS_REPLAY_SNAPSHOT']=f'{backend}-walk-{mode}'
        run=subprocess.run([exe,backend,mode if mode in ('estimate','mask') else 'supplied',str(w),str(h),sequence],
            cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(backend+'-'+mode+'.log')).write_bytes(run.stdout)
        run.check_returncode()
    for case in cases:
        truth=np.load(data/(case+'-truth.npz'))
        actor=nearest(truth['label']>0,(2560,1440))
        exposed=~actor & nearest(truth['old']>0,(2560,1440))
        steady=~(actor|exposed)
        def result(mode):return display(out/f'{backend}-{case}-{mode}.raw')
        oracle=result('oracle');cold=result('cold')
        entry={}
        for mode in ('camera','estimate','mask'):
            value=result(mode)
            err=np.abs(value-oracle).mean(axis=2)
            coldErr=np.abs(value-cold).mean(axis=2)
            entry[mode]={'actor_oracle':float(err[actor].mean()),'steady_oracle':float(err[steady].mean()),
                'revealed_oracle':float(err[exposed].mean()) if exposed.any() else None,
                'revealed_cold':float(coldErr[exposed].mean()) if exposed.any() else None}
        metrics[backend+'-'+case]=entry
        print(backend,case,entry,flush=True)
        (out/'metrics.json').write_text(json.dumps(metrics,indent=2))
        if case=='stationary':
            assert entry['estimate']['actor_oracle']<.00001,entry
        else:
            assert entry['estimate']['actor_oracle']<entry['camera']['actor_oracle'],entry
