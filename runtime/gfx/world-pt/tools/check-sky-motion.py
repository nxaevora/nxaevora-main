from pathlib import Path
import re,sys
import numpy as np

prefix=Path(sys.argv[1])
data={line[0]:line[1:] for line in (row.split('\t') for row in Path(str(prefix)+'_temporal.tsv').read_text().splitlines())}
assert data['status']==['ok'] and data['history_valid']==['1']
w,h=map(int,data['size'])
matrix=np.array(data['clip_to_previous'],np.float64).reshape(4,4)
jitter=np.array(data['jitter_current_previous'][:2],np.float64)
log=Path(sys.argv[2]).read_text(errors='replace')
match=re.search(r'arceus-qa: sky depth ([\deE+.-]+) at frame (\d+)',log)
assert match and int(match[2])==int(data['source_frame'][0])
sky_z=float(match[1])
features=np.fromfile(str(prefix)+'_temporal_current_features_rg32ui.bin','<u4').reshape(h,w,2)
motion=np.fromfile(str(prefix)+'_temporal_motion_rg16f.bin','<f2').reshape(h,w,2)
y,x=np.nonzero((features[:h//3,:,1]&0xfffff)==0)
if len(x)<3000:raise ValueError(f'Only {len(x)} sky pixels in the upper third')
clip=np.stack(((x+.5-jitter[0])/w*2-1,1-(y+.5-jitter[1])/h*2),axis=1)
projected=np.column_stack((clip,np.full(len(x),sky_z),np.ones(len(x))))@matrix
valid=projected[:,3]>0
expected=np.clip((projected[valid,:2]/projected[valid,3,None]-clip[valid])*[w*.5,-h*.5],[-w,-h],[w,h])
actual=motion[y[valid],x[valid]].astype(np.float32)
speed=np.linalg.norm(expected,axis=1)
error=np.linalg.norm(actual-expected,axis=1)
print('Sky pixels',len(actual),'expected median motion (px)',np.median(speed),'median error',np.median(error),'99th percentile error',np.percentile(error,99))
if np.median(speed)<1 or np.percentile(error,99)>max(.2,.002*np.median(speed)):
    raise ValueError('Camera rotation or sky motion does not satisfy the comparison')
