from pathlib import Path
import json,collections
report=Path('outputs/arceus-timing-investigation.txt').read_text().upper()
original=json.loads(Path('work/timing-index.json').read_text())['hits']
expanded=json.loads(Path('work/expanded-time-hits.json').read_text())
assert len(original)==40 and len(set(original))==40
for p in original:assert f'MAIN+{p:X}' in report,hex(p)
counts=collections.Counter(k for p,k,m,q in expanded)
assert counts['float 1/30']==47 and counts['float -1/30']==11
for p,k,m,q in expanded:
 assert f'{p:X}' in report,(hex(p),k)
assert '1AAF12C' in report
print('Verified all 40 original sites and all 68 expanded rate/default hits are represented.')
print(dict(counts))
