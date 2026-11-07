"""Compare recorded single-sample and filtered HDR luminance against a frozen path-tracing reference."""
import sys
from pathlib import Path
import numpy as np

WIDTH, HEIGHT, SAMPLES = 480, 270, 128
LUMA = np.array([.2126, .7152, .0722])

def summarize(path):
    data=np.fromfile(path,'<f4')
    pixels=WIDTH*HEIGHT*4
    if data.size not in (3*pixels,4*pixels,6*pixels) or not np.isfinite(data).all():
        raise ValueError(f'Invalid quality capture {path}')
    planes=data.reshape(-1,HEIGHT,WIDTH,4).astype(float)
    accum,raw,candidate=planes[:3]
    baseline=planes[3] if len(planes)>=4 else None
    tag=raw[:,:,3].astype('u4')
    valid=(tag&0x8000)!=0
    if valid.sum()<10000:raise ValueError(f'Insufficient reference surfaces {path}')
    target=(accum[:,:,:3]@LUMA)/SAMPLES
    variance=np.maximum(0,accum[:,:,3]/SAMPLES-target**2)
    scale=target+.05
    def error(rgb):return (rgb[:,:,:3]@LUMA-target)/scale
    signals={'raw':error(raw),'candidate':error(candidate)}
    if baseline is not None:
        if (baseline[valid,3]>0).mean()<.99:raise ValueError('Baseline history missing')
        signals['baseline']=error(baseline)
    masks={'surfaces':valid,'diffuse':valid&((tag&0x4000)==0),'glossy':valid&((tag&0x4000)!=0),
           'reprojected':valid&(candidate[:,:,3]>1),'reset':valid&(candidate[:,:,3]<=1)}
    print(path)
    for name,mask in masks.items():
        if mask.sum()<50:continue
        print(name,int(mask.sum()),'reference SE',round((np.sqrt(variance[mask]/SAMPLES)/scale[mask]).mean(),4),
              'mean history',round(candidate[mask,3].mean(),2))
        for label,signal in signals.items():
            e=signal[mask]
            print(' ',label,'rel RMSE',round(np.mean(e*e)**.5,5),'MAE',round(abs(e).mean(),5),'bias',round(e.mean(),5))
        if baseline is not None:
            # Spatial block bootstrap preserves within-tile correlation when comparing the exact same pixels.
            loss=signals['candidate']**2-signals['baseline']**2
            tiles=[];counts=[]
            for y in range(0,HEIGHT,12):
                for x in range(0,WIDTH,12):
                    m=mask[y:y+12,x:x+12]
                    if m.any():
                        tiles.append(loss[y:y+12,x:x+12][m].sum());counts.append(m.sum())
            rng=np.random.default_rng(42)
            picks=rng.integers(len(tiles),size=(1000,len(tiles)))
            boot=np.array(tiles)[picks].sum(1)/np.array(counts)[picks].sum(1)
            print('  paired delta MSE',round(loss[mask].mean(),6),'95% tile interval',np.quantile(boot,[.025,.975]).round(6))
for path in map(Path,sys.argv[1:]):summarize(path)
