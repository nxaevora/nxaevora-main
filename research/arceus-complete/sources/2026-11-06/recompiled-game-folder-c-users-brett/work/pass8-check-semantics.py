"""Check the final production translator's emitted coordinate expressions.
This evaluates neither complete shader control flow nor GPU image contents.
"""
import sys
sys.path.pop(0)
from pathlib import Path

source = Path('work/check-screen-fetch-semantics.py').read_text()
exec(source[:source.index('fixtures=Path(')])
fixtures = Path('work/pass8-integration/fixtures')
corpus = Path('work/pass8-final-corpus')
checks = 0
names = ['tld', 'integer-moves', 'float-moves', 'tlds-separated', 'tlds-lod',
         'tlds-array', 'tld-array', 'tlds-multisample', 'bindless-tld']
pairs = [(name, Shader(fixtures/f'{name}-0.spv'), Shader(fixtures/f'{name}-1.spv'), [(0,0)])
         for name in names]
for name, mapping in [('e9e7255e7594a006_4.spv',[(0,0),(1,1),(2,2)]),
                      ('e02c1a109a77fac4_4.spv',list(enumerate([0,1,4,5,10,11])))]:
    pairs.append((name, Shader(corpus/'shipped-off'/name), Shader(corpus/'shipped-on'/name), mapping))
for name, old, new, mapping in pairs:
    for fetch, slot in mapping:
        for scale in [.5,.75,1.,4/3,1.5,2.,3.]:
            for px in list(range(32))+[127,128,1919,2559]:
                for py in [0,1,2,47,719,1439]:
                    args = (px+.5,py+.5,scale)
                    got = new.coordinates(*args,1<<slot,fetch)
                    assert got[:2] == (px,py), (name,fetch,scale,px,py,got)
                    for mask in [0,0xffffffff^(1<<slot)]:
                        assert new.coordinates(*args,mask,fetch) == old.coordinates(*args,mask,fetch), (name,fetch,mask)
                    if scale == 1:
                        assert got == old.coordinates(*args,1<<slot,fetch), (name,fetch,got)
                    checks += 1
report = dict(coordinate_cases=checks, failures=0, synthetic_shaders=len(names),
              real_fetches=9, scales=[.5,.75,1.,4/3,1.5,2.,3.],
              limits='Restricted emitted-coordinate expression evaluation; not complete shader or GPU rendering validation.')
Path('work/pass8-integration/semantics.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
