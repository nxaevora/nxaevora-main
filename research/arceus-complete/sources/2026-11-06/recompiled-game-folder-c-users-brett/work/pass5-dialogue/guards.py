import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for f,offsets in [(0xfbfbd0,[0,0x24,0x12c,0x158]),(0x27379b8,[0,0xc,0x40]),(0xfc10e4,[0,0x1c,0x20])]:
 print(hex(f),hex(size[f]))
 for off in offsets:print(hex(off),hex(struct.unpack_from('<I',b,f+off)[0]))
print('equivalent final CBZ XZR',hex(0xb4000000|(((0xfbfcc4-0xfbfd28)//4)&0x7ffff)<<5|31))
