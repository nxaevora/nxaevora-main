from pathlib import Path
import shutil,hashlib,re,subprocess,json
root=Path(__file__).resolve().parent.parent;work=root/'work/rim-candidate'
runtime=root/'outputs/rim-runtime';runtime.mkdir(exist_ok=True)
exe=runtime/'ArceusTest.exe';shutil.copyfile(work/'ArceusRimCandidate-runtime.exe',exe)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=(root/'work/reflection/controller-reflection.c').read_text()
s=s.replace('\\\\reflection-runtime\\\\','\\\\rim-runtime\\\\')
array=','.join('0x'+sha(exe)[i:i+2] for i in range(0,64,2))
s,count=re.subn(r'(static int diagnostic_ok\(void\)\{static const unsigned char h\[32\]=\{)[^}]+',lambda m:m[1]+array,s);assert count==1
s=s.replace('Arceus reflection capture','Arceus neutral-rim candidate')
s=s.replace('env(L"SNX_REFLECTION_CAPTURE",L"1");','env(L"SNX_REFLECTION_CAPTURE",L"1");env(L"SNX_ARCEUS_NEUTRAL_RIM",L"1");')
s=s.replace('Reflection capture %ls;','Neutral-rim workaround candidate %ls;')
s=s.replace('\\"reflection_capture\\":true,','\\"reflection_capture\\":true,\\"neutral_rim_workaround\\":true,')
s=s.replace('current-controller-test.txt','current-rim-test.txt')
assert 'CopyFileW' not in s and 'TerminateProcess' not in s
source=work/'controller-rim.c';source.write_text(s)
launcher=root/'outputs/Arceus-Pink-Box-Candidate.exe'
args=['C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe',str(source),'-O2','-Wall','-Wextra','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',str(launcher)]
p=subprocess.run(args,capture_output=True,text=True);(work/'launcher-compile.txt').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(launcher),'--verify-only']);assert p.returncode==0
(work/'package.json').write_text(json.dumps({'launcher':str(launcher),'launcher_sha256':sha(launcher),'candidate_sha256':sha(exe),'verify_only':p.returncode,'game_launched':False,'installed_runtime_replaced':False},indent=2))
print('Packaged',launcher,'runtime',sha(exe),'verification passed; no game launched')
