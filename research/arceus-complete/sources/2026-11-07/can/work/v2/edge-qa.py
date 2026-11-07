"""Measure edge sharpness separately from grain in fixed-camera 4K captures."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image,ImageFilter

root=Path(__file__).resolve().parent
def read_mean(label,box):
    files=sorted((root/('filter-'+label)).glob('screen_*.ppm'))[17:29]
    if len(files)!=12:raise ValueError(f'{label}: expected twelve captures')
    with Image.open(root/'filter-off/screen_0028.ppm') as image:
        reference=signature(image)
    parts=[]
    for file in files:
        with Image.open(file) as image:
            if np.abs(signature(image)-reference).mean()<.05:
                parts.append(np.asarray(image.crop(box).convert('L'),np.float32))
    if len(parts)<10:raise ValueError(f'{label}: camera does not match')
    return np.stack(parts)

def signature(image):
    area=np.asarray(image.crop((300,100,3500,720)).resize((200,40)).convert('L'),np.float32)
    return (area-area.mean())/max(1.,area.std())

def gradient(image):
    dy,dx=np.gradient(image)
    return np.sqrt(dx*dx+dy*dy)

for name,box in [('door',(2145,795,2419,1512)),('trim',(960,1382,1555,1609))]:
    base=read_mean(sys.argv[1],box).mean(axis=0)
    # Locate durable contours in the average image. Individual noisy pixels
    # cannot define a contour after this spatial and temporal prefilter.
    small=Image.fromarray(np.uint8(np.clip(base,0,255))).filter(ImageFilter.GaussianBlur(2))
    ref=gradient(np.asarray(small,np.float32))
    mask=ref>max(3.,np.percentile(ref,95))
    rawSlope=gradient(base)[mask].mean()
    for label in sys.argv[1:]:
        try:
            series=read_mean(label,box)
        except ValueError as error:
            print(error)
            continue
        slope=gradient(series.mean(axis=0))[mask].mean()
        flicker=series.std(axis=0)[mask].mean()
        print(f'{label} {name}: {mask.sum()} contour pixels, slope {slope:.2f} ({slope/rawSlope:.1%} of reference), temporal SD {flicker:.2f}')
