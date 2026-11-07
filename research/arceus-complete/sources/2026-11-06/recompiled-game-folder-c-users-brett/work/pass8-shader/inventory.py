import sys
sys.path.pop(0)
from pathlib import Path
exec(compile(Path('work/pass8-shader/decode.py').read_text().split("if __name__=='__main__':")[0], 'decode.py','exec'))
from collections import defaultdict
names={line.split()[0]:line.split(' ',2)[2].strip() for line in Path('work/shipped-fragment-manifest.txt').read_text().splitlines() if len(line.split(' ',2))==3}
group=defaultdict(list);detail=[]
for path in Path('work/shipped-fragment-corpus').glob('*.snxs'):
 h=path.stem.split('_')[0];words=load(path)
 hits=[(i,B(w,0,8),B(w,30,8)*4) for i,w in enumerate(words) if i>=11 and (i-10)%4!=0 and op(w)=='IPA' and B(w,30,8)*4 in (0x70,0x74)]
 if not hits:continue
 container=Path(names.get(h,'unknown')).name;group[container].append(h)
 detail.append({'hash':h,'container':container,'screen_ipa':hits,'logical_bitops':[(i,op(w),B(w,20,19)) for i,w in enumerate(words) if i>=11 and (i-10)%4!=0 and op(w) in ('LOP_I','LOP32I','BFE_I','SHL_I','SHR_I')]})
for k,v in sorted(group.items()):print(k,len(v),v[:3])
root.joinpath('screen-inventory.json').write_text(json.dumps(detail,indent=2)+'\n')
for k,hs in group.items():
 if 'standard' in k or 'terrain' in k or 'fabric' in k or 'non_directional' in k:
  h=hs[0];path=next(Path('work/shipped-fragment-corpus').glob(h+'*'));root.joinpath(h+'.txt').write_text(listing(load(path)));print('EXAMPLE',k,h)

