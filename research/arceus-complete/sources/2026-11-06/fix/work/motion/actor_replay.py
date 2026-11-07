from pathlib import Path
import csv,json,subprocess
import numpy as np
from PIL import Image
P=Path(__file__).resolve().parent
DATA=P/'fixtures'
DATA.mkdir(exist_ok=True)
CAP=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
W,H=1920,1080

def captured(stamp):
    manifest=next(CAP.glob('*'+stamp+'*attachments.tsv'))
    rows=list(csv.DictReader((s for s in manifest.read_text().splitlines() if not s.startswith('#')),delimiter='\t'))
    lighting=next(CAP.glob('*'+stamp+'*lighting.tsv')).read_text().splitlines()
    bind={t[4]:t[6] for s in lighting if (t:=s.split('\t'))[0]=='texture'}
    target=next(s for s in lighting if s.startswith('draw\t')).split('\t')[-1]
    def raw(address,dtype,channels=1):
        row=next(r for r in rows if r['gva']==address)
        shape=(int(row['image_h']),int(row['image_w']))+((channels,) if channels>1 else ())
        return np.fromfile(row['file'],dtype).reshape(shape)
    light=raw(target,'<u4')
    rgb=np.ones((H,W,4),'<f2')
    for c,(shift,m) in enumerate(((0,6),(11,6),(22,5))):
        bits=(light>>shift)&((1<<(m+5))-1); mant=bits&((1<<m)-1); exp=(bits>>m).astype('i4')
        values=np.where(exp==0,np.ldexp(mant.astype('f4'),-14-m),np.ldexp(1+mant.astype('f4')/(1<<m),exp-15))
        rgb[:,:,c]=np.asarray(Image.fromarray(values).resize((W,H),Image.Resampling.BOX))
    z=(raw(bind['32'],'<u4')&0xffffff).astype('f4')/0xffffff
    z=np.asarray(Image.fromarray(z).resize((W,H),Image.Resampling.NEAREST))
    material=np.asarray(Image.fromarray(raw(bind['2e'],'u1',4)).resize((W,H),Image.Resampling.NEAREST))
    return rgb,z,material

def ids(material):
    return (material[:,:,2].astype('u2')<<8)+material[:,:,3]

def save(prefix,images):
    for suffix,im in zip(('color','depth','material'),images):
        im.tofile(DATA/(prefix+'.'+suffix))

def shift(image,dx,dy):
    return image[np.clip(np.arange(H)-dy,0,H-1)[:,None],np.clip(np.arange(W)-dx,0,W-1)]

def motion_frame(base,actor,selection,locs,cam=(0,0)):
    scene=[shift(im,*cam) for im in base]
    label=np.zeros((H,W),'u1')
    for part,(mx,my,mask) in enumerate(locs,1):
        dx,dy=-620+mx+cam[0],-100+my+cam[1]
        sy,sx=np.nonzero(mask&selection)
        xx,yy=sx+dx,sy+dy
        valid=(xx>=0)&(xx<W)&(yy>=0)&(yy<H)
        if not valid.all():sx,sy,xx,yy=sx[valid],sy[valid],xx[valid],yy[valid]
        for i in range(3):scene[i][yy,xx]=actor[i][sy,sx]
        label[yy,xx]=part
    return scene,label

def main():
    base=captured('192104');actor=captured('192112');mat=ids(actor[2])
    y,x=np.ogrid[:H,:W]
    selection=(mat>=296)&(mat<=317)&(x>W*.733)&(x<W*.93)&(y>H*.15)
    assert selection.sum()>100000
    print('Foreground pixels',int(selection.sum()),'material ids',np.unique(mat[selection]).tolist(),flush=True)
    shape=np.ones((H,W),bool);start=[(0,0,shape)]
    frames=[];expected={}
    def add(src,out,cam=(0,0),valid=1):
        frames.append(f'{src} {out} {-cam[0]} {-cam[1]} 0 0 {valid}')
    def arrange(name,positions,cam=(0,0),reference=start):
        scene,old=motion_frame(base,actor,selection,reference)
        save('start_'+name,scene);add('start_'+name,'-',valid=0)
        frame,lbl=motion_frame(base,actor,selection,positions,cam)
        save(name,frame);add(name,name,cam)
        old=shift(old,*cam)
        # Compare only where the synthetic object or background is visible now.
        truth=np.zeros((H,W,2),'f4');truth[:]=[-cam[0],-cam[1]]
        for n,((a,b,_),(c,d,_)) in enumerate(zip(reference,positions),1):
            truth[lbl==n]=[a-c-cam[0],b-d-cam[1]]
        expected[name]=(truth,lbl,old)
    arrange('stationary',start)
    arrange('walk',[(12,5,shape)])
    arrange('subtle',[(2,1,shape)])
    arrange('camera_only',start,cam=(8,0))
    arrange('combined',[(12,5,shape)],cam=(8,0))
    arm=(mat==308)|(mat==312)|(mat==313)|(mat==317)
    arrange('articulated',[(12,5,~arm),(4,-2,arm)],reference=[(0,0,~arm),(0,0,arm)])
    arrange('shared_material',[(12,0,shape),(-412,0,shape)],reference=[(0,0,shape),(-400,0,shape)])
    # Keep steady-state time samples separate from initialization.
    for i in range(12):add('walk','-',valid=bool(i) and 1 or 0)
    (DATA/'sequence.txt').write_text('\n'.join(frames)+'\n')
    r=subprocess.run([P/'replay_gpu.exe',str(W),str(H),'sequence.txt'],cwd=DATA,capture_output=True,text=True)
    print(r.stdout,flush=True)
    if r.returncode:raise RuntimeError((r.returncode,r.stderr))
    (DATA/'timings.tsv').write_text(r.stdout)
    metrics={}
    for name,(truth,lbl,old) in expected.items():
        flow=np.fromfile(DATA/(name+'.flow'),'<f2').reshape(H,W,2).astype('f4');assert np.isfinite(flow).all()
        mask=np.fromfile(DATA/(name+'.mask'),'u1').reshape(H,W)/255
        dist=np.linalg.norm(truth-flow,axis=2)
        visible=lbl>0;steady=(lbl==0)&(old==0);revealed=(lbl==0)&(old>0)
        metrics[name]={'actor_mean_endpoint_px':float(dist[visible].mean()),'actor_within_1px':float((dist[visible]<=1).mean()),'actor_within_2px':float((dist[visible]<=2).mean()),'static_false_flow':float((dist[steady]>1).mean()),'newly_revealed_reactive_mean':float(mask[revealed].mean()) if revealed.any() else None,'steady_reactive_mean':float(mask[steady].mean())}
        np.savez_compressed(DATA/(name+'-truth.npz'),truth=truth.astype('f2'),label=lbl,old=old)
    print(json.dumps(metrics,indent=2));(DATA/'metrics.json').write_text(json.dumps(metrics,indent=2))
    for name,item in metrics.items():
        assert item['actor_within_1px']>.985,(name,item)
        assert item['static_false_flow']<.00015,(name,item)
    for name in ('stationary','camera_only'):
        assert metrics[name]['actor_mean_endpoint_px']==0

if __name__=='__main__':
    main()
