from pathlib import Path
import numpy as np,csv,json
from PIL import Image
root=Path(__file__).resolve().parent;cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
def frame(name):
 p=next(cap.glob('*'+name+'*attachments.tsv'));prefix=p.name[:-len('_attachments.tsv')];rs=list(csv.DictReader((s for s in p.read_text().splitlines() if not s.startswith('#')),delimiter='\t'))
 light=(cap/(prefix+'_lighting.tsv')).read_text().splitlines();bind={a[4]:a[6] for line in light if (a:=line.split('\t'))[0]=='texture'};target=next(l for l in light if l.startswith('draw\t')).split('\t')[-1];r={k:next(x for x in rs if x['gva']==addr) for k,addr in {'base':bind['2a'],'depth':bind['32'],'light':target}.items()};h,w=int(r['depth']['image_h']),int(r['depth']['image_w']);assert int(r['depth']['vkformat'])==129
 z=(np.fromfile(r['depth']['file'],'<u4').reshape(h,w)&0xffffff)/float(0xffffff);base=np.fromfile(r['base']['file'],'u1').reshape(h,w,4);m=np.fromfile(cap/(prefix+'_light_0_cb4.bin'),'<f4')[48:64].astype('f8').reshape(4,4)
 return z,base,m,r
def compare(cur,prev):
 z,b,m,_=cur;pz,pb,pm,_=prev;h,w=z.shape;y,x=np.mgrid[0:h:4,0:w:4];Z=z[y,x];clip=np.stack(((x+.5)*2/w-1,1-(y+.5)*2/h,Z,np.ones_like(Z)),axis=-1);q=clip@(m@np.linalg.inv(pm));ndc=q[...,:3]/np.where(abs(q[...,3:4])>1e-10,q[...,3:4],np.nan);px=(ndc[...,0]+1)*.5*w-.5;py=(1-ndc[...,1])*.5*h-.5;X=np.clip(np.rint(px),0,w-1).astype(int);Y=np.clip(np.rint(py),0,h-1).astype(int);valid=(Z>0)&(q[...,3]>0)&(px>=0)&(py>=0)&(px<=w-1)&(py<=h-1)&(ndc[...,2]>0)&(ndc[...,2]<=1);err=abs(pz[Y,X]-ndc[...,2]);threshold=np.maximum(2e-5,.015*ndc[...,2]);keep=valid&(err<threshold);col=b[y,x,:3].astype(float);proj=np.abs(col-pb[Y,X,:3]).mean(-1);identity=np.abs(col-pb[y,x,:3]).mean(-1)
 return {'sampled':Z.size,'visible_after_depth_test':int(keep.sum()),'mean_base_rgb_error_0to255_reprojected':float(proj[keep].mean()),'mean_base_rgb_error_0to255_same_pixels_same_mask':float(identity[keep].mean()),'median_motion_pixels':float(np.median(np.hypot(px-x,py-y)[keep]))}
def pink(f):
 r=f[3]['light'];w,h=int(r['image_w']),int(r['image_h']);assert int(r['vkformat'])==122
 v=np.fromfile(r['file'],'<u4').reshape(h,w)[735:820,700:1800];rgb=[]
 for shift,m in [(0,6),(11,6),(22,5)]:
  bits=(v>>shift)&((1<<(m+5))-1);fr=bits&((1<<m)-1);e=(bits>>m).astype(int);rgb.append(np.where(e==0,np.ldexp(fr.astype(float),-14-m),np.ldexp(1+fr/(1<<m),e-15)))
 R,G,B=rgb;return int(((R>1.6*G)&(B>1.6*G)&(G>.03)).sum())
if __name__=='__main__':
 before,close,wide=map(frame,['190711','192104','192112']);result={'tabletop_purple_before':pink(before),'tabletop_purple_candidate':pink(close),'region_pixels':93500,'wide_to_close':compare(wide,close),'close_to_wide':compare(close,wide)}
 print(json.dumps(result,indent=2));(root/'results.json').write_text(json.dumps(result,indent=2));assert result['tabletop_purple_candidate']<result['tabletop_purple_before'];assert all(result[k]['mean_base_rgb_error_0to255_reprojected']<result[k]['mean_base_rgb_error_0to255_same_pixels_same_mask'] for k in ['wide_to_close','close_to_wide'])
