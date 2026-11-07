from pathlib import Path
import numpy as np,sys
from PIL import Image
p=Path(__file__).resolve().parent;sys.path.insert(0,str(p.parent/'taa'));from audit_frames import frame
z,_,_,r=frame('192104')
v=np.fromfile(r['light']['file'],'<u4').reshape(z.shape);rgb=[]
for shift,m in [(0,6),(11,6),(22,5)]:
 bits=(v>>shift)&((1<<(m+5))-1);fr=bits&((1<<m)-1);e=(bits>>m).astype(int);rgb.append(np.where(e==0,np.ldexp(fr.astype(float),-14-m),np.ldexp(1+fr/(1<<m),e-15)).astype('f4'))
rgb=np.nan_to_num(np.stack(rgb,axis=-1),posinf=0); wh=(1920,1080)
lo=np.ones((1080,1920,4),'f2')
for i in range(3):lo[:,:,i]=Image.fromarray(rgb[:,:,i]).resize(wh,Image.Resampling.BOX)
lo.tofile(p/'scene.raw');np.asarray(Image.fromarray(z.astype('f4')).resize(wh,Image.Resampling.NEAREST)).tofile(p/'depth.raw')
if len(sys.argv)>1:
 def tm(a):return (np.maximum(0,a)/(1+np.maximum(0,a)))**(1/2.2)
 ref=tm(rgb);h,w=z.shape
 for backend in ['DLSS','FSR']:
  a=np.fromfile(p/(backend+'.raw'),'<f2').reshape(h,w,4)[:,:,:3].astype('f4');assert np.isfinite(a).all();a=tm(a);err=np.abs(ref-a).mean();corr=np.corrcoef(ref[::4,::4].ravel(),a[::4,::4].ravel())[0,1];print(backend,'tone-mapped RGB mean absolute difference',round(float(err),5),'correlation',round(corr,5));assert err<.08 and corr>.9
