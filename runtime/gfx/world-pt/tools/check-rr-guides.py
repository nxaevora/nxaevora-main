"""Check one raw guide readback from the frame-indexed village pan."""
from pathlib import Path
import argparse
import numpy as np

width, height = 1920, 1080
parser=argparse.ArgumentParser()
parser.add_argument("path",type=Path)
parser.add_argument("--no-forward-draws",action="store_true",help="Expect identical pre- and post-forward readbacks on a transition frame")
args=parser.parse_args()
path=args.path
with path.open("rb") as stream:
    matrices = np.fromfile(stream, "<f4", 48).reshape(3, 4, 4).astype("float64")
    diffuse, specular, surface = np.fromfile(stream, "<f2", 3*width*height*4).reshape(3,height,width,4)
    distance, depth = np.fromfile(stream, "<f4", 2*width*height).reshape(2,height,width)
    overlay=np.fromfile(stream,"<f2")
    if overlay.size not in (0,2*height*width*4):
        raise ValueError("Unexpected trailing guide data")
for plane in (matrices, diffuse, specular, surface, distance, depth):
    if not np.isfinite(plane).all():
        raise ValueError("Non-finite guide data")
for albedo in (diffuse, specular):
    assert np.all((albedo >= 0) & (albedo <= 1))
norm_length = np.linalg.norm(surface[...,:3].astype("float32"),axis=2)
active = norm_length > .1
roughness = surface[...,3]
assert active.sum()>10000 and np.all(np.abs(norm_length[active]-1)<.003)
assert np.all((roughness >= 0) & (roughness <= 1)) and np.ptp(roughness[active])>.05
assert np.all(distance>=0) and (distance>0).sum()>1000 and not np.any(distance[~active])
assert np.ptp(diffuse[active,:3])>.1 and np.ptp(specular[active,:3])>.01
y,x=np.where(active & (depth>0))
y,x=y[::101],x[::101]
clip=np.stack((2*(x+.5)/width-1,1-2*(y+.5)/height,depth[y,x],np.ones(len(x))),axis=1)
world=clip@matrices[0]
back=world@matrices[1]@matrices[2]
back/=back[:,3,None]
pixel_error=np.linalg.norm((back[:,:2]-clip[:,:2])*[width*.5,height*.5],axis=1)
depth_error=np.abs(back[:,2]-clip[:,2])
print(f"{active.sum()} normalized surface guides, {(distance>0).sum()} reflected hits")
print(f"Roughness range {roughness[active].min():.4f} to {roughness[active].max():.4f}")
print(f"Reprojection p99 {np.percentile(pixel_error,99):.4f} px; reversed depth p99 {np.percentile(depth_error,99):.8g}")
assert np.percentile(pixel_error,99)<.5 and np.percentile(depth_error,99)<1.e-4
if overlay.size:
    before,after=overlay.reshape(2,height,width,4)
    assert np.isfinite(overlay).all()
    changed=np.any(before[:,:,:3]!=after[:,:,:3],axis=2)
    print(f"Guest overlays changed {changed.sum()} of {width*height} scene pixels before reconstruction")
    if args.no_forward_draws:
        assert changed.sum()==0
    else:
        assert 100<changed.sum()<width*height
