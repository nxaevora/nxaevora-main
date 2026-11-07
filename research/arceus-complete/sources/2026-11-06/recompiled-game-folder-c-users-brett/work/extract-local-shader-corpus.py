import sys
sys.path.pop(0)  # this work directory also contains an unrelated inspect.py
from pathlib import Path
import struct, hashlib, json
root=Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/shader_cache')
out=Path('work/local-shader-corpus'); out.mkdir(exist_ok=True)
info_size=2096
counts={}; manifest=[]
for file in ['ArceusTest.vkshaders','ArceusTest.vkshipped']:
    count=0; source=None
    with (root/file).open('rb') as f:
        head=f.read(16); assert head[:8]==b'SNXVKSH\n'
        f.seek((16+struct.unpack_from('<I',head,12)[0]+7)&~7)
        while head:=f.read(16):
            if len(head)!=16:break
            typ,size,check=struct.unpack('<IIQ',head); p=f.read(size)
            if len(p)!=size:break
            if typ==5: source=p; continue
            if typ!=1 or source is None:continue
            assert p[:40]==source[:40] and p[48:128]==source[48:128]
            h,_,_,stage,clen,clena,ncbr,ncode,flags=struct.unpack_from('<3Q6I',source)
            nspv=struct.unpack_from('<I',p,40)[0]
            at=128+info_size+ncbr*16
            assert p[at:at+4]==b'\x03\x02\x23\x07' and 0<=len(p)-at-nspv*4<8
            code=source[128+ncbr*16:128+ncbr*16+ncode]
            assert len(code)==ncode
            # Preserve the cached input key fields. Translation resets outputs.
            si=p[128:128+info_size]
            suffix=hashlib.sha256(p[48:128]+code).hexdigest()[:12]
            name=f'{h:016x}_{stage}_{suffix}.snxs'
            blob=struct.pack('<4s5I',b'SNXS',1,info_size,stage,ncode,ncode)+si+code
            (out/name).write_bytes(blob)
            (out/(name+'.cbr')).write_bytes(source[128:128+ncbr*16])
            count+=1
            manifest.append(dict(name=name,hash=f'{h:016x}',stage=stage,source=file,ncbr=ncbr,bytes=ncode))
            source=None
    counts[file]=count
Path('work/local-shader-corpus-manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(dict(records=counts,unique=len(list(out.glob('*.snxs')))),indent=2))
