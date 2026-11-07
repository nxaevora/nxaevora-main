import sys
sys.path.pop(0)
from pathlib import Path
import struct,re
h=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/common/utilities/spirv_emit.h').read_text()
names={int(v,0):n for n,v in re.findall(r'SPV_OP_(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)',h)}
for sha in ['815803708190e019','b75440cca42d33f5','e9e7255e7594a006']:
 p=Path('work/shipped-fragment-candidate')/(sha+'_4.spv')
 w=struct.unpack('<'+'I'*(p.stat().st_size//4),p.read_bytes());at=5;lines=[]
 while at<len(w):
  n=w[at]>>16;op=w[at]&65535;a=w[at+1:at+n];s=f'{at:04d} {names.get(op,str(op))} '+','.join(map(str,a))
  if op==43:s+=f' [asfloat {struct.unpack("<f",struct.pack("<I",a[2]))[0]:.12g}]'
  lines.append(s);at+=n
 (Path('work/pass8-render')/(sha+'.spv.txt')).write_text('\n'.join(lines)+'\n')
print(Path('work/pass8-render/815803708190e019.spv.txt').read_text())
