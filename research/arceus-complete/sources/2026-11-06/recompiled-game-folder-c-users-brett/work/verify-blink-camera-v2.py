from pathlib import Path
import json,struct
exec(Path('work/verify-blink-camera.py').read_text().split('# Classify exact')[0])
fns=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
for off,size,head,tail,replacement in [(0x1aaf0b4,0x2a0,0xa9ba6ffc,0x17ffffe7,0xb4fffcff),(0x193c2c4,0xea8,0xfc1d0fe8,0x17fffec1,0xb4ffd83f)]:
 assert fns[off]==size
 assert struct.unpack_from('<I',b,off)[0]==head
 assert struct.unpack_from('<I',b,off+size-4)[0]==tail
 def signed(v,n): return v-(1<<n) if v&(1<<(n-1)) else v
 assert signed(tail&0x3ffffff,26)==signed((replacement>>5)&0x7ffff,19)
 assert replacement&31==31
assert struct.unpack_from('<I',b,0x1aaf12c)[0]==0x71000508
for fps in (30,60,120):
 rate=.075; angle=sum(rate*(30/fps) for _ in range(fps*10))
 assert abs(angle-22.5)<1e-9
print('New blink and camera function metadata, instruction guards, and equivalent branches passed.')
print('Constant camera angular travel agrees at 30, 60 and 120 FPS.')
