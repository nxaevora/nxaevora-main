from nav import *
h=np.load(here/'village-height.npy')*185
def interpolate(grid,x,z):
 n=grid.shape[0];x=np.clip(x,0,n-1.00001);z=np.clip(z,0,n-1.00001)
 ix=x.astype(int);iz=z.astype(int);sx=x-ix;sz=z-iz
 return (1-sx)*(1-sz)*grid[iz,ix]+sx*(1-sz)*grid[iz,ix+1]+sz*(1-sx)*grid[iz+1,ix]+sx*sz*grid[iz+1,ix+1]
def score(points):
 x,y,z=points[:,:3].T
 mask=(x>=0)&(x<=1025)&(z>=0)&(z<=1025);x,y,z=x[mask],y[mask],z[mask]
 vals=[]
 for transpose in [0,1]:
  for flipx in [0,1]:
   for flipz in [0,1]:
    grid=h.T if transpose else h
    if flipx:grid=grid[:,::-1]
    if flipz:grid=grid[::-1,:]
    est=interpolate(grid,x*1024/1025,z*1024/1025)
    err=y-est
    vals.append((np.mean(abs(err)<.5),np.median(abs(err)),np.median(err),(transpose,flipx,flipz),np.percentile(err,[10,90])))
 return sorted(vals,key=lambda s:-s[0])[:2]
d,items=nav('ha_area00')
typ,offset,count=items[13]
a=np.frombuffer(d,'<f4',int(count)*4,int(offset)).reshape(-1,4)
print('nav',score(a))
for f in sorted(here.glob('street-*.bin'))[-4:]:
 a=np.fromfile(f,'<f4').reshape(-1,4);v=a[1:2041];print(f.name,a[0],score(v[v[:,3]>1e-6]))
