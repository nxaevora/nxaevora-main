import sys
sys.path.pop(0)
from pathlib import Path
import json,subprocess,bisect
d=json.loads(Path('work/timing-index.json').read_text())
fns=d['fns'];starts=[f for f,n in fns]
out=[]
for pc in d['calls'][str(0x1059a78)]:
    f,n=fns[bisect.bisect_right(starts,pc)-1]
    begin=max(f,pc-40);end=min(f+n,pc+100)
    out.append(f'Consumer main+{f:X}, getter call main+{pc:X}\n')
    text=subprocess.check_output([sys.executable,'work/inspect.py',f'{begin:x}',f'{end-begin:x}'],text=True)
    out.append(text)
Path('work/script-frame-consumer-operations.txt').write_text('\n'.join(out))
print(f'{len(out)//2} call sites inspected; local disassembly saved.')
