import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for p in range(0,0x3520000,4):
 w=int.from_bytes(b[p:p+4],'little')
 if (w & 0xffc00000)==0x91000000 and ((w>>10)&4095)==0x530:
  f=owner(p)
  print(hex(p),hex(f or 0),f'x{w&31}=x{(w>>5)&31}+530',vals(f)[:3] if f else [])
