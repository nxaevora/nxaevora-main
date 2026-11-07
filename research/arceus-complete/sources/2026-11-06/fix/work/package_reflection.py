from pathlib import Path
import hashlib,subprocess,json,shutil,re
root=Path(__file__).resolve().parent.parent
out=root/'outputs'; runtime=out/'reflection-runtime';runtime.mkdir(exist_ok=True)
candidate=runtime/'ArceusTest.exe'
shutil.copyfile(root/'work/reflection/ArceusReflection-runtime.exe',candidate)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=Path('C:/Users/brett/Documents/Codex/2026-11-06/i-m-working-on-a-long/work/pink-box/pink-box-comparison.c').read_text()
source=source.replace('2026-11-06\\\\i-m-working-on-a-long','2026-11-06\\\\fix')
source=source.replace('#define GAME WORK', '#define DIAGNOSTIC ROOT L"\\\\outputs\\\\reflection-runtime\\\\ArceusTest.exe"\n#define GAME WORK')
source=source.replace('static int baseline_ok(void){','static int file_hash_ok(const wchar_t* path, const unsigned char expected[32]){')
source=re.sub(r' static const unsigned char expected\[32\]=\{[^}]+\};\n','',source,count=1)
source=source.replace('CreateFileW(GAME L"\\\\ArceusTest.exe",GENERIC_READ','CreateFileW(path,GENERIC_READ',1)
def arr(h):return ','.join('0x'+h[i:i+2] for i in range(0,64,2))
basehash='b32d40b03cee8adaf34da9150a657e97f25f9950e4ca25fbad7f641cf8caf019'
checks='static int baseline_ok(void){static const unsigned char h[32]={'+arr(basehash)+'};return file_hash_ok(GAME L"\\\\ArceusTest.exe",h);}\n'
checks+='static int diagnostic_ok(void){static const unsigned char h[32]={'+arr(sha(candidate))+'};return file_hash_ok(DIAGNOSTIC,h);}\n'
source=source.replace('int WINAPI wWinMain(',checks+'int WINAPI wWinMain(',1)
source=source.replace('int verified=baseline_ok();','int verified=baseline_ok() && diagnostic_ok();')
source=source.replace('Installed runtime is not the verified B32D40B0 baseline. Nothing was replaced or launched.','Baseline or diagnostic runtime hash mismatch. Nothing was replaced or launched.')
source=source.replace('Arceus pink-box comparison','Arceus reflection capture')
source=source.replace('env(L"SNX_MATERIAL_SNAPSHOT",L"1");','env(L"SNX_MATERIAL_SNAPSHOT",L"1");env(L"SNX_REFLECTION_CAPTURE",L"1");')
source=source.replace('Pink-box controlled comparison %ls; shadow mode explicitly selected; F3 saves lighting buffers;', 'Reflection capture %ls; accepted native shadows; F3 saves cube faces/mips, lighting bindings and attachments;')
source=source.replace('\\"material_snapshot\\":true,','\\"material_snapshot\\":true,\\"reflection_capture\\":true,\\"isolated_runtime\\":true,')
old='wchar_t args[]=L"\\\"" GAME L"\\\\ArceusTest.exe\\\" --nvn_native=0'
new='wchar_t args[]=L"\\\"" DIAGNOSTIC L"\\\" --game=\\\"" GAME L"\\\" --nvn_native=0'
assert old in source
source=source.replace(old,new)
source=source.replace('CreateProcessW(GAME L"\\\\ArceusTest.exe",args','CreateProcessW(DIAGNOSTIC,args')
assert 'CopyFileW' not in source and 'TerminateProcess' not in source
p=root/'work/reflection/controller-reflection.c';p.write_text(source)
compiler='C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
exe=out/'Arceus-Reflection-Test.exe'
args=[compiler,str(p),'-O2','-Wall','-Wextra','-municode','-mwindows','-DSHADOWMODE=1','-lbcrypt','-o',str(exe)]
r=subprocess.run(args,capture_output=True,text=True);assert r.returncode==0,r.stderr
(root/'work/reflection/launcher-compile.txt').write_text(r.stdout+r.stderr)
r=subprocess.run([str(exe),'--verify-only']);assert r.returncode==0
proof={'launcher':str(exe),'launcher_sha256':sha(exe),'diagnostic_sha256':sha(candidate),'baseline_sha256':basehash,'verify_only_exit':r.returncode,'installed_executable_replaced':False,'game_launched':False,'command':args}
(root/'work/reflection/package.json').write_text(json.dumps(proof,indent=2));print(json.dumps(proof,indent=2))
