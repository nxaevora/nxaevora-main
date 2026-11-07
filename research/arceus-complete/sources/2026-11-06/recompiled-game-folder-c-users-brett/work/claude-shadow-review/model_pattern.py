import numpy as np
exec(open('model_receiver.py').read().split("rng = np.random")[0])
rng=np.random.default_rng(3)
pts=rng.uniform(40,N-40,size=(2,60000))
angles=np.radians([0,15,30,45,60,75,90,120,150,180,225,300])
print('Pattern on flat receivers: std / (max-min) of visibility across the surface, worst over 12 slope directions')
print('(native and linexact are exactly planar -> uniform; any non-zero is a texel-locked pattern)')
for r in [0.25,0.5,1.0,2.0]:
  for extra in [0.1,0.3,0.6,1.0]:
    beta=2.13*r+extra   # corner tap (1.5r,1.5r) needs 2.12r for a diagonal slope
    res={}
    for method in ['native','nearest','linexact']:
      worst=(0,0)
      for a in angles:
        g=1e-4; gx,gy=g*np.cos(a),g*np.sin(a)
        plane=lambda u,v,gx=gx,gy=gy:0.5+gx*u+gy*v
        vis=visibility(make_array(method,plane),plane,pts[0],pts[1],beta,g,radius=r)
        worst=max(worst,(vis.std(),vis.max()-vis.min()))
      res[method]=worst
    print(f'r={r:4.2f} beta={beta:4.2f} | '+' | '.join(f'{m}: std {s:.3f} range {d:.3f}' for m,(s,d) in res.items()))
