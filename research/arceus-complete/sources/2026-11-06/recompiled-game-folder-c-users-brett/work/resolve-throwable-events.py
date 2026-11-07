import sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for value in (0xee4f3d27318ed6e9,0xc9bc01a4300f3adb):
    needle=struct.pack('<Q',value); pos=0; found=[]
    while (pos:=b.find(needle,pos))>=0:
        found.append(pos);pos+=8
    print('HASH',hex(value),'data sites',[hex(p) for p in found])
    for p in found[:12]:
        for q in range(p-16,p+40,8):
            v=D['rel'].get(q)
            if v is not None and v<len(b):
                s=b[v:v+120].split(b'\0')[0]
                print(hex(q),hex(v),repr(s))
