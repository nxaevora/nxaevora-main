exec(open('work/inspect.py').read().split('if len(sys.argv)>1:')[0])
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
ws=struct.unpack_from(f'<{n//4}I',b)
for i,w in enumerate(ws):
 if w & 0x9f000000 != 0x90000000: continue
 imm=((w>>29)&3)|(((w>>5)&0x7ffff)<<2)
 if imm & (1<<20): imm-=1<<21
 page=((base+i*4)&~4095)+(imm<<12); reg=w&31
 if page not in (base+0x397e000 & ~4095, (base+0x42ec288)&~4095): continue
 for j in range(i+1,min(i+15,len(ws))):
  q=ws[j]; rn=(q>>5)&31
  if rn!=reg: continue
  if q&0x3b000000==0x39000000: addr=page+(((q>>10)&4095)<<(q>>30))
  elif q&0x7f000000==0x11000000: addr=page+((q>>10)&4095)
  else: continue
  if addr in (base+0x397e8d0,base+0x397e8d8,base+0x42ec288): print(hex(i*4),hex(j*4),'->',hex(addr-base))

