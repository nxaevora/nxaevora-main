from models import Asset
from texture import texture
from readpak import *
from PIL import Image
import numpy as np
root=game/'archive/field/terrain'
destination=Path(os.environ['ARCEUS_TERRAIN_DIR'])
colors={}
paths=sorted(root.glob('ha_area??.gfpak'))
for path in paths:
 for _,_,data in entries(path):
  if data[:4]!=b'BNTX':continue
  name,fmt,w,h,mips=texture(data)
  if not name.endswith('_alb') or name in colors:continue
  bc={0x1a:1,0x1b:2,0x1c:3,0x20:7}.get(fmt>>8)
  if not bc or max(w,h)>>(len(mips)-1)>4:continue
  block=Image.frombytes('RGBA',(4,4),data[mips[-1]:mips[-1]+16],'bcn',(bc,'BC'+str(bc)))
  value=np.array(block.getpixel((0,0))[:3])/255.
  colors[name[:-4]]=np.where(value<=.04045,value/12.92,((value+.055)/1.055)**2.4) if (fmt&255)==6 else value
for path in paths:
 target=destination/(path.stem+'.land')
 if not target.exists():continue
 binData=target.read_bytes()
 magic,version,tiles,length,w,h=struct.unpack_from('<4sIIIII',binData)
 if version not in (1,2) or tiles!=1:raise ValueError('invalid land input')
 for _,_,data in entries(path):
  if len(data)<1000000 or data[:4]==b'BNTX' or data[4:8]==b'TAG0':continue
  a=Asset(data)
  try:
   t=a.root();gw=a.number(t,3);gh=a.number(t,4);offset,n=a.vec(t,5)
   if not(33<gw<=4097 and n==gw*gh):continue
   layers=a.tables(t,8)
  except (struct.error,ValueError,IndexError,TypeError):continue
  accum=np.zeros((h,w,3));total=np.zeros((h,w));unresolved=[]
  allweights=np.zeros((h,w))
  x=np.clip(np.rint(np.linspace(0,gw-1,w)).astype(int),0,gw-2)
  z=np.clip(np.rint(np.linspace(0,gh-1,h)).astype(int),0,gh-2)
  for layer in layers:
   name=a.string(layer,0);off,count=a.vec(layer,1)
   side=round(count**.5)
   if side<2 or side*side!=count:continue
   sample=np.clip(np.rint(np.linspace(0,side,w)).astype(int),0,side-1)
   weights=np.frombuffer(data,'u1',count,off).reshape(side,side)[np.ix_(sample,sample)]
   allweights+=weights
   col=colors.get(name)
   if col is None and name.endswith('ll01'):col=colors.get(name.replace('ll01','l01'))
   if col is None:unresolved.append(name);continue
   accum+=weights[:,:,None]*col;total+=weights
  rgb=accum/np.maximum(1,total)[:,:,None]
  srgb=np.where(rgb<=.0031308,12.92*rgb,1.055*np.maximum(rgb,0)**(1/2.4)-.055)
  rgba=np.empty((h,w,4),dtype='u1');rgba[:,:,:3]=np.round(255*np.clip(srgb,0,1))
  rgba[:,:,3]=np.round(255*np.minimum(1,total/np.maximum(1,allweights)))
  updated=bytearray(binData[:40+w*h*4]);struct.pack_into('<I',updated,4,2)
  target.write_bytes(updated+rgba.tobytes())
  print(path.stem,len(layers),'resolved weight',round(total.sum()/max(1,allweights.sum()),3),unresolved[:4])
  break
