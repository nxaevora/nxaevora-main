"""Compare equal-area environment MIS against quadrature with disjoint blocked directions."""
import numpy as np
rng=np.random.default_rng(25);N=350000;pi=np.pi
def diruv(u,v):
    y=1-2*v;phi=2*pi*(u-.5);r=np.sqrt(np.maximum(0,1-y*y))
    return np.stack([r*np.cos(phi),y,r*np.sin(phi)],-1)
def field(p):
    return .06+5*np.exp(90*(p@np.array([.36,.8,.48])-1))+1.6*np.maximum(0,p[:,2])**4
def visibility(p):return (p[:,0]<.36)|(p[:,2]>.2)
def density(l,rough):
    nh2=(1+l[:,1])*.5;a2=rough**4
    return a2/(4*pi*(1-nh2+nh2*a2)**2)
def g(l,rough):
    c=np.clip(l[:,1],0,1);return 2*c/(c+np.sqrt(rough**4+(1-rough**4)*c*c))
def f(l):return .34+.66*(1-np.sqrt(np.clip((1+l[:,1])*.5,0,1)))**5
def gloss(rough,n):
    t=np.arctan(rough**2*np.sqrt((z:=rng.random(n))/(1-z)));phi=2*pi*rng.random(n)
    return np.stack([np.sin(2*t)*np.cos(phi),np.cos(2*t),np.sin(2*t)*np.sin(phi)],1)
X,Y=np.meshgrid(np.arange(64),np.arange(32));mass=np.zeros(2048)
for i in range(4):mass+=field(diruv((X+.25+.5*(i%2))/64,(Y+.25+.5*(i//2))/32).reshape(-1,3))
mass/=mass.sum()
def sky(n):
    cells=rng.choice(len(mass),size=n,p=mass);uniform=rng.random(n)<.04;cells[uniform]=rng.integers(2048,size=uniform.sum())
    return diruv((cells%64+rng.random(n))/64,(cells//64+rng.random(n))/32)
def dp(l):return np.maximum(l[:,1],0)/pi
def proposal(l,rough):
    x=(np.floor((np.arctan2(l[:,2],l[:,0])/(2*pi)+.5)*64).astype(int)%64)
    y=np.clip(((1-l[:,1])*16).astype(int),0,31)
    return .5*(.04+.96*2048*mass[64*y+x])/(4*pi)+.25*dp(l)+.25*density(l,rough)
def cosdir(n):return diruv(rng.random(n),.5*(1-np.sqrt(rng.random(n))))
q,p=np.meshgrid((np.arange(1024)+.5)/1024,(np.arange(512)+.5)/512)
nodes=diruv(q.ravel(),p.ravel())
for rough,pr in ((.3,.25),(.6,.64),(.9,.95)):
    li=field(nodes)*visibility(nodes)
    target=4*pi*np.mean(li*(.48*dp(nodes)+density(nodes,rough)*g(nodes,rough)*f(nodes)))
    full=sky(N);u=rng.random(N)
    full[u>=.5]=cosdir((u>=.5).sum());full[u>=.75]=gloss(rough,(u>=.75).sum())
    d=dp(full);s=density(full,rough);q=proposal(full,rough)
    explicit=field(full)*visibility(full)*(.48*d/(q+(1-pr)*d)+s*g(full,rough)*f(full)/(q+pr*s))
    chosen=rng.random(N)<pr;l=cosdir(N);l[chosen]=gloss(rough,chosen.sum())
    densityBs=np.where(chosen,pr*density(l,rough),(1-pr)*dp(l))
    weight=densityBs/(densityBs+proposal(l,rough))
    implicit=field(l)*visibility(l)*weight*np.where(chosen,f(l)*g(l,rough)/pr,.48/(1-pr))
    v=explicit+implicit;se=v.std()/np.sqrt(N)
    print(f'rough {rough}, integral {target:.6f}, estimate {v.mean():.6f}, SE {se:.6f}')
    assert abs(v.mean()-target)<5*se
