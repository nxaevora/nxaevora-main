from texture import *
import json
def fingerprint(b):
    h=14695981039346656037
    for v in b:h=((h^v)*1099511628211)&0xffffffffffffffff
    return f'{h:016x}'
catalog={}
for sub,area in [('model/pack','ha_area00_s01'),('model/pack','ha_area00'),('terrain','ha_area00')]:
    for i,t,b in entries(game/'archive/field'/sub/(area+'.gfpak')):
        if b[:4]!=b'BNTX':continue
        name,fmt,w,h,mips=texture(b)
        signature=fingerprint(b[mips[0]:mips[0]+2048])
        catalog.setdefault(signature,[]).append([name,w,h,fmt])
(here/'texture-signatures.json').write_text(json.dumps(catalog))
print(len(catalog),'unique samples')
