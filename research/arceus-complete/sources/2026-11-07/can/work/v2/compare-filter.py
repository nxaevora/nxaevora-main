from pathlib import Path
import numpy as np
import sys
from PIL import Image

root=Path(__file__).resolve().parent
# Normalized regions of the locked indoor camera, excluding all characters and HUD.
regions={'plaster':(.69,.205,.86,.58),'door':(.56,.37,.63,.70),
         'wainscoting':(.25,.64,.405,.745),'carpet':(.16,.81,.36,.86)}
def signature(path):
    with Image.open(path) as image:
        upper=np.asarray(image.crop((300,100,3500,720)).resize((200,40)).convert('L'),np.float32)
    return (upper-upper.mean())/max(1.,upper.std())
reference=signature(root/'filter-off/screen_0028.ppm')
for mode in sys.argv[1:] or ('off','on'):
    frames=sorted((root/('filter-'+mode)).glob('screen_*.ppm'))[17:29]
    if len(frames)!=12:
        raise ValueError(f'{mode}: expected 12 fixed-camera shots, got {len(frames)}')
    aligned=[frame for frame in frames if np.abs(signature(frame)-reference).mean()<.05]
    if len(aligned)<10:
        print(f'{mode}: camera alignment failed ({len(aligned)}/12), excluded')
        continue
    frames=aligned
    for name,bounds in regions.items():
        samples=[]
        for frame in frames:
            with Image.open(frame) as img:
                box=tuple(round(v*img.size[i%2]) for i,v in enumerate(bounds))
                samples.append(np.asarray(img.crop(box).convert('L'),dtype=np.float32))
        data=np.stack(samples)
        mean=data.mean(axis=0)
        edge=(np.abs(np.diff(mean,axis=0)).mean()+np.abs(np.diff(mean,axis=1)).mean())*.5
        print(f'{mode:3} {name:12} temporal SD={data.std(axis=0).mean():.3f} mean={mean.mean():.2f} mean gradient={edge:.3f}')
