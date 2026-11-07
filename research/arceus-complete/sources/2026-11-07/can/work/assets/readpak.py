from pathlib import Path
import struct,ctypes,re,sys,collections,os
here=Path(__file__).resolve().parent
game=Path.home()/'Documents/Codex/work/arceus-games/ArceusTest/ArceusTest_data/romfs/bin'
dll_path=os.add_dll_directory(str(Path.home()/'Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin'))
dll=ctypes.CDLL(str(here/'ooz.dll'))
dll.ooz_decompress.argtypes=(ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t)
def entries(path):
    b=path.read_bytes()
    assert b[:8]==b'GFLXPACK'
    n=struct.unpack_from('<I',b,16)[0];table=struct.unpack_from('<Q',b,24)[0]
    for i in range(n):
        typ,codec,size,packed,align,offset=struct.unpack_from('<HHIIIQ',b,table+i*24)
        payload=b[offset:offset+packed]
        if size>256<<20 or len(payload)!=packed:raise ValueError('bad member size')
        if codec==3:
            dst=ctypes.create_string_buffer(size+64)
            got=dll.ooz_decompress(payload,packed,dst,size)
            if got!=size:raise ValueError((i,got,size))
            data=dst.raw[:size]
        elif packed==size:data=payload
        else:raise ValueError((typ,codec,size,packed))
        yield i,typ,data
def strings(b):
    return [s.decode('ascii') for s in re.findall(rb'[a-zA-Z0-9_./-]{5,}',b)]
if __name__=='__main__':
    for folder in ['terrain','model/pack']:
        for area in ['ha_area00_s01','ha_area00']:
            path=game/'archive/field'/folder/(area+'.gfpak')
            groups=collections.Counter()
            text=[]
            for i,t,b in entries(path):
                groups[t]+=1
                hits=[s for s in strings(b) if any(v in s.lower() for v in ['.trm','height','collision','area00','terrain','placement','ground_','house_'])]
                text.append(f'{i} {t} {len(b)} {b[:12].hex()} '+repr(hits[:12])[:350])
            (here/(folder.split('/')[0]+'-'+area+'.txt')).write_text('\n'.join(text))
            print(folder,area,len(text),groups)
