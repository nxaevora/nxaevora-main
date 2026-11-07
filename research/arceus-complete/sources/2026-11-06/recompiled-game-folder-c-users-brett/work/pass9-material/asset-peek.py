import sys
sys.path.pop(0)
from pathlib import Path
import struct,re
base=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/archive/field/model/pack')
for n in ['ha_area00.gfpak','ha_area00_s01.gfpak']:
 b=(base/n).read_bytes();print(n,len(b),b[:256].hex(' '));print(re.findall(rb'[ -~]{8,}',b[:65536])[:50])
base=Path(r'C:/Users/brett/Documents/Codex/work/arceus-dump/romfs/bin/field/model/pack')
for n in ['objects_area00_const05.trmtr','objects_area00_house03.trmtr']:
 b=(base/n).read_bytes();print(n,b[:64].hex(' '));print([x.decode('ascii') for x in re.findall(rb'[ -~]{4,}',b)])
