import sys
sys.path.pop(0)
from pathlib import Path
import struct,json,hashlib,subprocess,zlib,math
out=Path('work/pass9-render/const05');out.mkdir(exist_ok=True)
p=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/model/pack/objects_area00_const05_01_nrm.bntx');b=p.read_bytes()
u32=lambda o:struct.unpack_from('<I',b,o)[0]
u64=lambda o:struct.unpack_from('<Q',b,o)[0]
u16=lambda o:struct.unpack_from('<H',b,o)[0]
assert b[:4]==b'BNTX' and b[0x20:0x24]==b'NX  ' and u32(0x24)==1
tex=u64(u64(0x28));assert b[tex:tex+4]==b'BRTI'
w,h=u32(tex+0x24),u32(tex+0x28);levels=u16(tex+0x16);bh=u32(tex+0x34)&7;fmt=u32(tex+0x1c);size=u32(tex+0x50);ptr=u64(tex+0x70);mips=[u64(ptr+8*i) for i in range(levels)]
assert w==h==1024 and levels==11 and bh==4 and fmt==0x1e01
assert b[mips[0]-16:mips[0]-12]==b'BRTD'
rows=[]
for l,off in enumerate(mips):
 lw,lh=max(1,w>>l),max(1,h>>l);tw,th=(lw+3)//4,(lh+3)//4;gx=(tw*16+63)//64;gy=(th+7)//8;lbh=bh
 while lbh>0 and gy<=1<<(lbh-1):lbh-=1
 length=gx*((gy+(1<<lbh)-1)>>lbh)*(1<<lbh)*512
 end=mips[l+1] if l+1<len(mips) else mips[0]+size
 assert off+length==end and end<=len(b),(l,off,length,end)
 rows.append(dict(level=l,width=lw,height=lh,offset=off,size=length,block_height_log2=lbh))
 if l not in [0,2]:continue
 blocks=bytearray(tw*th*16)
 for y in range(th):
  for x in range(tw):
   xb=x*16;addr=(y//(8<<lbh))*gx*(512<<lbh)+(xb//64)*(512<<lbh)+((y%(8<<lbh))//8)*512+((xb&32)<<3)+((xb&16)<<1)+(xb&15)+(((y&7)>>1)<<6)+((y&1)<<4)
   assert addr+16<=length
   blocks[(y*tw+x)*16:(y*tw+x+1)*16]=b[off+addr:off+addr+16]
 bp=out/f'level{l}.bc5';bp.write_bytes(blocks);raw=out/f'level{l}.rg16';subprocess.run([str(Path('work/pass9-render/decode-bc5.exe').resolve()),str(lw),str(lh),str(bp),str(raw)],check=True)
 def png(name,data):
  def chunk(t,p):return struct.pack('>I',len(p))+t+p+struct.pack('>I',zlib.crc32(t+p))
  scan=b''.join(b'\0'+data[y*lw*3:(y+1)*lw*3] for y in range(lh));(out/name).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',lw,lh,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
 rg=struct.unpack('<'+'H'*(lw*lh*2),raw.read_bytes());normal=bytearray();shade=bytearray();ranges=[min(rg[::2]),max(rg[::2]),min(rg[1::2]),max(rg[1::2])]
 for i in range(lw*lh):
  nx,ny=rg[2*i]/65535*2-1,rg[2*i+1]/65535*2-1;nz=math.sqrt(max(0,1-nx*nx-ny*ny));normal.extend((round((nx*.5+.5)*255),round((ny*.5+.5)*255),round((nz*.5+.5)*255)));v=round(max(0,min(1,.3*nx+.4*ny+math.sqrt(.75)*nz))*255);shade.extend((v,v,v))
 png(f'level{l}-normal.png',normal);png(f'level{l}-illustrative-shading.png',shade);rows[-1]['decoded_rg16_ranges']=ranges
result=dict(source=str(p),sha256=hashlib.sha256(b).hexdigest(),file_bytes=len(b),brti=tex,raw_format=hex(fmt),width=w,height=h,levels=levels,layout_word=hex(u32(tex+0x34)),mip_table=ptr,data_size=size,component_bytes=list(b[tex+0x58:tex+0x5c]),all_mip_spans_match_16byte_4x4_blocks=True,mips=rows,interpretation='BC5_UNORM decoding hypothesis: conventional 0x1e01 value, 16-byte4x4 sizes and two-channel-normal content consistent; no local BNTX enum definition found. Named loose asset not matched to screenshot.')
(out/'metadata.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
