from fb import *
from itertools import islice
for area in ['ha_area00','ha_area01','ha_area02','ha_area03','ha_area04','ha_area05']:
 p=game/'archive/field/terrain'/(area+'.gfpak')
 if not p.exists():continue
 print(area,p.stat().st_size)
 for i,t,b in islice(entries(p),30):
  if b.startswith((b'BNTX',b'GFL')) or len(b)<256:continue
  f=FB(b)
  try:
   fields=f.fields(f.u32(0))
   if len(fields)<10:continue
   a,bx,n=[f.u32(fields[j]) for j in (3,4,5)]
   if not(3<=a<=4097 and 3<=bx<=4097 and f.u32(f.ref(fields[5]))==a*bx):continue
   print(' tile',i,a,bx,[tuple(round(struct.unpack_from('<f',f.b,q)[0],4) if q else 0 for q in f.fields(f.ref(fields[j]))) for j in (0,1,2)])
  except (ValueError,struct.error,TypeError,IndexError): pass
