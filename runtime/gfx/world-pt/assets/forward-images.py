"""Identify bound forward textures by their native tiled texture bytes."""
import json,re,struct,sys
from collections import defaultdict
from pathlib import Path
from readpak import game,here
def textures(blob):
    start=blob.find(b'BNTX\0\0\0\0')
    if start<0:return
    b=memoryview(blob)[start:]
    size=struct.unpack_from('<I',b,0x1c)[0]
    if size>len(b):return
    count,table=struct.unpack_from('<IQ',b,0x24)
    if count>2048:return
    for i in range(count):
        o=struct.unpack_from('<Q',b,table+8*i)[0]
        if b[o:o+4]!=b'BRTI':continue
        w,h=struct.unpack_from('<II',b,o+0x24)
        name=struct.unpack_from('<Q',b,o+0x60)[0]
        length=struct.unpack_from('<H',b,name)[0]
        label=bytes(b[name+2:name+2+length]).decode('utf8')
        pointers=struct.unpack_from('<Q',b,o+0x70)[0]
        first=struct.unpack_from('<Q',b,pointers)[0]
        n=struct.unpack_from('<I',b,o+0x50)[0]
        if n<2048 or first+2048>size:continue
        key=14695981039346656037
        for byte in b[first:first+2048]:key=((key^byte)*1099511628211)&0xffffffffffffffff
        yield f'{key:016x}',w,h,label
def build(log):
    wanted=set(re.findall(r'RomFS: reading (\S+\.ptcl)',Path(log).read_text(errors='replace')))
    known=defaultdict(set);used=0
    for p in game.rglob('*.ptcl'):
        if p.name not in wanted:continue
        used+=1
        for key,w,h,n in textures(p.read_bytes()):known[f'{key}/{w}x{h}'].add((n,p.stem))
    for k,names in json.loads((here/'texture-signatures.json').read_text()).items():
        for name,w,h,fmt in names:known[f'{k}/{w}x{h}'].add((name,'field'))
    print('Read',used,'particle archives;',len(known),'unique signatures')
    return {k:sorted(v) for k,v in known.items()}
if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--build':
        (here/'forward-images.json').write_text(json.dumps(build(sys.argv[2])))
    else:
        known=json.loads((here/'forward-images.json').read_text())
        rows=defaultdict(set)
        for line in Path(sys.argv[1]).read_text(errors='replace').splitlines():
            if not line.startswith('forward-image:'):continue
            data=dict(re.findall(r'(\w+)=([a-z0-9]+)',line))
            key=data['fingerprint']+'/'+data['size']
            labels=known.get(key,[(key,'unknown')])
            rows[data['fs'],data['cb']].update(x[0] for x in labels)
        for (fs,cb),labels in sorted(rows.items()):print(fs,cb,','.join(sorted(labels))[:250])
