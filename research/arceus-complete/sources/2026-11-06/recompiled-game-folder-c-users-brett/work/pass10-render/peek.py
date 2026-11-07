import sys
sys.path.pop(0)
from pathlib import Path
import json,collections,struct
j=json.loads(Path('work/pass10-render/materials.json').read_text())
for id,r in j['records'].items():
 if r['draw']['fs'] in ['a336734bf0d4c9c0','815803708190e019']:
  print('DRAW',id,r['draw'],'OUTPUT',r['outputs'])
  for k,v in r['slots'].items():
   if k=='1' or r['draw']['fs']=='815803708190e019':print('SLOT',k,v)
print('BC5samplerdesc counts')
c=collections.Counter(v['descriptors']['tsc'] for r in j['records'].values() for v in r['slots'].values() if v.get('input',{}).get('fkey')=='24928')
print(c)
