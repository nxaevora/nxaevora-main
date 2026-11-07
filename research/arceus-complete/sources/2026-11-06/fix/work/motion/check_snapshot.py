"""Validate the files produced by the actual F3 writer against known GPU inputs."""
from pathlib import Path
import json
import numpy as np
from inspect_capture import inspect

here=Path(__file__).resolve().parent
out=here/'integrated'
results={}
for backend,mode,width,height in [('FSR','mask',1920,1080),('DLSS','estimate',1920,1080),('TAA','mask',2560,1440)]:
    stem=out/f'{backend}-walk-{mode}_temporal'
    report=inspect(Path(str(stem)+'.tsv'))
    assert report['backend']==backend and report['size']==[width,height]
    assert report['material_corrections_over_one_px']>100000
    errors=report['mean_feature_mismatch_on_corrected_pixels']
    assert errors['estimated']<errors['camera']
    source=here/'fixtures' if width==1920 else out/'2560'
    for what,frame in [('current','walk'),('previous','start_walk')]:
        raw=np.memmap(str(stem)+'_'+what+'_features_rg32ui.bin',mode='r',dtype='<u4',shape=(height,width,2))
        material=np.memmap(source/(frame+'.material'),mode='r',dtype='u1',shape=(height,width,4))
        expected=(material[:,:,2].astype('u2')<<8)+material[:,:,3]
        assert np.array_equal(raw[:,:,1]>>20,np.where(expected<1024,expected,4095))
        depth=np.memmap(source/(frame+'.depth'),mode='r',dtype='<f4',shape=(height,width))
        assert np.abs((raw[:,:,1]&0xfffff).astype('f4')-depth*1048575.).max()<=1
        linear=np.memmap(source/(frame+'.color'),mode='r',dtype='<f2',shape=(height,width,4))[:,:,:3].astype('f4')
        encoded=np.rint(255*np.sqrt(linear/(1+linear)))
        for c in range(3):assert np.abs(((raw[:,:,0]>>(8*c))&255)-encoded[:,:,c]).max()<=1
    if width==1920:
        for left,right in [('motion_rg16f.bin','flow'),('reactive_r8.bin','mask')]:
            assert Path(str(stem)+'_'+left).read_bytes()==(source/('walk.'+right)).read_bytes()
    results[backend]=report
(out/'capture-check.json').write_text(json.dumps(results,indent=2))
print('Passed F3 readback for',', '.join(results))
