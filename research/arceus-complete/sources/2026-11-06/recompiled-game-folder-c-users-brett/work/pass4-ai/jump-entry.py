import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,n in fns:
 if 0xaf1a58<=f<=0xaf22e8:print(hex(f),hex(n))
