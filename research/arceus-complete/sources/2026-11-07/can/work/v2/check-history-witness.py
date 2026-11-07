from pathlib import Path
import numpy as np
import sys

def analyze(path):
    planes=np.fromfile(path,'<f4').reshape(6,270,480,4)
    raw=planes[1];tags=raw[:,:,3].astype(np.uint32);valid=(tags&0x8000)!=0
    flags=planes[4,:,:,0].astype(np.uint32)
    info=planes[5];depths=planes[4,:,:,2:4]
    print(path.name,valid.sum(),'valid pixels')
    for label,mask in [('all',valid),('glossy',valid&((tags&0x4000)!=0))]:
        print(label,int(mask.sum()))
        if not mask.any():continue
        for i,name in enumerate(('surface','CPU history','positive clip w','in bounds','material ID','previous age','depth','albedo','roughness class','normal','accumulated')):
            print(name,round(100*(((flags&(1<<i))!=0)&mask).sum()/mask.sum(),2),end='; ')
        print()
        rej=mask&((flags&8)!=0)&((flags&1024)==0)
        if rej.any():
            for j,n in enumerate(('relative depth difference','albedo distance','normal cosine','previous age')):
                print(n,np.round(np.quantile(info[:,:,j][rej],[.1,.5,.9]),5))
            print('projected motion',np.round(np.quantile(planes[4,:,:,1][rej],[.1,.5,.9]),4))
            print('expected depth / current depth',np.round(np.quantile(depths[:,:,0][rej]/np.maximum(1.e-9,depths[:,:,1][rej]),[.1,.5,.9]),5))
if __name__=='__main__':
    for arg in sys.argv[1:]:analyze(Path(arg))
