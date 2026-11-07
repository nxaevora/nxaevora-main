from pathlib import Path
import numpy as np
import argparse

def analyze(path, packed_ids=False):
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
            labels = ['relative depth difference', 'albedo distance', 'normal cosine']
            if packed_ids:
                pairs = info[:, :, 3][rej].astype(np.uint32)
                current, previous = pairs >> 12, pairs & 4095
                print('rejected pixels with matching IDs', int((current == previous).sum()), 'of', pairs.size)
                ids, counts = np.unique(pairs, return_counts=True)
                for index in np.argsort(counts)[-5:][::-1]:
                    print('current ID', ids[index] >> 12, 'previous ID', ids[index] & 4095, 'pixels', counts[index])
            else:
                labels.append('previous age')
            for j,n in enumerate(labels):
                print(n,np.round(np.quantile(info[:,:,j][rej],[.1,.5,.9]),5))
            print('projected motion',np.round(np.quantile(planes[4,:,:,1][rej],[.1,.5,.9]),4))
            print('expected depth / current depth',np.round(np.quantile(depths[:,:,0][rej]/np.maximum(1.e-9,depths[:,:,1][rej]),[.1,.5,.9]),5))
if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--packed-ids', action='store_true', help='decode the surface-history witness format')
    parser.add_argument('captures', type=Path, nargs='+')
    args = parser.parse_args()
    for capture in args.captures:
        analyze(capture, args.packed_ids)
