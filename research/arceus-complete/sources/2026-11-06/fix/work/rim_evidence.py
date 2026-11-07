from pathlib import Path
import json,struct,re
from probe_lighting import load,cap,prefix
import numpy as np
shader=Path('C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett/work/pass10-render/89619027ba43e5d3.txt')
lines=shader.read_text().splitlines();words={int(s.split()[0]):int(s.split()[2],16) for s in lines}
w=words[328];assert (w>>20)&0x7ffff==0x302
# BFE immediate encodes offset2,width3. The input R22 comes from basecolor alpha.
base=load('552a80000');scene=load('5508c0000');region=scene[735:820,700:1800];mask=(region[...,0]>1.6*region[...,1])&(region[...,2]>1.6*region[...,1])&(region[...,1]>.03)
category=(base[735:820,700:1800,3]>>2)&7
colors=[]
for p in sorted(cap.glob('*1907*_light_0_cb3.bin')):
 b=p.read_bytes();r={'capture':p.name}
 for offset in [0xd0,0x150,0x1d0]:
  v=struct.unpack_from('<4f',b,offset);r[hex(offset)]=v
  if offset==0x150:r['purple_srgb8']=[round(255*(12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055)) for x in v[:3]]
 colors.append(r)
ldc={}
for n in [627,659,660,661,663,664]:
 w=words[n];ldc[str(n)]={'bank':(w>>36)&31,'offset':hex((w>>20)&65535),'address_register':(w>>8)&255,'destination':w&255,'size_code':(w>>48)&7}
result={'purple_pixel_count':int(mask.sum()),'material_categories_among_purple':{str(i):int(((category==i)&mask).sum()) for i in range(8)},'colors':colors,'ldc_instructions':ldc,'scope':'Static dataflow and captured constants; not a complete pixel shader replay or matched console reference.'}
Path('work/rim-evidence.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
