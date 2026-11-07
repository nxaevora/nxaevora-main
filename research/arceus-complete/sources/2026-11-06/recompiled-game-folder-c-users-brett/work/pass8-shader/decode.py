import sys
sys.path.pop(0)
from pathlib import Path
import struct,re,json
root=Path('work/pass8-shader')
source=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/shader/maxwell_opcodes.def').read_text()
patterns=[]
for line in source.splitlines():
 m=re.match(r'(\w+)\s+"[^"]+"\s+"([01 -]+)"',line)
 if not m:continue
 p=m[2].replace(' ','');mask=int(''.join('0' if c=='-' else '1' for c in p),2);value=int(p.replace('-','0'),2)
 patterns.append((mask.bit_count(),mask,value,m[1]))
patterns.sort(reverse=True)
def B(w,lo,n):return (w>>lo)&((1<<n)-1)
def op(w):return next((name for _,mask,value,name in patterns if (w>>48)&mask==value),'UNKNOWN')
def load(path):
 data=Path(path).read_bytes();magic,version,infosz,stage,ncode,maximum=struct.unpack_from('<4s5I',data)
 assert magic==b'SNXS';code=data[24+infosz:24+infosz+ncode]
 return struct.unpack('<'+'Q'*(len(code)//8),code)
def listing(words):
 rows=[]
 for i,w in enumerate(words):
  if i<11 or (i-10)%4==0:continue
  o=op(w);d,a,b,c=B(w,0,8),B(w,8,8),B(w,20,8),B(w,39,8);g=B(w,16,4)
  s=f'{i:04d} +{i*8:04x} {w:016x} P{g:x} {o:12s} d=R{d} a=R{a} b=R{b} c=R{c}'
  if o=='IPA':s+=f' attr={B(w,30,8)*4:03x} mul={B(w,54,2)} sat={B(w,51,1)}'
  if o.endswith('_I') or o.endswith('32I'):
   imm=B(w,20,32) if o.endswith('32I') else B(w,20,19)|(B(w,56,1)<<19)
   bits=imm if o.endswith('32I') else imm<<12
   s+=f' imm=0x{imm:x} float={struct.unpack("<f",struct.pack("<I",bits))[0]:g}'
  if o.endswith('_C'):s+=f' cb={B(w,34,5)} off={B(w,20,14)*4:x}'
  if o.startswith('TLD') or o.startswith('TEX') or o.startswith('TXQ'):s+=f' tex={B(w,36,13):x} encoding={B(w,53,4)}'
  rows.append(s)
 return '\n'.join(rows)+'\n'
if __name__=='__main__':
 for arg in sys.argv[1:]:
  path=next(Path('work/shipped-fragment-corpus').glob(arg+'*.snxs'))
  root.joinpath(arg+'.txt').write_text(listing(load(path)))
  print(arg,len(load(path)))
