from pathlib import Path
import struct,json,numpy as np,collections
root=Path(__file__).resolve().parent
catalog=json.loads((root/'texture-signatures.json').read_text())
def read(path,include_materials=False):
 b=path.read_bytes();tag,n,draws,textures=struct.unpack_from('<4I',b)
 assert tag==0x31504353
 axes=np.frombuffer(b,'<f4',12,16).reshape(3,4)[:,:3];org=np.frombuffer(b,'<f4',4,64)[:3]
 records=np.ndarray((draws,12),'<u4',b,80)
 end,slots=records[:,:2].T
 names=[]
 for key,w,h in struct.iter_unpack('<QII',b[80+draws*48:80+draws*48+textures*16]):
  candidates={v[0] for v in catalog.get(f'{key:016x}',[]) if v[1:3]==[w,h]}
  names.append(candidates.pop() if len(candidates)==1 else '')
 offset=80+draws*48+textures*16
 pos,attrs=np.frombuffer(b,'<f4',offset=offset).reshape(2,n,4)
 xyz=pos[:,:3]@axes+org
 result=[]
 for draw,(start,stop,slot) in enumerate(zip([0,*end[:-1]],end,slots)):
  values=(names[slot&255],(slot>>8)&255,xyz[start*3:stop*3],attrs[start*3:stop*3,:2])
  if include_materials:
   rough,metal=records[draw,2:4]
   values+=(names[rough] if rough else '',names[metal] if metal else '')
  result.append(values)
 return result
if __name__=='__main__':
 for p in sorted((root/'static').glob('*.bin')):
  c=collections.Counter()
  for name,cut,points,uv in read(p):
   c[name or 'unknown']+=len(points)//3
  print(p.name,sum(c.values()),len(c),c.most_common(6),sum(v for n,v in c.items() if n.startswith('objects_')))
