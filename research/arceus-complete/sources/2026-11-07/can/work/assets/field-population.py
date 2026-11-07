import runpy,collections,numpy as np
r=runpy.run_path("work/assets/resident-placement.py")["tables"]()
for area,models in r.items():
    c=collections.Counter()
    for name,rows in models.items():
        ok=[x for x in rows if not any(x[3])]
        c[name]=len(ok)
    print(area,c.most_common(22))
