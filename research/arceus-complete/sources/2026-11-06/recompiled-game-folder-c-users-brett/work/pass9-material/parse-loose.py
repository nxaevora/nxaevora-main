import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
class FB:
 def __init__(self,b):self.b=b
 def u32(self,p):return struct.unpack_from('<I',self.b,p)[0]
 def i32(self,p):return struct.unpack_from('<i',self.b,p)[0]
 def fields(self,p):
  v=p-self.i32(p);n,z=struct.unpack_from('<HH',self.b,v)
  assert 4<=n<=100 and 4<=z<=10000 and 0<=v<len(self.b)
  return {i:p+o for i in range((n-4)//2) if(o:=struct.unpack_from('<H',self.b,v+4+2*i)[0])}
 def ref(self,p):return p+self.u32(p)
 def s(self,p):
  a=self.ref(p);n=self.u32(a);assert n<4096 and a+4+n<=len(self.b);return self.b[a+4:a+4+n].decode('utf-8')
 def vector(self,p):
  a=self.ref(p);n=self.u32(a);assert n<1000 and a+4+4*n<=len(self.b);return [self.ref(a+4+4*i) for i in range(n)]
 def parse(self):
  root=self.fields(self.u32(0));mats=self.vector(root[1]);rows=[]
  for p in mats:
   fields=self.fields(p);name=self.s(fields[0]);row={'material':name,'textures':{},'scalar_parameters':{},'vector_parameters':{}}
   if 2 in fields:
    for t in self.vector(fields[2]):
     q=self.fields(t);row['textures'][self.s(q[0])]=self.s(q[1])
   if 4 in fields:
    for t in self.vector(fields[4]):
     q=self.fields(t);row['scalar_parameters'][self.s(q[0])]=struct.unpack_from('<f',self.b,q[1])[0] if 1 in q else 0
   if 7 in fields:
    for t in self.vector(fields[7]):
     q=self.fields(t);row['vector_parameters'][self.s(q[0])]=list(struct.unpack_from('<4f',self.b,q[1])) if 1 in q else [0]*4
   rows.append(row)
  return rows
rows=[];fail=[]
for p in Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/model/pack').glob('*.trmtr'):
 try:
  for row in FB(p.read_bytes()).parse():rows.append({'file':p.name,**row})
 except Exception as e:fail.append({'file':p.name,'error':str(e)})
Path('work/pass9-material/loose-materials.json').write_text(json.dumps({'materials':rows,'failures':fail},indent=2))
print('materials',len(rows),'files',len(set(r['file'] for r in rows)),'failures',len(fail))
for row in rows:
 if 'area00' in row['file']:print(json.dumps(row))

