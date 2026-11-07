import sys
sys.path.pop(0)
from pathlib import Path
import array,json,math
base=Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
prefix='screenshot_20261106_175313_buffers_4'
def words(index):
    a=array.array('I');a.frombytes((base/f'{prefix}_{index:02}.bin').read_bytes());return a
def floats(index):
    a=array.array('f');a.frombytes((base/f'{prefix}_{index:02}.bin').read_bytes());return a
dest=words(62); results=[]
for layer,index in enumerate([51,49]):
    src=words(index); f=floats(index); exact=0; tested=0; total_error=0.; max_error=0.; phases=[[] for _ in range(3)]
    for y in range(1024):
        sy=min(1364,math.floor((y+.5)*1365/1024))
        for x in range(1024):
            sx=min(1364,math.floor((x+.5)*1365/1024))
            exact+=dest[layer*1024*1024+y*1024+x]==src[sy*1365+sx]
            if not(1<sx<1363 and 1<sy<1363):continue
            k=sy*1365+sx;c=f[k];dx=(f[k+1]-f[k-1])*.5;dy=(f[k+1365]-f[k-1365])*.5
            if not(0<c<1 and abs(dx)+abs(dy)>1e-7):continue
            if abs(f[k+1]+f[k-1]-2*c)>2e-7 or abs(f[k+1365]+f[k-1365]-2*c)>2e-7:continue
            # Plane's depth at destination's nominal guest center in the actual
            # 1365.333 viewport, versus the selected source pixel's center.
            ox=(x+.5)*4/3-(sx+.5);oy=(y+.5)*4/3-(sy+.5)
            error=-(dx*ox+dy*oy)
            tested+=1;total_error+=abs(error);max_error=max(max_error,abs(error))
            phases[x%3].append(error)
    results.append(dict(layer=layer,source_index=index,nearest_copy_exact=exact,total=1024*1024,
        planar_samples=tested,mean_abs_depth_error=total_error/max(tested,1),max_abs_depth_error=max_error,
        x_phase_mean_error=[sum(p)/len(p) if p else None for p in phases]))
out=Path('outputs/arceus-shadow-resampling-evidence.json')
out.write_text(json.dumps(dict(capture=prefix,results=results,
    scope='Exact captured-copy check plus local planar estimate. Does not replay shadow comparisons or prove bias sufficiency.'),indent=2))
print(out.read_text())
