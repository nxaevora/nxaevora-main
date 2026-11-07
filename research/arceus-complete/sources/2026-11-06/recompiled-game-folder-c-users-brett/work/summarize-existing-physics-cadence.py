import sys
sys.path.pop(0)
from pathlib import Path
import re,json,statistics
root=Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs')
out=[]
for name in ('timing-20261106-125820-850','timing-20261106-132828-807'):
    samples=[]
    with (root/name/'game-err.txt').open(errors='replace') as f:
        for line in f:
            if not line.startswith('arceus-timing-research: kind=1 '):continue
            r=dict(re.findall(r'([a-z_][a-z_0-9]*)=([^\s]+)',line))
            if int(r['count'])>=300:samples.append(r)
    rates=[]
    for a,b in zip(samples,samples[1:]):
        if a['object']!=b['object']:continue
        dc=int(b['count'])-int(a['count']); dn=int(b['ns'])-int(a['ns'])
        if dc>0 and dn>0:rates.append(dc*1e9/dn)
    out.append({'run':name,'sampled_rows':len(samples),'intervals':len(rates),
        'wrapper_calls_per_second_min_median_max':[min(rates),statistics.median(rates),max(rates)] if rates else None,
        'variable_modes':sorted({int(r['variable_mode']) for r in samples}),
        'multipliers':sorted({float(r['rate']) for r in samples}),
        'first_count':int(samples[0]['count']) if samples else None,
        'last_count':int(samples[-1]['count']) if samples else None,
        'scope':'Existing wrapper entry observations, not solver executions or displayed FPS. Running log is only a snapshot.'})
Path('work/existing-physics-cadence.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
