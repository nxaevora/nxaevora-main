from pathlib import Path
import struct,json
root=Path('C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/env')
results=[]
for p in root.rglob('*.trlgt'):
 b=p.read_bytes()
 u=lambda i:struct.unpack_from('<I',b,i)[0]
 si=lambda i:struct.unpack_from('<i',b,i)[0]
 h=lambda i:struct.unpack_from('<H',b,i)[0]
 def field(t,k):
  v=t-si(t);o=h(v+4+2*k) if 4+2*k<h(v) else 0
  return t+o if o else None
 def ptr(i):return i+u(i)
 def string(i):
  if i is None:return ''
  q=ptr(i);return b[q+4:q+4+u(q)].decode()
 def vector(i):
  q=ptr(i);return [ptr(q+4+4*j) for j in range(u(q))]
 try:
  for t in vector(field(u(0),1)):
   name=string(field(t,0));kind=string(field(t,1))
   if kind!='Rim':continue
   colors={}
   for c in vector(field(t,4)):
    colors[string(field(c,0))]=list(struct.unpack_from('<4f',b,field(c,1)))
   results.append(dict(file=str(p),name=name,kind=kind,colors=colors))
 except (ValueError,struct.error,UnicodeError,TypeError):continue
Path('work/lighting-asset-rims.json').write_text(json.dumps(results,indent=2))
matches=[]
for r in results:
 for name,c in r['colors'].items():
  rgb=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c[:3]]
  if max(abs(a-b) for a,b in zip(rgb,[.456411,.187821,.73791]))<.001:matches.append((r['file'],r['name'],name,c,rgb))
print('rim records',len(results),'matches',json.dumps(matches,indent=2))
print('standard',json.dumps([r for r in results if r['file'].endswith('sd_standard.trlgt') and r['name']=='rim_tgt_1'],indent=2))
