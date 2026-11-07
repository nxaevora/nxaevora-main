"""Inspect a temporal F3 capture. All calculations stay on the local machine."""
import argparse, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

def inspect(manifest, preview=None, title=None):
    manifest=Path(manifest)
    rows=[line.split('\t') for line in manifest.read_text().splitlines()]
    meta={r[0]:r[1:] for r in rows if r[0]!='image'}
    if meta['status']!=['ok']:
        raise ValueError(f"{manifest.name}: {meta['status']}")
    w,h=map(int,meta['size'])
    if w<=0 or h<=0 or w>8192 or h>8192:raise ValueError('invalid image size')
    prefix=str(manifest)[:-len('_temporal.tsv')]
    files={}
    for _,name,count,status in (r for r in rows if r[0]=='image'):
        path=Path(prefix+'_temporal_'+name+'.bin')
        if status!='ok' or path.stat().st_size!=int(count):raise ValueError(f'incomplete {name}')
        files[name]=path
    result={'backend':meta['backend'][0],'mode':meta['mode'][0],'source_frame':int(meta['source_frame'][0]),'size':[w,h]}
    if 'motion_rg16f' not in files:return result
    motion=np.memmap(files['motion_rg16f'],mode='r',dtype='<f2',shape=(h,w,2)).astype('f4')
    if not np.isfinite(motion).all():raise ValueError('nonfinite motion')
    result['motion_pixels_over_one']=int((np.linalg.norm(motion,axis=2)>1).sum())
    if 'previous_features_rg32ui' not in files:return result
    def unpack(key):
        image=np.memmap(files[key],mode='r',dtype='<u4',shape=(h,w,2))
        rgb=np.stack([(image[:,:,0]>>shift)&255 for shift in (0,8,16)],axis=2).astype('u1')
        return rgb,image[:,:,1]>>20,(image[:,:,1]&0xfffff).astype('f4')/1048575
    color,material,z=unpack('current_features_rg32ui');previous,old_id,old_z=unpack('previous_features_rg32ui')
    m=np.array(meta['clip_to_previous'],'f4').reshape(4,4)
    jx,jy,px,py=map(float,meta['jitter_current_previous'])
    y,x=np.ogrid[:h,:w];cx=((x.astype('f4')+.5-jx)/w)*2-1;cy=1-((y.astype('f4')+.5-jy)/h)*2
    def component(i):return cx*m[0,i]+cy*m[1,i]+z*m[2,i]+m[3,i]
    cw=component(3);valid=(z>0)&(cw>0);inv=1/np.where(valid,cw,1)
    camera=np.stack([(component(0)*inv-cx)*(.5*w),(cy-component(1)*inv)*(.5*h)],axis=2)
    camera[~valid]=0
    base=motion-camera;residual=np.linalg.norm(base,axis=2)
    changed=(residual>1)&valid
    mask=np.memmap(files['reactive_r8'],mode='r',dtype='u1',shape=(h,w))
    result.update({'material_corrections_over_one_px':int(changed.sum()),'correction_fraction':float(changed.mean()),
                   'reactive_over_half_fraction':float((mask>127).mean())})
    if changed.any():
        candidates,count=np.unique(material[changed],return_counts=True)
        result['largest_corrected_materials']=[{'id':int(candidates[i]),'pixels':int(count[i]),
            'median_residual_xy':np.median(base[changed&(material==candidates[i])],axis=0).round(3).tolist()} for i in np.argsort(-count)[:8]]
        expected=component(2)*inv
        def mismatch(vector):
            xx=np.floor(x+vector[:,:,0]+px-jx+.5).astype(int);yy=np.floor(y+vector[:,:,1]+py-jy+.5).astype(int)
            inside=(xx>=0)&(xx<w)&(yy>=0)&(yy<h);xx=xx.clip(0,w-1);yy=yy.clip(0,h-1)
            same=inside&(old_id[yy,xx]==material)
            d=np.abs(old_z[yy,xx]-expected)/np.maximum(.001,expected)
            c=np.abs(previous[yy,xx].astype('f4')-color)/255
            return np.where(same&(d<=.18),np.minimum(1,c.max(axis=2)+.6*d),1)
        result['mean_feature_mismatch_on_corrected_pixels']={'camera':float(mismatch(camera)[changed].mean()),'estimated':float(mismatch(motion)[changed].mean())}
    if preview:
        def thumbnail(rgb):return Image.fromarray(rgb).resize((520,round(520*h/w)),Image.Resampling.LANCZOS).convert('RGB')
        prev=thumbnail(previous);current=thumbnail(color)
        blend=color.copy()
        blend[changed]=np.clip(.36*color[changed]+.64*np.array([255,168,36]),0,255).astype('u1')
        masked=color.copy();masked[mask>127]=np.clip(.25*color[mask>127]+.75*np.array([69,232,245]),0,255).astype('u1')
        panels=[prev,current,thumbnail(blend),thumbnail(masked)]
        labels=['Previous frame','Current frame','Object-motion correction over 1 px','Pixels with reduced history confidence']
        ph=panels[0].height
        header=41 if title else 0
        canvas=Image.new('RGB',(1064,2*ph+99+header),(20,23,27));draw=ImageDraw.Draw(canvas)
        try:font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',18)
        except OSError:font=ImageFont.load_default()
        if title:draw.text((8,7),title,font=font,fill=(182,211,224))
        for i,(panel,label) in enumerate(zip(panels,labels)):
            left=8+(i%2)*528;top=header+6+(i//2)*(ph+45)
            draw.text((left,top),label,font=font,fill='white');canvas.paste(panel,(left,top+29))
        canvas.save(preview)
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('manifest',type=Path);parser.add_argument('--preview',type=Path);parser.add_argument('--title')
    args=parser.parse_args();print(json.dumps(inspect(args.manifest,args.preview,args.title),indent=2))
