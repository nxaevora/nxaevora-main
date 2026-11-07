from readpak import *
from PIL import Image
import numpy as np
def texture(blob):
    table=struct.unpack_from('<Q',blob,0x28)[0];offset=struct.unpack_from('<Q',blob,table)[0]
    fmt=struct.unpack_from('<I',blob,offset+0x1c)[0]
    w,h=struct.unpack_from('<II',blob,offset+0x24)
    layers=struct.unpack_from('<H',blob,offset+0x16)[0]
    name=struct.unpack_from('<Q',blob,offset+0x60)[0]
    name=blob[name+2:blob.index(0,name+2)].decode('ascii')
    ptr=struct.unpack_from('<Q',blob,offset+0x70)[0]
    mip=struct.unpack_from('<'+'Q'*layers,blob,ptr)
    return name,fmt,w,h,mip
if __name__=='__main__':
 for _,_,b in entries(game/'archive/field/terrain/ha_area00.gfpak'):
    if b[:4]!=b'BNTX':continue
    n,f,w,h,offs=texture(b)
    if '_alb' not in n:continue
    print(n,hex(f),w,h,len(offs),offs[-1],b[offs[-1]:offs[-1]+16].hex())
    typ={0x1a:1,0x1b:2,0x1c:3,0x20:7}.get(f>>8)
    if typ:
      im=Image.frombytes('RGBA',(4,4),b[offs[-1]:offs[-1]+16],'bcn',(typ,'BC'+str(typ)))
      print(im.getpixel((0,0)))
    break
