import sys
sys.path.pop(0)
from pathlib import Path
import runpy,json,hashlib,re
run=Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-163108-125')
path=run/'game-err.txt'
raw=path.read_bytes();log=raw.decode(errors='replace')
status=(run/'launch-status.txt').read_text()
assert 'game_exit=0' in status and log.rstrip().endswith('Exit.')
r=runpy.run_path('outputs/summarize-arceus-research.py')['summarize'](path)
r['capture_status']='completed; user manually closed; exit0'
r['log_sha256']=hashlib.sha256(raw).hexdigest()
r['controller_lines']=[line for line in log.splitlines() if line.startswith('input:') and any(term in line.lower() for term in ['gamepad','accelerometer','gyroscope'])]
r['settings']=json.loads((run/'test-settings.json').read_text())
r['user_feedback']='Camera speed and menu scrolling all felt good and better than previously; no visual problems, below1440p, no previous distorted appearance.'
r['postsolve_probe_present']=any(w['postsolve_observations'] for w in r['physics_worlds'])
Path('outputs/arceus-controller-test-pass6.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({k:r[k] for k in ['capture_status','guard_failures','controller_lines','camera','physics_step','physics_navigation','ui_animation_clock']},indent=2))
print('World groups:',len(r['physics_worlds']))
for w in r['physics_worlds']:
 print(w['world'],w['caller'],w['step_samples'],w['step_seconds_range'],w['normal_substeps'],w['normal_paths'])
