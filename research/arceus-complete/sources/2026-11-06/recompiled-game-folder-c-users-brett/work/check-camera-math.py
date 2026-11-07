from pathlib import Path
import struct
f=lambda v: struct.unpack('<f',struct.pack('<f',v))[0]
for fps in (30,60,120):
 rate=f(1/90); progress=0.; n=0; ns=round(1e9/fps)
 while progress<1:
  progress=f(progress+f(rate*ns/33333334.)); n+=1
 assert abs(n/fps-3)<=1/fps+1e-6
 print(f'{fps} FPS: nominal 3-second move finishes in {n/fps:.6f}s ({n} updates)')
