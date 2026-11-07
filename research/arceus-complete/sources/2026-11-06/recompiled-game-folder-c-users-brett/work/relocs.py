exec(open('work/inspect.py').read().split('if len(sys.argv)>1:')[0])
m=struct.unpack_from('<I',b,4)[0];d=m+struct.unpack_from('<i',b,m+4)[0]; tags={}
while True:
 t,v=struct.unpack_from('<QQ',b,d);d+=16
 if not t:break
 tags[t]=v
print('rela',hex(tags[7]),hex(tags[8]))
rel={}
for p in range(tags[7],tags[7]+tags[8],24):
 off,info,add=struct.unpack_from('<QQq',b,p)
 if info&0xffffffff==1027:rel[off]=add
for off in range(0x41714a0,0x4171590,8):print(hex(off),hex(rel.get(off,0)))
