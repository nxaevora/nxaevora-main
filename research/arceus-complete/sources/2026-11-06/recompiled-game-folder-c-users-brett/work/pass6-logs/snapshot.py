import sys
sys.path.pop(0)
from pathlib import Path
import runpy,json
root=Path('work/pass6-logs');run=Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-163108-125')
raw=run.joinpath('game-err.txt').read_bytes();raw=raw.rsplit(b'\n',1)[0]+b'\n'
path=root/'game-snapshot.log';path.write_bytes(raw)
r=runpy.run_path('outputs/summarize-arceus-research.py')['summarize'](path)
r['original_live_source']=str(run/'game-err.txt');r['snapshot_only']=True
root.joinpath('research-snapshot.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({k:r[k] for k in ['guard_failures','physics_worlds','physics_navigation','camera']},indent=2))
