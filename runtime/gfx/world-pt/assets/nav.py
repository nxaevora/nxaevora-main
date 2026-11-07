from readpak import *
import numpy as np
def sections(blob,lo,hi):
 while lo<hi:
  tagged=int.from_bytes(blob[lo:lo+4],'big');n=tagged&0x3fffffff;label=blob[lo+4:lo+8].decode('ascii')
  if n<8 or lo+n>hi: raise ValueError((lo,n,hi))
  yield label,lo+8,lo+n
  if tagged>>30==0:yield from sections(blob,lo+8,lo+n)
  lo+=n
def nav(area):
 data=next(d for i,t,d in entries(game/'archive/field/terrain'/(area+'.gfpak')) if d[4:8]==b'TAG0')
 parts={tag:data[a:b] for tag,a,b in sections(data,0,len(data))}
 return parts['DATA'],np.frombuffer(parts['ITEM'],dtype='<u4').reshape(-1,3)
if __name__=='__main__':
 data,items=nav('ha_area00')
 for j,(typ,offset,count) in enumerate(items):
  if count<30:continue
  end=items[j+1,1] if j+1<len(items) else len(data)
  if end<offset:continue
  stride=round((end-offset)/int(count),2)
  if stride in [16.,16.01,16.02] and count>500:
   a=np.frombuffer(data,dtype='<f4',count=int(count)*4,offset=int(offset)).reshape(-1,4)
   print(j,hex(typ),count,stride,'quantile',np.round(np.nanpercentile(a[::10],[5,50,95],axis=0),2).tolist())
