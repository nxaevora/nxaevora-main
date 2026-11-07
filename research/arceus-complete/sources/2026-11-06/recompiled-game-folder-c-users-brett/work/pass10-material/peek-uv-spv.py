import sys
sys.path.pop(0)
from pathlib import Path
src=Path('work/check-screen-fetch-semantics.py').read_text().split("fixtures=Path(")[0]
src=src.replace('if op==95:self.fetch.append(defs[a[3]])','if op==87:self.fetch.append(defs[a[3]])')
exec(src)
s=Shader('work/shipped-fragment-candidate/a336734bf0d4c9c0_4.spv')
for var,x in s.variables.items():
 if s.decor.get(var): print(var,x,s.decor[var])
print('samples',len(s.fetch))
for k in range(2):
 try:print(s.coordinates(0.5,0.5,1,0,k))
 except Exception as e:print(e)
