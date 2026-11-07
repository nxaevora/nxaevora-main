from pathlib import Path
import numpy as np
from PIL import Image
v2=Path('work/v2')
for file in v2.glob('*-samples.npz'):
    data=np.load(file);p=data['p'];c=data['c'];period=data['uv'][0]
    p=p[:,[0,2]]
    c=np.log(np.maximum(.005,c)).mean(axis=1)
    # Remove broad material tints and lighting before testing local texture features.
    cell=np.floor(p/3).astype(int);_,inv,cnt=np.unique(cell,axis=0,return_inverse=True,return_counts=True)
    keep=cnt[inv]>8
    c=c-np.bincount(inv,weights=c)[inv]/cnt[inv]
    p,c=p[keep],c[keep]
    model=np.asarray(Image.fromarray(data['image'].astype('u1')).resize((32,32),Image.Resampling.BOX)).mean(axis=2)
    model=(model-model.mean())/max(model.std(),.01)
    best=[];fold=(np.floor(p[:,0]/2).astype(int)+np.floor(p[:,1]/2).astype(int))%3
    for factor in (.25,.5,1,2,4,8,16,32,64,128,256,512,768,1000,1024,1025,1536,2048):
        uv=p*factor/(1025*period)
        for sy in (1,-1):
            for sx in (1,-1):
                a=np.floor((uv*np.array([sx,sy]))*32).astype(int)%32
                for swap in (False,True):
                    x,y=(a.T[::-1] if swap else a.T)
                    k=x+y*32
                    train=fold!=0
                    flat=np.bincount(k[train],weights=c[train],minlength=1024).reshape(32,32)
                    cov=np.fft.ifft2(np.conj(np.fft.fft2(flat))*np.fft.fft2(model)).real
                    iy,ix=np.unravel_index(cov.argmax(),cov.shape)
                    pred=model[(y+iy)%32,(x+ix)%32]
                    def corr(sel):
                        a,b=pred[sel],c[sel]
                        return np.mean((a-a.mean())*(b-b.mean()))/max(.00001,a.std()*b.std())
                    best.append((round(corr(~train),3),round(corr(train),3),factor,sx,sy,swap,ix,iy))
    print(file.stem,len(c),'best',sorted(best,reverse=True)[:3])
