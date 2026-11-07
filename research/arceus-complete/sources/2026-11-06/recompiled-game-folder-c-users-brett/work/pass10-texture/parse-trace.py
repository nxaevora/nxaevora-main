import sys
sys.path.pop(0)
from pathlib import Path
import json,re,collections
log=Path('C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-172820-124/game-err.txt')
draws={}; inputs={}; desc={}
for no,line in enumerate(log.read_text(errors='replace').splitlines(),1):
 if not line.startswith('material-trace: '):continue
 tag=line.split()[1]; d=dict(re.findall(r'(\w+)=([^ ]+)',line)); d['line']=no
 if tag=='draw':draws[d['id']]=d
 elif tag=='input' and 'address' in d:inputs[(d['id'],d['slot'])]=d
 elif tag=='descriptors':desc[(d['id'],d['slot'])]=d
rows=[]
for key,d in inputs.items():
 if key not in desc:continue
 row={'draw':draws[key[0]],'input':d,'descriptors':desc[key]}
 t=[int(s,16) for s in desc[key]['tic'].split(',')]
 row['decoded']={'format':t[0]&0x7f,'type':(t[4]>>23)&15,'hv':(t[2]>>21)&7,'w':(t[4]&65535)+1,'h':(t[5]&65535)+1,'d':((t[5]>>16)&16383)+1,'mips':((t[3]>>28)&15)+1,'base':t[7]&15,'max':(t[7]>>4)&15,'bh':(t[3]>>3)&7,'bd':(t[3]>>6)&7}
 rows.append(row)
Path('work/pass10-texture/material-bindings.json').write_text(json.dumps(rows,indent=2))
counts=collections.Counter((r['decoded']['format'],r['input']['conversion'],r['input']['vkfmt']) for r in rows)
print('draws',len(draws),'resolved inputs',len(rows),'unique source addresses',len(set(r['input']['address'] for r in rows)))
print('format/conversion/VkFormat counts',counts)
print('BC5 nonzero base:',[(r['input']['id'],r['input']['slot'],r['decoded'])for r in rows if r['decoded']['format']==0x28 and r['decoded']['base']])
print('a336 normal bindings:')
for r in rows:
 if r['draw']['fs']=='a336734bf0d4c9c0' and r['input']['slot']=='1':print(r)
