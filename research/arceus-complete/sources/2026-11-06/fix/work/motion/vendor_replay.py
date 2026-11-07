"""Compare the selected temporal backend to itself with known object vectors."""
from pathlib import Path
import json, shutil, subprocess
import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
data = here / 'fixtures'
out = here / 'vendor'
out.mkdir(exist_ok=True)
exe = here.parents[1] / 'outputs/upscale-runtime/MotionReplay.exe'
height, width = 1080, 1920
def link(source, name):
    target = out / name
    if target.exists():
        target.unlink()
    shutil.copyfile(source, target)

def display(raw):
    img = np.fromfile(raw, '<f2').reshape(1440, 2560, 4)[:, :, :3].astype('f4')
    assert np.isfinite(img).all()
    return np.maximum(img, 0) / (1 + np.maximum(img, 0))

def upscale_mask(mask):
    return np.array(Image.fromarray(mask).resize((2560,1440), Image.Resampling.NEAREST))

result = {}
for name in ('walk', 'articulated', 'shared_material'):
    truth = np.load(data / (name + '-truth.npz'))
    actor = upscale_mask(truth['label'] != 0)
    exposed = upscale_mask((truth['label']==0) & (truth['old']!=0))
    np.zeros((height,width,2),'<f2').tofile(out/'camera.bin')
    truth['truth'].tofile(out/'oracle.bin')
    for prefix, fixture in [('original','start_'+name), ('current',name)]:
        for index, ext in enumerate(('color','depth')):
            link(data / (fixture+'.'+ext), prefix+'-'+str(index)+'.bin')
    link(out/'camera.bin', 'original-2.bin')
    for backend in ('DLSS', 'FSR'):
        comparison = {}
        for mode, source in [('oracle',out/'oracle.bin'),('camera',out/'camera.bin'),('estimate',data/(name+'.flow'))]:
            link(source, 'current-2.bin')
            raw = out / f'{name}-{backend}-{mode}.raw'
            run = subprocess.run([str(exe),backend,'keep','current',str(raw)], cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (out / f'{name}-{backend}-{mode}.log').write_bytes(run.stdout)
            run.check_returncode()
            value = display(raw)
            if mode == 'oracle':
                oracle = value
            else:
                diff = np.abs(value-oracle).mean(axis=2)
                comparison[mode] = {key:float(diff[region].mean()) for key, region in [('character',actor),('revealed',exposed),('background',~(exposed|actor))]}
        result[name+'-'+backend]=comparison
        print(name, backend, comparison, flush=True)
        (out/'metrics.json').write_text(json.dumps(result,indent=2))

