import sys
sys.path.pop(0)
from pathlib import Path
import hashlib,json,difflib
root=Path(r'C:/Users/brett/Documents/Codex/work')
cwd=Path.cwd()
expected={
cwd/'work/ArceusClockExperiment-runtime.exe':'69B7C027FAFC35C2AF8270CDCBF01B666E8931CF0C81E39F170193A19F64DDAF',
cwd/'work/ArceusEngineCandidate-before-pass4-runtime.exe':'E8CC679125F8BDEA8EC822DFD5112135F3DDAD98DAAB4CB22E80C30D12730480',
cwd/'work/ArceusEngineCandidate-runtime.exe':'C2B2BE0B146D3C801A9E1E801243CA60D17965BDD363AF8DA5C746464B6753C1',
root/'nvn-build/scarletnx.exe':'C2B2BE0B146D3C801A9E1E801243CA60D17965BDD363AF8DA5C746464B6753C1'}
for p,want in expected.items():
 with p.open('rb') as f:got=hashlib.file_digest(f,'sha256').hexdigest().upper()
 assert got==want,(p,got)
 print(p.name+': hash verified')
source=(root/'nvn-src/src/nx/runtime/arceus_clock.c').read_bytes()
mirror=Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX/src/nx/runtime/arceus_clock.c').read_bytes()
assert source==mirror
old=Path('work/pass4/arceus_clock-before-pass4.c').read_text()
new=source.decode().replace('\r\n','\n')
prefix='static void physics_world_step_probe(CPU* c, u64 entry) {'
suffix='static void engine_clock_research_init(void) {'
assert old.split(prefix)[0]==new.split(prefix)[0]
assert old.split(suffix)[1]==new.split(suffix)[1]
assert new.split(prefix)[1].split(suffix)[0].count('engine_clock_original[3](c,entry);')==1
Path('work/pass4/runtime-observation.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='pass3',tofile='pass4')))
print('Source/mirror identical; change limited to existing physics observation wrapper')
D=json.loads(Path('work/timing-index.json').read_text());rel={int(k):v for k,v in D['rel'].items() if k!='null'}
assert rel[0x41b5018]==0x2fe2604 and rel[0x41b3ae0]==0x2f9158c
print('Both simulation vtable solve-method identities verified')
for fps in (30,120):
 launch=Path(f'outputs/Test-Arceus-Engine-Research-{fps}FPS.cmd').read_text()
 assert '-EngineCandidate' in launch and '-TimingResearch' in launch and '-Seconds' not in launch
launch=Path('outputs/Capture-Arceus-Timing.ps1').read_text()
assert '[int]$Seconds=0' in launch and "'--nvn_native=0'" in launch
print('Existing manual research launchers retain Vulkan and no time limit')
