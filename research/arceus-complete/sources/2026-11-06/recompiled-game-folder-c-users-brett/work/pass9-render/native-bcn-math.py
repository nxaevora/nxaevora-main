import sys
sys.path.pop(0)
from pathlib import Path
import struct,json
p=Path('work/pass9-texture');out={}
for kind,nc in [(3,1),(5,2)]:
 blocks=(p/f'native-{kind}-blocks.bin').read_bytes();got=struct.unpack('<16384f',(p/f'native-{kind}-got.bin').read_bytes());want=(p/f'native-{kind}-want.bin').read_bytes();worst=0;cpu_diff=0;ex=[]
 for i in range(4096):
  x,y=i%64,i//64;bi=y//4*16+x//4;pix=y%4*4+x%4
  for c in range(nc):
   raw=blocks[(bi*nc+c)*8:(bi*nc+c+1)*8];v=int.from_bytes(raw,'little');a0=v&255;a1=v>>8&255;k=v>>(16+3*pix)&7
   if k==0:exact=a0;model=a0*257
   elif k==1:exact=a1;model=a1*257
   elif a0>a1:exact=((8-k)*a0+(k-1)*a1)/7;model=a0*257+(a1-a0)*[0,257,36,72,113,144,185,221][k]
   elif k<6:exact=((6-k)*a0+(k-1)*a1)/5;model=a0*257+(a1-a0)*[0,257,48,96,161,209][k]
   else:exact=0 if k==6 else 255;model=int(exact*257)
   expected=int(exact*257+0.5);w=struct.unpack_from('<H',want,(i*nc+c)*2)[0];cpu_diff+=expected!=w;err=abs(model/65535-got[i*4+c]);worst=max(worst,err)
   if err>1e-7 and len(ex)<5:ex.append(dict(texel=i,channel=c,a0=a0,a1=a1,index=k,got=got[i*4+c],model=model/65535,error=err))
 out[kind]=dict(channels=4096*nc,cpu_fixture_formula_mismatches=cpu_diff,affine_observation_fit_max_abs=worst,examples=ex)
Path('work/pass9-render/native-bcn-independent-math.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))

