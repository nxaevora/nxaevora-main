import runpy
d=runpy.run_path("work/assets/resident-placement.py")
rows=d["tables"]()["ha_area00"]
for n,instances in rows.items():
    if "gate" in n or "guild" in n or "bridge" in n:
        print(n,[(list(v[0].round(1)),v[1][1].round(1),v[3]) for v in instances][:5])
