exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import collections
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
w=memoryview(b[:n]).cast('I');out=[];hits=[]
for i,a in enumerate(w):
 if a&0xffc00000!=0xb9400000:continue
 r=a&31;rn=(a>>5)&31;off=((a>>10)&4095)*4
 if rn in (29,31):continue
 for j in range(i+1,min(i+7,len(w))):
  v=w[j]
  if v&0x1f000000==0x11000000 and (v>>10)&4095==1 and (v>>5)&31==r and v&31==r:
   sign='-' if v&0x40000000 else '+'
   for k in range(j+1,min(j+5,len(w))):
    z=w[k]
    if z&0xffc00000==0xb9000000 and (z&0x003fffff)==(a&0x003fffff):
     p=i*4;f=owner(p)
     if f:
      hits.append([p,j*4,k*4,f,off,sign]);out.append(f'{p:x} {sign}=1 field={off:x} fn={f:x} '+str(vals(f))[:500])
     break
    if z&0x7c000000==0x14000000:break
   break
  isbranch=(v&0x7e000000 in (0x34000000,0x36000000)) or v&0xff000010==0x54000000
  if v&0x7c000000==0x14000000 or (v&31==r and not isbranch):break
Path('work/frame-counter-hits.txt').write_text('\n'.join(out));Path('work/frame-counter-hits.json').write_text(json.dumps(hits))
print('Potential object counters:',len(hits),'in',len(set(t[3] for t in hits)),'functions. Not all are timers.')
for s,h in zip(out,hits):
 if any(v.lower() in s.lower() for v in ['message','camera','blink','wait','skip','idle']) or 0x1930000<h[0]<0x1960000 or 0x2730000<h[0]<0x2740000:print(s)
