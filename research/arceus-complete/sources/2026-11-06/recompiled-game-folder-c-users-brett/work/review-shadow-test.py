import sys
sys.path.pop(0)
from pathlib import Path
import csv,json,struct,zlib
cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
out=Path('work/shadow-test-review');out.mkdir(exist_ok=True)
def png(p,w,h,rgb):
    def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
    raw=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    p.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
for p in cap.glob('screenshot_20261106_181*.ppm'):
    magic,dim,maxv,data=p.read_bytes().split(b'\n',3)
    assert magic==b'P6' and maxv==b'255'
    w,h=map(int,dim.split());assert len(data)==w*h*3
    png(out/(p.stem+'.png'),w,h,data)
result=[]
for p in sorted(cap.glob('screenshot_20261106_181*_attachments.tsv')):
    rows=list(csv.DictReader([x for x in p.read_text().splitlines() if not x.startswith('#')],delimiter='\t'))
    item={'manifest':p.name,'count':len(rows),'copies':[]}
    for key,addresses in [('1000a',['54a8a0000','54aca0000']),('f6',['53aca0000','53ada0000'])]:
        sources=[next(r for r in rows if r['gva']==a and r['is_rt']=='1' and r['fkey']==key) for a in addresses]
        dest=next(r for r in rows if r['gva']==addresses[0] and r['is_rt']=='0' and r['layers']=='2')
        raw=Path(dest['file']).read_bytes();layerbytes=int(dest['layer_bytes'])
        for i,s in enumerate(sources):
            assert s['image_w']==s['image_h']=='1024'
            data=Path(s['file']).read_bytes();copied=raw[i*layerbytes:(i+1)*layerbytes]
            mismatch=sum(a!=b for a,b in zip(data,copied))
            assert len(data)==len(copied)
            item['copies'].append({'gva':s['gva'],'format':key,'layer':i,'mismatched_bytes':mismatch,'bytes':len(data)})
    for address in ['540a88000','552a80000']:
        r=next(r for r in rows if r['gva']==address and r['is_rt']=='1'); data=Path(r['file']).read_bytes()
        w,h,bpp=map(int,[r['image_w'],r['image_h'],r['bpp']])
        if bpp==1:rgb=bytes(v for b in data for v in (b,b,b))
        elif bpp==4:rgb=bytes(b for i,b in enumerate(data) if i%4!=3)
        else:continue
        png(out/(p.stem+'_'+address+'.png'),w,h,rgb)
    result.append(item)
(out/'copy-verification.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
