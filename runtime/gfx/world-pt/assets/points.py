from pathlib import Path
import numpy as np
p=Path(__file__).resolve().parent
for f in sorted(p.glob('world-*.bin')):
 a=np.fromfile(f,'<f4').reshape(-1,4)
 valid=a[1:2041];valid=valid[(valid[:,3]>1.e-6)&np.isfinite(valid).all(axis=1)]
 if len(valid):print(f.name,'origin',a[0], 'bounds', np.round(np.percentile(valid[:,:3],[10,50,90],axis=0).T,1).tolist())
