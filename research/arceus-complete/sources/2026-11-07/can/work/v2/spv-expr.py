from pathlib import Path
import re,sys
s=Path('work/v2/terrain-material.spvasm').read_text()
defs=dict(re.findall(r'^\s*(%\w+) = (.+)$',s,re.M))
def expr(q,n):
 if q not in defs:return q
 op,*args=defs[q].split()
 if op in ('OpVariable','OpConstant'):return str(args[-1]) if op=='OpConstant' else q
 if not n:return q
 if op in ('OpBitcast','OpLoad','OpCopyObject'):return expr(args[1],n-1)
 return op.removeprefix('Op')+'('+','.join(expr(a,n-1) if a.startswith('%') else a for a in args[1:])+')'
for k in sys.argv[1:]:print(k,expr('%'+k,12)[:1500])
