import sys
sys.path.pop(0)
from pathlib import Path
import hashlib
root=Path(r'C:/Users/brett/Documents/Codex/work')
cwd=Path.cwd()
checks={
 'regular':(cwd/'work/ArceusClockExperiment-runtime.exe','69B7C027FAFC35C2AF8270CDCBF01B666E8931CF0C81E39F170193A19F64DDAF'),
 'prior_candidate':(cwd/'work/ArceusEngineCandidate-before-pass2-runtime.exe','EA2566FBC33DEC658F62A3E242D225D5760D99EA446B7344D3F87B1957A6B0DB'),
 'candidate':(cwd/'work/ArceusEngineCandidate-runtime.exe','837FE18FA97C5B252534B7E8DFE175A49602D6CCDE39291178F2EBCD3A8325E1'),
 'build':(root/'nvn-build/scarletnx.exe','837FE18FA97C5B252534B7E8DFE175A49602D6CCDE39291178F2EBCD3A8325E1')}
for label,(path,expected) in checks.items():
 digest=hashlib.file_digest(path.open('rb'),'sha256').hexdigest().upper()
 assert digest==expected,(label,digest)
 print(label,'verified')
assert 'checks=1954 failures=0' in (cwd/'outputs/arceus-engine-instruction-checks.txt').read_text(encoding='utf-16' if (cwd/'outputs/arceus-engine-instruction-checks.txt').read_bytes().startswith(b'\xff\xfe') else 'utf-8-sig')
print('Deliverable test result verified')
