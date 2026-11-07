"""Check the diffuse/triangle balance weights against direct area integration."""
import numpy as np

rng = np.random.default_rng(0xDE1A)
count = 1_000_000
height = 2.
probability = .75
# Two coplanar disjoint rectangular emitters with different sizes and radiance.
lights = [(-.7, -.65, .08, .09, 14.), (.1, .2, .17, .05, 5.)]
total_triangles = 2*len(lights)
reference = 0.
for x,y,w,h,radiance in lights:
    u,v=np.meshgrid((np.arange(256)+.5)/256,(np.arange(256)+.5)/256)
    d2=(x+w*u)**2+(y+h*v)**2+height**2
    reference += (radiance*height**2/(np.pi*d2**2)).mean()*w*h

def densities(x,y,area):
    d=np.sqrt(x*x+y*y+height**2)
    cosine=height/d
    return probability*cosine/np.pi, 2*d*d/(total_triangles*area*cosine), cosine

# The two triangle halves give uniform samples over each rectangle.
index=rng.integers(len(lights),size=count)
x0,y0,w,h,power=np.asarray(lights)[index].T
x=x0+w*rng.random(count)
y=y0+h*rng.random(count)
bsdf,light,cosine=densities(x,y,w*h)
explicit=power*cosine/(np.pi*(bsdf+light))
cosine=np.sqrt(rng.random(count))
phi=2*np.pi*rng.random(count)
r=height*np.sqrt(1-cosine*cosine)/cosine
x,y=r*np.cos(phi),r*np.sin(phi)
take=rng.random(count)<probability
implicit=np.zeros(count)
original=np.zeros(count)
for x0,y0,w,h,power in lights:
    hit=take & (x>=x0) & (x<x0+w) & (y>=y0) & (y<y0+h)
    bsdf,light,_=densities(x[hit],y[hit],w*h)
    implicit[hit]=power/probability*bsdf/(bsdf+light)
    original[hit]=power/probability
result=explicit+implicit
assert abs(result.mean()-reference)<5*result.std()/np.sqrt(count)
assert result.var()<original.var()/100
print(f"Reference irradiance / pi {reference:.7f}, balanced estimate {result.mean():.7f}, variance ratio {original.var()/result.var():.1f}x")
