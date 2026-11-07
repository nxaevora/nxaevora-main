from pathlib import Path
import json, subprocess
import numpy as np
here=Path(__file__).resolve().parent
results={}
for w,h,path,extra in [(1920,1080,here/'fixtures',[]),(2560,1440,here/'integrated/2560',['native'])]:
    f=path/'timed-sequence.txt'
    frames=['start_walk - 0 0 0 0 0']
    for i in range(48):frames.append(('walk' if i%2==0 else 'start_walk')+' - 0 0 0 0 1')
    f.write_text('\n'.join(frames)+'\n')
    run=subprocess.run([here/'replay_gpu.exe',str(w),str(h),f,*extra],cwd=path,capture_output=True,text=True)
    run.check_returncode()
    values=[float(line.split('\t')[3]) for line in run.stdout.splitlines() if line.startswith('frame\t')][9:]
    assert len(values)==40
    results[str(w)]={'samples':len(values),'median_gpu_ms':float(np.median(values)), 'p95_gpu_ms':float(np.percentile(values,95))}
    print(w,results[str(w)],flush=True)
(here/'kernel-timing.json').write_text(json.dumps(results,indent=2))
