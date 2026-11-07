import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
import re
for f,z in fns:
 hits=[s for s in vals(f) if re.search('3DBuffer|Resolution|resolution',s)]
 if hits:print(hex(f),[s[:220] for s in hits][:12])
