import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
m=struct.unpack_from('<I',b,4)[0];d=m+struct.unpack_from('<i',b,m+4)[0];tags={}
while True:
 t,v=struct.unpack_from('<QQ',b,d);d+=16
 if not t:break
 tags[t]=v
targets={0x4272ce0,0x4272cf0,0x4272d20,0x4272d28,0x4272d30,0x4272d38,0x4272d40,0x427a118,0x4270f20}
print('tags',tags)
for start,length in [(tags[7],tags[8]),(tags.get(23,0),tags.get(2,0))]:
 for p in range(start,start+length,24):
  off,info,add=struct.unpack_from('<QQq',b,p)
  if off not in targets:continue
  sym=info>>32;s=tags[6]+sym*24
  name,typ,other,sect,value,size1=struct.unpack_from('<IBBHQQ',b,s)
  st=tags[5]+name;en=b.find(b'\0',st)
  print(hex(off),'type',info&0xffffffff,'add',hex(add),'name',b[st:en].decode(),'value',hex(value),'size',size1,'float',struct.unpack_from('<6f',b,value) if value else None)
