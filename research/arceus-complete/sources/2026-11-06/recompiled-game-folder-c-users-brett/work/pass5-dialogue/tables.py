import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for i in range(7):print(i,hex(0x2b31634+4*struct.unpack_from('<H',b,0x39847f8+i*2)[0]))
