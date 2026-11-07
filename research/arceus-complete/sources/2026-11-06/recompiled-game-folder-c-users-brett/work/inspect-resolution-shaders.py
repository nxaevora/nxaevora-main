from pathlib import Path
import struct, re, collections, json
root=Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/shader_cache')
log=Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-125820-850/game-err.txt').read_text(errors='replace')
want={int(h,16) for h in re.findall(r'resolution-trace: draw.*hash=([0-9a-f]+)',log)}
out=Path('work/resolution-shaders');out.mkdir(exist_ok=True)
result=[]
for name in ['ArceusTest.vkshaders','ArceusTest.vkshipped']:
    with (root/name).open('rb') as f:
        head=f.read(16)
        assert head[:8]==b'SNXVKSH\n'
        n=struct.unpack_from('<I',head,12)[0]
        f.seek((16+n+7)&~7)
        while head:=f.read(16):
            if len(head)<16:break
            typ,size,check=struct.unpack('<IIQ',head)
            p=f.read(size)
            if len(p)<size:break
            if typ not in (1,5) or len(p)<128:continue
            h=struct.unpack_from('<Q',p)[0]
            if h not in want:continue
            stage,clen,clen_a,ncbr,nspv,flags=struct.unpack_from('<6I',p,24)
            if typ==5:
                (out/f'{h:016x}.source').write_bytes(p)
                continue
            at=p.find(b'\x03\x02\x23\x07',128)
            if at<0:continue
            spv=p[at:at+nspv*4]
            (out/f'{h:016x}.spv').write_bytes(spv)
            words=struct.unpack('<'+'I'*(len(spv)//4),spv)
            ops=collections.Counter();pos=5
            while pos<len(words):
                count=words[pos]>>16;op=words[pos]&0xffff
                if not count:raise ValueError('invalid SPIR-V')
                ops[op]+=1;pos+=count
            result.append(dict(hash=f'{h:016x}',stage=stage,words=nspv,fetch=ops[95],sample=ops[87]+ops[88],gather=ops[96],size=ops[103]+ops[104],f_to_s=ops[110],f_to_u=ops[109]))
print(json.dumps(result,indent=2))
(out/'summary.json').write_text(json.dumps(result,indent=2))
