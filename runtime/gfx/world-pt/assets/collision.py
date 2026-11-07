from readpak import *
for area,idx in [('ha_area00',2),('ha_area00_s01',0)]:
 data=next(b for i,t,b in entries(game/'archive/field/terrain'/(area+'.gfpak')) if i==idx)
 print(area,len(data),[s for s in strings(data) if 'Shape' in s or 'Mesh' in s or s.startswith('hkp')][:28])
 print(data[-200:].hex()[:100])
