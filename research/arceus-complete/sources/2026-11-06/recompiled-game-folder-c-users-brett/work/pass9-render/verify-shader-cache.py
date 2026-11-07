import sys
sys.path.pop(0)
from pathlib import Path
import struct,hashlib,json
root=Path('work/pass9-render');result=[]
for name in ['ArceusTest.vkshaders','ArceusTest.vkshipped']:
 with (Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/shader_cache')/name).open('rb') as f:
  head=f.read(16);n=struct.unpack_from('<I',head,12)[0];f.seek((16+n+7)&~7)
  while head:=f.read(16):
   if len(head)<16:break
   typ,size,check=struct.unpack('<IIQ',head);p=f.read(size)
   if typ!=1 or len(p)<128:continue
   h=struct.unpack_from('<Q',p)[0]
   if h!=0xe9e7255e7594a006:continue
   stage,clen,clen_a,ncbr,nspv,flags=struct.unpack_from('<6I',p,24);at=p.find(b'\x03\x02\x23\x07',128)
   assert at>=0
   spv=p[at:at+nspv*4];out=root/(name+'-e9.spv');out.write_bytes(spv)
   comparisons={}
   for folder in ['work/pass8-final-corpus/cached-on','work/pass8-final-corpus/shipped-on','work/shipped-fragment-candidate']:
    for q in Path(folder).glob('e9e7255e7594a006*.spv'):comparisons[str(q)]=spv==q.read_bytes()
   result.append(dict(cache=name,bytes=len(spv),sha256=hashlib.sha256(spv).hexdigest(),comparisons=comparisons))
(root/'shader-cache-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
