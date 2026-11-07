exec(open('work/inspect.py').read().split('if len(sys.argv)>1:')[0])
n=struct.unpack_from('<I',Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/exefs/main').read_bytes(),0x18)[0]
ws=struct.unpack_from(f'<{n//4}I',b)
for i,w in enumerate(ws):
 if w&0x7f800000==0x52800000 and ((w>>5)&65535) in (0x502b,0xa056): print('constant',hex(i*4))
 if w>>26==0x25:
  imm=w&0x3ffffff; imm=imm-(1<<26) if imm&(1<<25) else imm
  if (i+imm)*4==0x2783048: print('caller',hex(i*4))
