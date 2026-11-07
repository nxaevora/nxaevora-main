from readpak import *
import itertools
class FB:
    def __init__(self,b): self.b=b
    def u32(self,p): return struct.unpack_from('<I',self.b,p)[0]
    def ref(self,p): return p+self.u32(p)
    def fields(self,p):
        vt=p-struct.unpack_from('<i',self.b,p)[0]
        if not 0<=vt<len(self.b)-4:raise ValueError('vtable')
        size=struct.unpack_from('<H',self.b,vt)[0]
        if not 4<=size<=512:raise ValueError('vtable size')
        return [(p+o if o else None) for o in struct.unpack_from('<'+'H'*((size-4)//2),self.b,vt+4)]
    def dump(self,p):
        print('table',p)
        for i,off in enumerate(self.fields(p)):
            if off is None:continue
            val=self.u32(off); dest=off+val; txt=''
            if off<dest<len(self.b)-4:
                n=self.u32(dest);txt=f' -> {dest} first={n} payload={self.b[dest+4:dest+32].hex(" ")}'
            print(i,off,hex(val),round(struct.unpack_from('<f',self.b,off)[0],3),txt)
if __name__=='__main__':
 for i,k,data in itertools.islice(entries(game/'archive/field/terrain/ha_area00.gfpak'),6):
    if i in [1,3,5]:
        print('entry',i);fb=FB(data);fb.dump(fb.u32(0))
        (here/('member'+str(i)+'.bin')).write_bytes(data)
