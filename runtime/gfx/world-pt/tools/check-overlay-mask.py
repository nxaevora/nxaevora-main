from pathlib import Path
import sys
import numpy as np

def check(path):
    pixels=1920*1080
    if path.stat().st_size!=17*pixels:
        raise ValueError("Incomplete frame capture")
    with path.open("rb") as stream:
        colors=np.fromfile(stream,"<f2",16*pixels//2).reshape(2,pixels,4)
        mask=np.fromfile(stream,"u1",pixels)
    if not np.isfinite(colors).all():
        raise ValueError("Nonfinite scene color")
    before,after=colors[:,:,:3].astype(np.float32)
    expected=255*np.max(abs(after-before)/(np.maximum(after,before)+.0625),axis=1)
    error=np.max(np.abs(mask.astype(np.int16)-np.rint(expected)))
    changed=np.any(before!=after,axis=1)
    print(f"{changed.sum()} changed pixels, {np.count_nonzero(mask)} nonzero reactive pixels, max quantization error {error:.0f}/255")
    assert 100<changed.sum()<pixels//6 and 100<np.count_nonzero(mask)<=changed.sum()
    assert np.all(mask[~changed]==0) and error<=1
if __name__=="__main__":
    check(Path(sys.argv[1]))
