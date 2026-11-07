from pathlib import Path
import sys
import numpy as np
from PIL import Image

root=Path(__file__).resolve().parent
def image_at(path):
    with Image.open(path) as image:
        return np.asarray(image.convert('L'),np.float32)

def patch_sum(values,side):
    prefix=np.pad(values.cumsum(0).cumsum(1),((1,0),(1,0)))
    return prefix[side:,side:]-prefix[:-side,side:]-prefix[side:,:-side]+prefix[:-side,:-side]

def track(current,previous,x,y,side,radius):
    ref=current[y:y+side,x:x+side]
    search=previous[y-radius:y+side+radius,x-radius:x+side+radius]
    if search.shape!=(side+2*radius,)*2 or ref.std()<4:return None
    ref=ref-ref.mean()
    shape=search.shape
    spectrum=np.fft.rfft2(search)*np.conj(np.fft.rfft2(ref,s=shape,axes=(0,1)))
    product=np.fft.irfft2(spectrum,s=shape,axes=(0,1))[:2*radius+1,:2*radius+1]
    energy=np.maximum(1,patch_sum(search**2,side)-patch_sum(search,side)**2/side**2)
    corr=product/np.sqrt((ref**2).sum()*energy)
    iy,ix=np.unravel_index(np.argmax(corr),corr.shape)
    if min(ix,iy,2*radius-ix,2*radius-iy)<1 or corr[iy,ix]<.90:return None
    def peak(a,b,c):
        return float(np.clip(.5*(a-c)/min(a-2*b+c,-1e-5),-.5,.5))
    return (ix-radius+peak(corr[iy,ix-1],corr[iy,ix],corr[iy,ix+1]),
            iy-radius+peak(corr[iy-1,ix],corr[iy,ix],corr[iy+1,ix]),corr[iy,ix])

for label in sys.argv[1:]:
    files=sorted((root/('filter-'+label)).glob('screen_*.ppm'))
    residual=[];speed=[];pairs=0;old_index=-2
    for file in files:
        idx=int(file.stem.split('_')[-1]);image=image_at(file)
        # One in four pixels for robust landmark tracking. Measure residual at native 4K.
        small=image.reshape(540,4,960,4).mean(axis=(1,3))
        if idx==old_index+1:
            pairs+=1
            for y in (140,210,290):
                for x in (150,280,420,560,700):
                    match=track(small,old_small,x,y,24,52)
                    if not match:continue
                    dx,dy,_=match
                    if np.hypot(dx,dy)<.75:continue
                    px=4*x;py=4*y;qx=px+4*dx;qy=py+4*dy
                    a=int(np.floor(qx));b=int(np.floor(qy));fx=qx-a;fy=qy-b
                    p=image[py:py+96,px:px+96]
                    q=old_image[b:b+96,a:a+96]*(1-fx)*(1-fy)+old_image[b:b+96,a+1:a+97]*fx*(1-fy)
                    q+=old_image[b+1:b+97,a:a+96]*(1-fx)*fy+old_image[b+1:b+97,a+1:a+97]*fx*fy
                    # Drop local exposure shifts. Perspective and animation can still contribute.
                    delta=p-q
                    residual.append(np.abs(delta-np.median(delta)).mean())
                    speed.append(np.hypot(dx,dy)*4)
        old_index,old_image,old_small=idx,image,small
    if residual:
        print(label,'consecutive pairs',pairs,'tracked moving patches',len(speed),'median motion px',round(np.median(speed),1),
              'median 4K luma residual',round(np.median(residual),2),'p90',round(np.percentile(residual,90),2))
    else: print(label,'no confidently tracked moving patches from',pairs,'consecutive pairs')
