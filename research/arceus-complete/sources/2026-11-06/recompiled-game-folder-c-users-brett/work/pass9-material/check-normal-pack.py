import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,math
f=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
checks=0;errors=[];maximum=0.0
# Exhaust the cross-byte extraction. Actual 255-based UNORM conversion and f32 multiply.
for byte in range(256):
 n=f(byte/255);p=f(n*f(15.9375));expect=byte/16
 checks+=1
 if p!=expect:errors.append(['middle-byte-scale',byte,p,expect])
# Exhaust each12bit component with every nibble from the other one: independent packing identities.
for qx in range(4095):
 for other in range(16):
  b0=qx>>4;b1=((qx&15)<<4)|other
  x=f(math.fma(f(b0/255),4080,math.floor(f(f(b1/255)*15.9375))))
  e=abs(x-qx);maximum=max(maximum,e);checks+=1
  if e>0.001:errors.append(['x',qx,other,x])
for qy in range(4095):
 for other in range(16):
  b1=(other<<4)|(qy>>8);b2=qy&255
  p=f(f(b1/255)*15.9375);fract=f(p-math.floor(p))
  y=f(math.fma(fract,4096,f(f(b2/255)*255)))
  e=abs(y-qy);maximum=max(maximum,e);checks+=1
  if e>0.001:errors.append(['y',qy,other,y])
report={'cases':checks,'errors_gt_0_001':len(errors),'maximum_integer_reconstruction_error':maximum,'first_errors':errors[:10],'scope':'Arithmetic identities derived from producer words181-219 and receiver28-39, using binary32 UNORM reads. Not execution of complete emitted shader or GPU framebuffer.'}
Path('work/pass9-material/normal-pack-checks.json').write_text(json.dumps(report,indent=2));print(report)

