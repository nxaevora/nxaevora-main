import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
print('UI clock denominator table',[(hex(p),struct.unpack_from('<Q',b,p)[0]) for p in range(0x39848e0,0x3984910,8)])
for t in [0xfb6d94,0xc21490,0xc26384,0xc26428]:
 print('function',hex(t),'strings',vals(t),'callers',len(callers(t)))
