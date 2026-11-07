from pathlib import Path
import struct,json
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
checks={0xc9a9b4:0x6db923e9,0xc9ac44:0x51000530,0xc9b298:0x17ffffea}
for o,w in checks.items():assert struct.unpack_from('<I',b,o)[0]==w,hex(o)
assert dict(json.loads(Path('work/timing-index.json').read_text())['fns'])[0xc9a9b4]==0x8e8
# Original collector countdown and separate pressed/repeated masks, with post-hook repair.
def run(fps,holds):
 counter=0; previous=0;phase=0;events=[];triggers=[]
 for frame,held in enumerate(holds,1):
  phase+=30; ticks=phase//fps;phase%=fps
  if not held or held!=previous:counter=15
  elif not counter:counter=3
  counter=(counter-1)&0xffffffff
  pressed=held&~previous;repeat=held if counter==0 else 0
  if not ticks:counter=(counter+1)&0xffffffff;repeat=0
  if repeat:events.append(frame/fps)
  if pressed:triggers.append((frame,pressed))
  previous=held
 return events,triggers
for fps in [30,60,120]:
 events,triggers=run(fps,[1]*(fps*3))
 assert triggers==[(1,1)]
 assert abs(events[0]-.5)<1e-9,(fps,events)
 assert all(abs((y-x)-.1)<1e-9 for x,y in zip(events,events[1:]))
 _,triggers=run(fps,[1,0,2,0,1])
 assert triggers==[(1,1),(3,2),(5,1)],(fps,triggers)
print('Repeat timing: 500ms initial wait and 100ms intervals at 30/60/120 for authored 15/3; one-frame presses preserved.')
f=struct.unpack('<f',struct.pack('<f',4/3))[0]
assert int(1920*f+.5)==2560 and int(1080*f+.5)==1440
print('Vulkan float scale rounds 1920x1080 to 2560x1440.')
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
a=Path('work/arceus-clock-before-v4.c').read_text();z=p.read_text()
extract=lambda s:s[s.index('static GuestFn blink_original'):s.index('static GuestFn camera_input_original')]
assert extract(a)==extract(z)
print('Confirmed blinking code unchanged; menu binary/function guards pass.')
