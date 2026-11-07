import sys
sys.path.pop(0)
from pathlib import Path
import struct,zlib,math,json,collections
p=Path(r'C:/Users/brett/AppData/Local/Temp/codex-clipboard-cafe8c76-76d7-4438-aee6-6e6facdb3dd5.png')
b=p.read_bytes(); assert b[:8]==b'\x89PNG\r\n\x1a\n'
at=8;chunks=[]
while at<len(b):
 n=struct.unpack_from('>I',b,at)[0];kind=b[at+4:at+8];payload=b[at+8:at+8+n];at+=n+12
 if kind==b'IHDR':width,height,depth,color,comp,filt,inter=struct.unpack('>IIBBBBB',payload)
 if kind==b'IDAT':chunks.append(payload)
assert (depth,color,comp,filt,inter)==(8,6,0,0,0)
raw=zlib.decompress(b''.join(chunks));stride=width*4;prev=bytearray(stride);rows=[]
def paeth(a,b,c):
 p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
 return a if pa<=pb and pa<=pc else b if pb<=pc else c
for y in range(height):
 at=y*(stride+1);f=raw[at];r=bytearray(raw[at+1:at+1+stride])
 for i in range(stride):
  a=r[i-4] if i>=4 else 0;c=prev[i-4] if i>=4 else 0
  add=0 if f==0 else a if f==1 else prev[i] if f==2 else (a+prev[i])//2 if f==3 else paeth(a,prev[i],c)
  r[i]=(r[i]+add)&255
 rows.append([.2126*r[i]+.7152*r[i+1]+.0722*r[i+2] for i in range(0,stride,4)])
 prev=r
regions={'stairs_middle':(550,160,1250,370),'right_paving':(1400,20,1590,300),'trousers':(235,150,360,350),'ui_background':(180,520,850,552)}
result={'source':str(p),'size':[width,height],'method':'Read original PNG pixels only. Four-neighbor Laplacian residual, phase averages, no image editing/resampling. Scores indicate phase-locked periodic energy, not a rendering cause.','regions':{}}
for name,(x0,y0,x1,y1) in regions.items():
 vals=[(x,y,4*rows[y][x]-rows[y][x-1]-rows[y][x+1]-rows[y-1][x]-rows[y+1][x]) for y in range(y0,y1) for x in range(x0,x1)]
 total=sum(v*v for x,y,v in vals)/len(vals)
 scores=[]
 for period in range(2,17):
  sums=[0.]*(period*period);counts=[0]*(period*period)
  for x,y,v in vals:
   i=(y%period)*period+x%period;sums[i]+=v;counts[i]+=1
  means=[s/n if n else 0 for s,n in zip(sums,counts)]
  energy=sum(m*m*n for m,n in zip(means,counts))/len(vals)
  record={'period':period,'explained_energy_fraction':energy/total if total else 0,'phase_rms':math.sqrt(energy)}
  if period==4:record['global_xy_phase_mean']=[means[i:i+4] for i in range(0,16,4)]
  scores.append(record)
 result['regions'][name]={'box':[x0,y0,x1,y1],'residual_rms':math.sqrt(total),'strongest':sorted(scores,key=lambda s:s['explained_energy_fraction'],reverse=True)[:5],'period4':scores[2]}
Path('work/pass8-review/image-periodicity.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
