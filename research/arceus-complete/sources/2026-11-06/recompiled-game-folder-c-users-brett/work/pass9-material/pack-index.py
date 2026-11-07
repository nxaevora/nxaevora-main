import sys
sys.path.pop(0)
from pathlib import Path
import struct,re,json,collections
root=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/archive/field/model/pack')
allrows=[]
for name in ['ha_area00.gfpak','ha_area00_s01.gfpak']:
 b=(root/name).read_bytes();n=struct.unpack_from('<I',b,16)[0];table=struct.unpack_from('<Q',b,24)[0]
 rows=[]
 for i in range(n):
  kind,codec,usize,csize,align,offset=struct.unpack_from('<HHIIIQ',b,table+i*24)
  assert offset+csize<=len(b)
  p=b[offset:offset+csize]
  strings=[x.decode('ascii') for x in re.findall(rb'[A-Za-z0-9_./]{6,}',p) if any(z in x for z in [b'.trmtr',b'.trmdl',b'.bntx',b'NormalHeight',b'NormalMap',b'house',b'const',b'bridge',b'waterwheel'])]
  raw=p[2:] if p[:2]==b'\xcc\x06' and len(p)==usize+2 else None
  row=dict(archive=name,index=i,kind=kind,codec=codec,usize=usize,csize=csize,offset=offset,raw=raw is not None,strings=strings)
  rows.append(row)
  if raw is not None:
   out=Path('work/pass9-material/raw-members');out.mkdir(exist_ok=True);out.joinpath(name+'.'+str(i)+'.bin').write_bytes(raw)
 Path('work/pass9-material').joinpath(name+'.index.json').write_text(json.dumps(rows,indent=2));allrows+=rows
 print(name,'entries',n,'raw',sum(x['raw'] for x in rows),'materialname candidates',sum(any('.trmtr' in s for s in r['strings']) for r in rows))
Path('work/pass9-material/packed-asset-strings.txt').write_text('\n'.join(f"{r['archive']} entry={r['index']} usize={r['usize']} raw={r['raw']}: {', '.join(r['strings'])}" for r in allrows if r['strings']))
