"""Independent quadrature and visible-normal sampling of an emissive rectangle."""
import numpy as np
rng=np.random.default_rng(849626)
count=600000
# Normal incidence makes the projected visible GGX distribution invertible in closed form.
# Include several glossy selection probabilities and a metallic-only case.
lo=np.array([-.28,-.23]);hi=np.array([.57,.38]);height=1.75
area=np.prod(hi-lo);power=3.
def density(cosine,rough):
    a2=rough**4
    nh2=(1.+cosine)/2.
    return a2/(4*np.pi*(1.-nh2+nh2*a2)**2)
def response(cosine,rough,f0):
    g=2*cosine/(cosine+np.sqrt(rough**4+(1.-rough**4)*cosine**2))
    f=f0+(1-f0)*(1.-np.sqrt((1.+cosine)/2.))**5
    return f*g
def omega(xy):
    d2=(xy**2).sum(axis=-1)+height**2
    cosine=height/np.sqrt(d2)
    return cosine,d2/(cosine*area)
axes=(np.arange(1024)+.5)/1024
grid=lo+np.stack(np.meshgrid(axes,axes),-1)*(hi-lo)
node,solid=omega(grid)
for rough,probability,f0 in ((.16,1.,.92),(.38,.25,.04),(.8,.6,.48)):
    reference=(power*density(node,rough)*response(node,rough,f0)/solid).mean()
    xy=lo+rng.random((count,2))*(hi-lo)
    cos,pLight=omega(xy)
    pdf=density(cos,rough)
    explicit=power*pdf*response(cos,rough,f0)/(pLight+probability*pdf)
    u=rng.random(count)
    thetaH=np.arctan(rough**2*np.sqrt(u/(1.-u)))
    angle=2*thetaH;cos=np.cos(angle);phi=2*np.pi*rng.random(count)
    xy=height*np.tan(angle)[:,None]*np.column_stack((np.cos(phi),np.sin(phi)))
    keep=(cos>0)&np.all((xy>=lo)&(xy<hi),axis=1)&(rng.random(count)<probability)
    implicit=np.zeros(count)
    p=density(cos[keep],rough)
    implicit[keep]=power*response(cos[keep],rough,f0)*p/(omega(xy[keep])[1]+probability*p)
    estimate=implicit+explicit
    error=abs(estimate.mean()-reference);se=estimate.std()/np.sqrt(count)
    print(f'roughness {rough:.2f}, lobe {probability:.2f}, integral {reference:.6f}, estimate {estimate.mean():.6f}, SE {se:.6f}')
    assert error < 5*se
