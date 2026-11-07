import sys
sys.path.pop(0)
from pathlib import Path
import hashlib,json,struct
files=[Path('work/ArceusEngineCandidate-runtime.exe'),Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/ArceusTest.exe')]
r={str(p):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'mtime':p.stat().st_mtime} for p in files}
for f in ['ArceusTest.vkshaders','ArceusTest.vkshipped','ArceusTest.vkscan']:
 p=Path(r'C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/shader_cache')/f
 with p.open('rb') as h:
  head=h.read(16);n=struct.unpack_from('<I',head,12)[0];key=h.read(n).decode(errors='replace')
 r[f]={'size':p.stat().st_size,'key':key,'mtime':p.stat().st_mtime}
Path('work/pass9-render/runtime-cache.json').write_text(json.dumps(r,indent=2))
print(json.dumps(r,indent=2))
