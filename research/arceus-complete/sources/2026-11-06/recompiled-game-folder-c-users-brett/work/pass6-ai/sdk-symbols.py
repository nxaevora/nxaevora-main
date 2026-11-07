import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib
root=Path('work/pass6-ai')
raw=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/sdk.bin').read_bytes()
m=struct.unpack_from('<I',raw,4)[0];d=m+struct.unpack_from('<i',raw,m+4)[0];tags={}
while True:
 t,v=struct.unpack_from('<QQ',raw,d);d+=16
 if not t:break
 tags[t]=v
count=struct.unpack_from('<I',raw,tags[4]+4)[0];out={}
for i in range(count):
 n,info,other,sec,value,z=struct.unpack_from('<IBBHQQ',raw,tags[6]+i*24)
 s=tags[5]+n;name=raw[s:raw.find(b'\0',s)].decode(errors='replace')
 if (name.startswith('_ZN2nn4util6detail') and any(x in name for x in ['FloatDegree180','FloatPi','Float2Pi','Float1Divided2Pi','SinCoefficients','CosCoefficients','FloatQuaternionEpsilon'])) or name=='acosf':
  out[name]={'offset':hex(value),'length':z,'floats':list(struct.unpack_from('<'+'f'*(z//4),raw,value)) if z<100 and z%4==0 else None,'hex':raw[value:value+z].hex() if z<100 else None}
print(json.dumps(out,indent=2))
root.joinpath('sdk-symbols.json').write_text(json.dumps({'sdk_sha256':hashlib.sha256(raw).hexdigest(),'symbols':out},indent=2)+'\n')
