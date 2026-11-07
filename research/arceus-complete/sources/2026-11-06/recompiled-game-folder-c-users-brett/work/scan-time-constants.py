exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import collections
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
w=memoryview(b[:n]).cast('I')
targets={0x3d088889:'float 1/30',0x3d088888:'float truncated 1/30',0x3f800000:'float 1',0x3c888889:'float 1/60',0x3c088889:'float 1/120',0x3fa1111111111111:'double 1/30',33333333:'ns 1/30 floor',33333334:'ns 1/30 ceil',16666666:'ns 1/60 floor',16666667:'ns 1/60 ceil',8333333:'ns 1/120 floor',8333334:'ns 1/120 ceil'}
del targets[0x3f800000]
targets.update({0xbd088889:'float -1/30',0xbc888889:'float -1/60',0xbc088889:'float -1/120'})
hits=[]
for i,a in enumerate(w):
 p=i*4
 if a&0x7f800000==0x52800000:
  reg=a&31;v=((a>>5)&65535)<<(((a>>21)&3)*16);pcs=[p]
  if v in targets:hits.append((p,targets[v],'MOV',pcs.copy()))
  for j in range(i+1,min(i+13,len(w))):
   z=w[j]
   if z&0x7f800000==0x72800000 and z&31==reg:
    shift=((z>>21)&3)*16;v=(v&~(65535<<shift))|(((z>>5)&65535)<<shift);pcs.append(j*4)
    if v in targets:hits.append((p,targets[v],'MOV/MOVK',pcs.copy()))
   elif z&0x7f800000==0x52800000 and z&31==reg:break
   if z&0x7c000000==0x14000000:break
 if a&0x9f000000==0x90000000:
  reg=a&31;imm=((a>>29)&3)|(((a>>5)&0x7ffff)<<2)
  if imm&0x100000:imm-=0x200000
  page=(p&~4095)+(imm<<12)
  for j in range(i+1,min(i+7,len(w))):
   z=w[j]
   if z&0x3b000000!=0x39000000 or (z>>5)&31!=reg or not(z&0x00400000):continue
   scale=z>>30
   if z&0x04000000 and (z>>22)&3==3:scale=4
   t=page+(((z>>10)&4095)<<scale)
   if not 0x32a0000<=t<len(b)-16:continue
   for k in range(0,1<<scale,4):
    v=struct.unpack_from('<I',b,t+k)[0]
    if v in targets:hits.append((j*4,targets[v],'ADRP/load',[p,t+k]))
   if scale>=3:
    v=struct.unpack_from('<Q',b,t)[0]
    if v in targets:hits.append((j*4,targets[v],'ADRP/load64',[p,t]))
 if a&0x3b000000==0x18000000:
  imm=(a>>5)&0x7ffff
  if imm&0x40000:imm-=0x80000
  t=p+imm*4
  if 0x32a0000<=t<len(b)-8:
   v=struct.unpack_from('<I',b,t)[0]
   if v in targets:hits.append((p,targets[v],'literal',[t]))
hits=sorted(set((p,k,m,tuple(q)) for p,k,m,q in hits))
Path('work/expanded-time-hits.json').write_text(json.dumps(hits))
out=[]
for p,k,m,q in hits:
 f=owner(p)
 out.append(f'{p:x} {k}: {m} '+','.join(f'{a:x}' for a in q)+f' owner={f:x} '+str(vals(f))[:350] if f else f'{p:x} {k} no function')
Path('work/expanded-time-hits.txt').write_text('\n'.join(out))
print('Counts',dict(collections.Counter(k for p,k,m,q in hits)))
print('1/30 loads and nonadjacent constructions:')
for s,(p,k,m,q) in zip(out,hits):
 if ('1/30' in k and (p not in D['hits'])) or 'ns ' in k:print(s)
