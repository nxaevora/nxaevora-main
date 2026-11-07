import sys
sys.path.pop(0)
from pathlib import Path
exec(Path('work/pass8-shader/decode.py').read_text().split("if __name__")[0])
root=Path('work/pass10-render')
hashes=['e9e7255e7594a006','e02c1a109a77fac4','f4d83b712affc9b5','89619027ba43e5d3','fddd1ce88da11da0','1d0676ab12f2bfeb']
matched=Path('work/pass8-review/shipped-matched-chains.txt').read_text()
out=[]
for name in hashes:
 words=load(next(Path('work/shipped-fragment-corpus').glob(name+'*.snxs')))
 root.joinpath(name+'.txt').write_text(listing(words))
 ops=[]
 for i,w in enumerate(words):
  if i<11 or (i-10)%4==0:continue
  if op(w) not in ['TEX','TEXS','TLD','TLDS','TXQ','TXD']:continue
  ops.append(dict(word=i,op=op(w),handle=hex(B(w,36,13)),encoding=B(w,53,4)))
 print(name,len(words),ops)
 out.append(dict(hash=name,words=len(words),textures=ops))
(root/'consumer-shaders.json').write_text(json.dumps(out,indent=2))
(root/'consumer-old-matched-chains.txt').write_text('\n'.join(x for x in matched.splitlines() if any(n in x for n in hashes)))
