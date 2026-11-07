import sys
sys.path.pop(0)
from pathlib import Path
import difflib
before=Path('work/pass3/arceus_clock-before-pass3.c').read_text()
after=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c').read_text()
Path('work/pass3/runtime-probe.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='before-pass3',tofile='after-pass3')))
print('Runtime changes:',len(after.splitlines())-len(before.splitlines()),'added lines; diff saved locally')
# Compact initializer-to-use evidence, retaining complete addresses.
text=Path('work/pass3/180c338.txt').read_text().splitlines()
ranges=[(0x180c7e0,0x180c81c),(0x180d000,0x180d060),(0x180d3a8,0x180d3e4),(0x180d694,0x180d6d0),(0x180db10,0x180db64),(0x180dccc,0x180dcd8),(0x180de00,0x180de5c),(0x180e72c,0x180e750)]
out=[]
for a,z in ranges:
 out += ['']+[x for x in text if len(x)>9 and x[8]==':' and a<=int(x[:8],16)<=z]
Path('work/pass3/capture-timer-initializers.txt').write_text('\n'.join(out)+'\n')
