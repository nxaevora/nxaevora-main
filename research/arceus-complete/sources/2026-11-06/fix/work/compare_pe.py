from pathlib import Path
import struct
base=Path('C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett/work/native-shadow-candidate/ArceusNativeShadow-runtime.exe')
def sec(p):
 b=p.read_bytes();pe=struct.unpack_from('<I',b,60)[0];n=struct.unpack_from('<H',b,pe+6)[0];o=struct.unpack_from('<H',b,pe+20)[0];a={}
 for i in range(n):
  at=pe+24+o+40*i;name=b[at:at+8].rstrip(b'\0').decode();v,rva,size,ptr=struct.unpack_from('<IIII',b,at+8);a[name]=(rva,b[ptr:ptr+size])
 return a
a=sec(base);b=sec(Path('work/reflection/baseline-relink.exe'))
for key in ['.text','.rdata']:
 x=a[key][1];y=b[key][1];diff=[i for i in range(min(len(x),len(y))) if x[i]!=y[i]]
 print(key,len(x),len(y),'diffbytes',len(diff),'first',[(hex(a[key][0]+i),x[i],y[i]) for i in diff[:30]])
 if key=='.text':
  symbols=[]
  for line in Path('work/reflection/symbols.txt').read_text().splitlines():
   z=line.split()
   if len(z)==3 and z[1] in ['t','T']:
    try:symbols.append((int(z[0],16)-0x140000000,z[2]))
    except ValueError:pass
  import bisect,collections
  keys=[q[0] for q in symbols];c=collections.Counter(symbols[bisect.bisect_right(keys,a[key][0]+i)-1][1] for i in diff);print(c.most_common(25))
