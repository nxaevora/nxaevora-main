import sys
sys.path.pop(0)
from pathlib import Path
import subprocess,hashlib,json
here=Path(__file__).resolve().parent
out=here.parent.parent/'outputs'
legacy=Path('C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett/work/native-shadow-candidate/controller-shadow-launcher.c')
source=legacy.read_text()
source=source.replace('#include <tlhelp32.h>','#include <tlhelp32.h>\n#include <bcrypt.h>\n#ifndef SHADOWMODE\n#error SHADOWMODE must be 0 or 1\n#endif\n#if SHADOWMODE\n#define SHADOWVALUE L"1"\n#define SHADOWJSON "true"\n#define MODELABEL L"shadow-ON"\n#else\n#define SHADOWVALUE L"0"\n#define SHADOWJSON "false"\n#define MODELABEL L"shadow-OFF"\n#endif')
source=source.replace('#define ROOT L"C:\\\\Users\\\\brett\\\\Documents\\\\Codex\\\\2026-11-06\\\\recompiled-game-folder-c-users-brett"', '#define ROOT L"C:\\\\Users\\\\brett\\\\Documents\\\\Codex\\\\2026-11-06\\\\i-m-working-on-a-long"')
old=' if(!CopyFileW(ROOT L"\\\\work\\\\native-shadow-candidate\\\\ArceusNativeShadow-runtime.exe",GAME L"\\\\ArceusTest.exe",FALSE))return fail(L"Cannot install prepared test runtime");'
assert old in source
source=source.replace(old,' /* Run the verified installed baseline; never copy or replace an executable. */')
source=source.replace('env(L"SNX_ARCEUS_NATIVE_SHADOWS",L"1")','env(L"SNX_ARCEUS_NATIVE_SHADOWS",SHADOWVALUE)')
source=source.replace('\\"native_shadows\\":true','\\"native_shadows\\":%s')
oldend='\\"seconds\\":0}\\n");fclose(f);}'
assert oldend in source
source=source.replace(oldend,'\\"seconds\\":0}\\n",SHADOWJSON);fclose(f);}')
source=source.replace('L"%ls\\n1440p native-shadow candidate;', 'L"%ls\\nPink-box controlled comparison %ls;')
source=source.replace('controller input enabled.\\n",run);','controller input enabled.\\n",run,MODELABEL);')
source=source.replace('native 1024 shadow producers;','shadow mode explicitly selected;')
source=source.replace('L"Arceus native shadow test"','L"Arceus pink-box comparison"')
source=source.replace('int WINAPI wWinMain(',r'''static int baseline_ok(void){
 static const unsigned char expected[32]={0xb3,0x2d,0x40,0xb0,0x3c,0xee,0x8a,0xda,0xf3,0x4d,0xa9,0x15,0x0a,0x65,0x7e,0x97,0xf2,0x5f,0x99,0x50,0xe4,0xca,0x25,0xfb,0xad,0x7f,0x64,0x1c,0xf8,0xca,0xf0,0x19};
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;HANDLE file=INVALID_HANDLE_VALUE;unsigned char *object=NULL,digest[32],buffer[65536];DWORD size=0,got=0;int ok=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof size,&got,0)<0)goto done;
 object=HeapAlloc(GetProcessHeap(),0,size);if(!object)goto done;
 if(BCryptCreateHash(alg,&hash,object,size,NULL,0,0)<0)goto done;
 file=CreateFileW(GAME L"\\ArceusTest.exe",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)goto done;
 for(;;){if(!ReadFile(file,buffer,sizeof buffer,&got,NULL))goto done;if(!got)break;if(BCryptHashData(hash,buffer,got,0)<0)goto done;}
 if(BCryptFinishHash(hash,digest,sizeof digest,0)<0)goto done;
 ok=!memcmp(digest,expected,sizeof digest);
 done:if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(hash)BCryptDestroyHash(hash);if(object)HeapFree(GetProcessHeap(),0,object);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
int WINAPI wWinMain(''')
needle=' (void)a;(void)b;(void)command;(void)show;'
assert needle in source
source=source.replace(needle,r''' (void)a;(void)b;(void)show;
 int verified=baseline_ok();
 if(!wcscmp(command,L"--verify-only"))return verified?0:2;
 if(!verified){MessageBoxW(NULL,L"Installed runtime is not the verified B32D40B0 baseline. Nothing was replaced or launched.",L"Arceus pink-box comparison",MB_OK|MB_ICONERROR);return 2;}
''')
assert 'CopyFileW' not in source and 'TerminateProcess' not in source
assert 'SNX_ARCEUS_NATIVE_SHADOWS",SHADOWVALUE' in source
src=here/'pink-box-comparison.c';src.write_text(source)
compiler=Path('C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe')
results=[]
for mode in [0,1]:
 name='OFF' if mode==0 else 'ON';exe=out/f'Arceus-Pink-Box-Shadow-{name}.exe'
 cmd=[str(compiler),str(src),'-O2','-Wall','-Wextra','-municode','-mwindows',f'-DSHADOWMODE={mode}','-lbcrypt','-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True,text=True);(here/f'build-{name}.txt').write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 verify=subprocess.run([str(exe),'--verify-only'])
 if verify.returncode:raise RuntimeError('Baseline verification failed; no game launched')
 results.append({'mode':name,'compiler':str(compiler),'command':cmd,'launcher':str(exe),'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'verify_only_exit':verify.returncode,'game_launched':False})
(here/'comparison-build.json').write_text(json.dumps({'source':str(src),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'derived_from':str(legacy),'derived_source_sha256':hashlib.sha256(legacy.read_bytes()).hexdigest(),'launchers':results},indent=2))
print(json.dumps(results,indent=2))
