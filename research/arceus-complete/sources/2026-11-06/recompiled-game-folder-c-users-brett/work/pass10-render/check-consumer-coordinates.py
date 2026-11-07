import sys
sys.path.pop(0)
from pathlib import Path
import struct,hashlib,json
root=Path('work/pass10-render')
hashes=['e9e7255e7594a006','e02c1a109a77fac4','f4d83b712affc9b5','89619027ba43e5d3','fddd1ce88da11da0','1d0676ab12f2bfeb']
paths={}
for name in ['ArceusTest.vkshaders','ArceusTest.vkshipped']:
 with (Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/shader_cache')/name).open('rb') as f:
  head=f.read(16);n=struct.unpack_from('<I',head,12)[0];version=f.read(n);f.seek((16+n+7)&~7)
  print(name,version)
  while head:=f.read(16):
   if len(head)<16:break
   typ,size,check=struct.unpack('<IIQ',head);p=f.read(size)
   if typ!=1 or len(p)<128:continue
   h=hex(struct.unpack_from('<Q',p)[0])[2:]
   if h not in hashes:continue
   stage,clen,clen_a,ncbr,nspv,flags=struct.unpack_from('<6I',p,24)
   at=p.find(b'\x03\x02\x23\x07',128);assert at>=0
   spv=p[at:at+nspv*4];out=root/(name+'-'+h+'.spv');out.write_bytes(spv);paths[name+'-'+h]=out
exec(Path('work/check-screen-fetch-semantics.py').read_text().split('fixtures=Path(')[0])
result=[]
for name,path in paths.items():
 h=name.split('-')[-1];s=Shader(path)
 slot=1 if h in ['e9e7255e7594a006','f4d83b712affc9b5','fddd1ce88da11da0'] else 0
 fetch=slot if h!='e9e7255e7594a006' else 1
 vals=[s.coordinates(x+.5,10.5,4/3,0xffffffff,fetch) for x in range(20)]
 rec=dict(cache=name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),normal_fetch=fetch,slot=slot,normal_coordinates=vals)
 if h=='1d0676ab12f2bfeb':
  rec['depth_coordinates']=[s.coordinates(x+.5,10.5,4/3,3,1) for x in range(20)]
  rec['physical_twice_center']=[[2*x+1,21] for x in range(20)]
 print(rec);result.append(rec)
(root/'consumer-coordinates.json').write_text(json.dumps(result,indent=2))
