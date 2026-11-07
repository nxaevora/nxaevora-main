import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import subprocess
f=0xcc3888;describe(f)
for p in D['calls'].get(f,[]):
 q=owner(p);print(hex(p),hex(q),vals(q))
fs=[f,*callers(f)]
subprocess.run([sys.executable,'work/pass6-resolution/disasm.py',*[hex(f) for f in fs]],check=True)
