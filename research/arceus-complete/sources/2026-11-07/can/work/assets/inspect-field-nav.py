from models import Asset,entries,game
import runpy
from pathlib import Path
v=runpy.run_path(str(Path(__file__).with_name("resident-placement.py")))["vec3"]
for i,_,b in entries(game/"archive/field/resident_release.gfpak"):
    if i==87:
        a=Asset(b)
        for o in a.tables(a.root(),0):
            name=a.string(o,0)
            if "AREA00" in name and ("AREA01" in name or "GATE" in name or "_FT" in name or "_TO_AREA00_S01" in name):
                print(name,a.string(o,2),v(a,o,8,0).round(2))
    if i<1086 and any(w in b.lower() for w in [b'area00_gate',b'area00_exit',b'area00_guard',b'area00_gatem',b'area00_gard']):
        import re
        print(i,list(dict.fromkeys(m.decode(errors="replace") for m in re.findall(b"[a-zA-Z0-9_./]+",b) if any(x in m.lower() for x in [b'area00_gate',b'area00_exit',b'area00_guard',b'area00_gard'])))[:10])
