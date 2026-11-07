import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f in [0xc8ae6c,0x144d8fc,0x144c2b8,0x148ff9c]:describe(f)
for n in range(2,8):print('mode',n,'target',hex(0xcc38d8+4*struct.unpack_from('<H',b,0x3978732+(n-2)*2)[0]))
for p in range(0x3978758,0x3978778,4):print(hex(p),struct.unpack_from('<f',b,p)[0])
