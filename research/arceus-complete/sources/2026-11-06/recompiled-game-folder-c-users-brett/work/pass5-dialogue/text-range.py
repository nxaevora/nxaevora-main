import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import subprocess
fs=[hex(f) for f,z in fns if 0xfbfbd0<=f<0xfc27d0]
subprocess.run([sys.executable,'work/pass5-dialogue/disasm.py',*fs],check=True,stdout=subprocess.DEVNULL)
