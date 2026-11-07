from pathlib import Path
import struct,bisect,json,re,collections
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes();base=0x8005000
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
fns=sorted(set((int(a,16)-base,int(z)) for a,z in (l.split() for l in Path('work/functions.tsv').read_text().splitlines()) if base<=int(a,16)<base+n));starts=[a for a,z in fns]
def owner(p):
 i=bisect.bisect_right(starts,p)-1
 return fns[i][0] if i>=0 and p<fns[i][0]+fns[i][1] else None
def string(t):
 if not (0x3790000<=t<0x39d0000):return None
 e=b.find(b'\0',t,t+500)
 if e<0:return None
 v=b[t:e]
 if len(v)<3 or not all(32<=c<127 for c in v):return None
 return v.decode()
w=memoryview(b[:n]).cast('I');calls=collections.defaultdict(list);refs=collections.defaultdict(list);srefs=collections.defaultdict(list);hits=[]
for i,a in enumerate(w):
 p=i*4
 if a&0x7c000000==0x14000000:
  imm=a&0x3ffffff
  if imm&0x2000000:imm-=0x4000000
  t=p+imm*4
  if 0<=t<n:calls[t].append(p)
 if i+1<len(w):
  v=w[i+1]
  if a&0xffffffe0==0x52911120 and v&0xffffffe0==0x72a7a100 and a&31==v&31:hits.append(p)
 if a&0x9f000000!=0x90000000:continue
 imm=((a>>29)&3)|(((a>>5)&0x7ffff)<<2)
 if imm&0x100000:imm-=0x200000
 page=(p&~4095)+(imm<<12);reg=a&31
 for j in range(i+1,min(i+7,len(w))):
  v=w[j]
  if v&0xffc00000==0x91000000 and (v>>5)&31==reg:
   t=page+((v>>10)&4095); refs[t].append(p);val=string(t)
   if val:srefs[owner(p)].append((p,t,val))
  # Conservatively stop at an apparent write to the page register. This avoids
  # pairing an ADRP with an ADD after that same register was repurposed.
  if v&31==reg:break
# Relocation pointer references: vtables and static descriptors.
m=struct.unpack_from('<I',b,4)[0];d=m+struct.unpack_from('<i',b,m+4)[0];tags={}
while True:
 t,v=struct.unpack_from('<QQ',b,d);d+=16
 if not t:break
 tags[t]=v
rel={};rev=collections.defaultdict(list)
for p in range(tags[7],tags[7]+tags[8],24):
 off,info,add=struct.unpack_from('<QQq',b,p)
 if info&0xffffffff==1027:rel[off]=add;rev[add].append(off)
result={'fns':fns,'calls':calls,'refs':refs,'srefs':srefs,'hits':hits,'rel':rel,'rev':rev}
Path('work/timing-index.json').write_text(json.dumps(result))
out=[]
for p in hits:
 f=owner(p);out.append(f'\nSITE {p:x} FN {f:x} SIZE {dict(fns)[f]}')
 own=srefs.get(f,[]);out.extend(' LOCAL '+v for a,t,v in own[:12])
 cf=sorted({owner(a) for a in calls.get(f,[])+refs.get(f,[]) if owner(a)!=f and owner(a)!=None})
 out.append(' CALLERS '+','.join(hex(a) for a in cf[:15]))
 for c in cf[:15]:
  vals=srefs.get(c,[])
  if vals:out.append(f' CALLER {c:x} '+', '.join(v for a,t,v in vals[:8]))
 out.append(' VTABLE '+','.join(hex(a) for a in rev.get(f,[])[:8]))
Path('work/40-context.txt').write_text('\n'.join(out))
print('Indexed',len(fns),'functions;',len(hits),'1/30 sites;',len(srefs),'functions with string refs')
