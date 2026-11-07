from interior import mesh_data, pair, np, here
m,b=pair('ha_area00_s01',11,9)
capt=np.fromfile(here.parent/'world/positions.f32','<f4').reshape(-1,4)
def signatures(data):
    codes=np.zeros(len(data),np.uint32)
    for k in range(1,10):
        codes[:-k] |= (np.all(data[k:]==data[:-k],axis=1).astype(np.uint32) << k)
    return codes
sign=signatures(capt)
for (pos,idx),shape in zip(mesh_data('ha_area00_s01',11,9),m.tables(m.root(),1)):
 for t in m.tables(shape,4):
    count,first=m.number(t,0),m.number(t,1)
    P=pos[idx[first:first+count]]
    seq=signatures(P)
    cand=np.arange(0,len(sign)-count+1,3)
    for k in np.linspace(0,count-12,128,dtype=int):
        cand=cand[sign[cand+k]==seq[k]]
        if len(cand)<2:break
    best=None
    for st in cand[:100]:
        ix=np.linspace(0,count-1,3072,dtype=int)
        x=np.column_stack([P[ix],np.ones(len(ix))]); y=capt[st+ix]
        tr=np.linalg.lstsq(x[::3],y[::3],rcond=None)[0]
        err=np.linalg.norm(x@tr-y,axis=1)
        if np.std(y[:,3])>.03 and (best is None or np.median(err)<best[0]):best=(np.median(err),st,np.percentile(err,99),tr)
    print(m.string(t,3),'candidates',len(cand),'best',best[:3] if best else None)
    if best and best[2]<.01:
        print(best[3].round(4))
