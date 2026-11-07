exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for t in [0x3878507,0x3802f06]: print(hex(t),b[t:b.index(0,t)].decode())
