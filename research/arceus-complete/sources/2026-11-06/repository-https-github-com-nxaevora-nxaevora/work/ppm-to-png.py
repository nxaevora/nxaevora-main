import pathlib,struct,zlib,collections,sys
p=pathlib.Path(sys.argv[1])
with p.open('rb') as f:
    assert f.readline().strip()==b'P6'
    w,h=map(int,f.readline().split()); assert f.readline().strip()==b'255'
    rgb=f.read()
def chunk(t,b): return struct.pack('!I',len(b))+t+b+struct.pack('!I',zlib.crc32(t+b)&0xffffffff)
raw=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
print(w,h, 'nonzero bytes', sum(v!=0 for v in rgb))
