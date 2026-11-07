import sys
sys.path.pop(0)
from pathlib import Path
import hashlib,struct,json

root=Path(r'C:/Users/brett/Documents/Codex/work')
cwd=Path.cwd()
checks={
 'regular unchanged':(cwd/'work/ArceusClockExperiment-runtime.exe','69B7C027FAFC35C2AF8270CDCBF01B666E8931CF0C81E39F170193A19F64DDAF'),
 'previous candidate retained':(cwd/'work/ArceusEngineCandidate-before-pass3-runtime.exe','837FE18FA97C5B252534B7E8DFE175A49602D6CCDE39291178F2EBCD3A8325E1'),
 'new candidate':(cwd/'work/ArceusEngineCandidate-runtime.exe','E8CC679125F8BDEA8EC822DFD5112135F3DDAD98DAAB4CB22E80C30D12730480'),
 'build matches candidate':(root/'nvn-build/scarletnx.exe','E8CC679125F8BDEA8EC822DFD5112135F3DDAD98DAAB4CB22E80C30D12730480')}
for label,(path,expected) in checks.items():
 with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest().upper()
 assert digest==expected,(label,digest)
 print(label+': verified')
source=(root/'nvn-src/src/nx/runtime/arceus_clock.c').read_bytes()
mirror=Path(r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main/third_party/ScarletNX/src/nx/runtime/arceus_clock.c').read_bytes()
assert source==mirror
print('Runtime source and mirror: identical')
b=(root/'arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
word=lambda p:struct.unpack_from('<I',b,p)[0]
fns=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
assert fns[0x2b433bc]==0x304
assert word(0x2b43408)==0xaa0003f3 # owner in X19
assert word(0x2b43430)==0xb980c815 # signed mode in X21, [X0,+C8]
assert word(0x2b43448)==0xd63f0100 # call through X8
assert struct.unpack_from('<5Q',b,0x39848e0)==(16666667,33333334,50000000,66666667,83333334)
print('UI observer owner, mode, call-site and denominator provenance: verified')
result=Path('outputs/arceus-ui-capture-instruction-checks.txt').read_text()
assert 'checks=1779 failures=0 compiled_functions=7' in result
for fps in (30,120):
 launcher=Path(f'outputs/Test-Arceus-Engine-Research-{fps}FPS.cmd').read_text()
 assert '-EngineCandidate' in launcher and '-TimingResearch' in launcher and '-Seconds' not in launcher
capture=Path('outputs/Capture-Arceus-Timing.ps1').read_text()
assert '[int]$Seconds=0' in capture and "'--nvn_native=0'" in capture
assert 'Stop-Process -Id $game' not in capture
print('Manual test launchers: research candidate, Vulkan, no time limit')
print('Guest-instruction result artifact: verified')
