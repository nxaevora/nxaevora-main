"""Compare paired GPU timestamps across the same offline route, alternating build preferences."""
import re,sys
from pathlib import Path
import numpy as np
def read(path):
    text=path.read_text(errors='replace')
    if 'frames=2208 hash=' not in text or re.search(r'VUID-[A-Za-z]|device lost|vendor dispatch failed',text,re.I):
        raise ValueError(f'Incomplete route or Vulkan error in {path}')
    return {(int(f),int(m)):float(t) for f,m,t in re.findall(r'world-gpu: frame=(\d+) mark=(\d+) us=([\d.]+)',text)}
def summarize(paths):
    runs=[read(p) for p in paths];common=set.intersection(*(set(r) for r in runs))
    for region,lo,hi in [('starter room',445,705),('village',1300,1936)]:
        for label,marks in [('AS build',[4]),('lighting inputs',[5]),('path integration',[6]),('sum of renderer intervals',list(range(2,10)))]:
            frames=[i for i in range(lo,hi+1) if all((i,m) in common for m in marks)]
            if len(frames)<15:raise ValueError('Insufficient aligned samples')
            a=np.array([[sum(r[i,m] for m in marks)/1000 for i in frames] for r in runs])
            build=(a[0]+a[3])/2;trace=(a[1]+a[2])/2;d=trace-build
            rng=np.random.default_rng(442)
            blocks=np.array_split(d,max(4,len(d)//4))
            boot=[np.median(np.concatenate([blocks[k] for k in picks])) for picks in rng.integers(len(blocks),size=(1000,len(blocks)))]
            print(region,label,len(frames),'aligned frames; median ms',round(np.median(build),3),'->',round(np.median(trace),3),
                  'paired delta',round(np.median(d),3),'95% block interval',np.quantile(boot,[.025,.975]).round(3))
if __name__=='__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: check-traversal.py RUN_DIRECTORY')
    root = Path(sys.argv[1])
    summarize([root/('filter-traversal-'+s)/'game.log' for s in ['build','trace','trace2','build2']])
