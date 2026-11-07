import sys
sys.path.pop(0)
from pathlib import Path
exec(Path('work/pass8-shader/decode.py').read_text().split("if __name__")[0])
manifest=Path('work/shipped-fragment-manifest.txt').read_text().splitlines()
rows=[]
for line in manifest:
 if 'ha_standard.bnsh' not in line:continue
 name=line.split()[0]
 paths=list(Path('work/shipped-fragment-corpus').glob(name+'*.snxs'))
 if not paths:continue
 words=load(paths[0]);ops=[op(w) for i,w in enumerate(words) if i>=11 and(i-10)%4]
 attrs=[B(w,30,8)*4 for i,w in enumerate(words) if i>=11 and(i-10)%4 and op(w)=='IPA']
 tex=[dict(word=i,opcode=op(w),handle=hex(B(w,36,13)),encoding=B(w,53,4),da=B(w,0,8),db=B(w,28,8),swizzle=B(w,50,3),fp32=B(w,59,1)) for i,w in enumerate(words) if i>=11 and(i-10)%4 and op(w) in ['TEXS','TEX','TLD','TLDS']]
 rows.append(dict(hash=name,words=len(words),screen_xy=any(a in(0x70,0x74) for a in attrs),quad_ops=[o for o in ops if o in ['SHFL','FSWZADD']],textures=tex))
Path('work/pass9-material/material-inventory.json').write_text(json.dumps(rows,indent=2))
print('ha_standard variants',len(rows),'screen_xy',sum(x['screen_xy'] for x in rows),'quad',sum(bool(x['quad_ops']) for x in rows))
print(next((x for x in rows if x['hash'].startswith('a336')),None))
