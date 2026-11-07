import sys
sys.path.pop(0)
from pathlib import Path
exec(Path('work/pass8-shader/decode.py').read_text().split('if __name__')[0])
root=Path('work/pass10-render');j=json.loads((root/'candidate-checks.json').read_text());trace=json.loads((root/'materials.json').read_text())
manifest={l.split()[0]:l.split()[-1] for l in Path('work/shipped-fragment-manifest.txt').read_text().splitlines()}
out=[]
for file in j['comparisons']['shipped-on']['changed']:
 h=file.split('_')[0];w=load(next(Path('work/shipped-fragment-corpus').glob(h+'*.snxs')))
 (root/(h+'.txt')).write_text(listing(w))
 active=[dict(word=i,opcode=op(v),bits=f'{v:016x}') for i,v in enumerate(w) if i>=11 and(i-10)%4 and (v&~65535)==0x3868104000070000]
 tex=[dict(word=i,opcode=op(v),handle=hex(B(v,36,13))) for i,v in enumerate(w) if i>=11 and(i-10)%4 and op(v) in ('TLDS','TLD')]
 out.append(dict(hash=h,container=manifest[h],exact_twice_instructions=active,fetches=tex,trace_configs=trace['shaders'].get(h.lstrip('0'),0)))
(root/'changed-role-audit.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
