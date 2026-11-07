import sys
sys.path.pop(0)
from pathlib import Path
import json
meta=json.loads(Path('work/pass9-render/const05/metadata.json').read_text());checks=0
for level in meta['mips']:
 nx=(level['width']+3)//4;ny=(level['height']+3)//4;b=level['block_height_log2'];H=1<<b;gobs=(nx*16+63)//64
 for y in range(ny):
  for x in range(nx*16):
   # NVIDIA TRM legacy TEGRA_RAW equation, in byte-oriented elements.
   gob=(y//(8*H))*512*H*gobs+(x//64)*512*H+((y%(8*H))//8)*512
   trm=gob+((x%64)//32)*256+((y%8)//2)*64+((x%32)//16)*32+(y%2)*16+(x%16)
   local=(y>>(3+b))*gobs*(512<<b)+(x>>6)*(512<<b)+((y>>3)&(H-1))*512+((x&32)<<3)+((x&16)<<1)+(x&15)+(((y&7)>>1)<<6)+((y&1)<<4)
   assert trm==local
   checks+=1
result={'byte_addresses':checks,'mips':len(meta['mips']),'mismatches':0,'primary_source':'https://cdck-file-uploads-global.s3.dualstack.us-west-2.amazonaws.com/nvidia/original/4X/a/0/8/a0808d84bcb2524b5ee54ddea208b294887cd42e.pdf','section':'NVIDIA Thor TRM 5.8.2.3.2, legacy TEGRA_RAW equation, printed page245','caveat':'Tegra X1 direct TRM download redirected to login; this comparison uses the expressly named legacy TEGRA_RAW equation in later NVIDIA documentation, not an asserted reading of X1 TRM.'}
Path('work/pass10-texture/trm-layout-check.json').write_text(json.dumps(result,indent=2));print(result)
