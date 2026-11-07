#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#ifndef SHADOWMODE
#error SHADOWMODE must be 0 or 1
#endif
#if SHADOWMODE
#define SHADOWVALUE L"1"
#define SHADOWJSON "true"
#define MODELABEL L"shadow-ON"
#else
#define SHADOWVALUE L"0"
#define SHADOWJSON "false"
#define MODELABEL L"shadow-OFF"
#endif
#include <stdio.h>
#include <wchar.h>
#define ROOT L"C:\\Users\\brett\\Documents\\Codex\\2026-11-06\\fix"
#define WORK L"C:\\Users\\brett\\Documents\\Codex\\work"
#define DIAGNOSTIC L"C:\\Users\\brett\\Documents\\Codex\\2026-11-07\\can\\outputs\\world-pt-v3-runtime\\ArceusTest.exe"
#define GAME WORK L"\\arceus-games\\ArceusTest"
static int fail(const wchar_t* what){wchar_t msg[512];swprintf(msg,512,L"%ls\nWindows error: %lu\nNo running game will be closed.",what,GetLastError());MessageBoxW(NULL,msg,L"Arceus World PT v3 DLSS RR 4K",MB_OK|MB_ICONERROR);return 1;}
static HANDLE output(const wchar_t* dir,const wchar_t* name){wchar_t path[1024];swprintf(path,1024,L"%ls\\%ls",dir,name);SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};return CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);}
static void env(const wchar_t* k,const wchar_t* v){SetEnvironmentVariableW(k,v);}
static int file_hash_ok(const wchar_t* path, const unsigned char expected[32]){
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;HANDLE file=INVALID_HANDLE_VALUE;unsigned char *object=NULL,digest[32],buffer[65536];DWORD size=0,got=0;int ok=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof size,&got,0)<0)goto done;
 object=HeapAlloc(GetProcessHeap(),0,size);if(!object)goto done;
 if(BCryptCreateHash(alg,&hash,object,size,NULL,0,0)<0)goto done;
 file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)goto done;
 for(;;){if(!ReadFile(file,buffer,sizeof buffer,&got,NULL))goto done;if(!got)break;if(BCryptHashData(hash,buffer,got,0)<0)goto done;}
 if(BCryptFinishHash(hash,digest,sizeof digest,0)<0)goto done;
 ok=!memcmp(digest,expected,sizeof digest);
 done:if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(hash)BCryptDestroyHash(hash);if(object)HeapFree(GetProcessHeap(),0,object);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static int baseline_ok(void){static const unsigned char h[32]={0xb3,0x2d,0x40,0xb0,0x3c,0xee,0x8a,0xda,0xf3,0x4d,0xa9,0x15,0x0a,0x65,0x7e,0x97,0xf2,0x5f,0x99,0x50,0xe4,0xca,0x25,0xfb,0xad,0x7f,0x64,0x1c,0xf8,0xca,0xf0,0x19};return file_hash_ok(GAME L"\\ArceusTest.exe",h);}
static int diagnostic_ok(void){static const unsigned char h[32]={0x98,0x74,0x88,0xb1,0x1e,0x98,0x44,0x3f,0xac,0xd9,0xf1,0x0,0x7e,0x28,0xc0,0x79,0x5c,0x4f,0x2e,0xb7,0x8,0x6e,0x38,0x78,0x2f,0x7a,0xb8,0x7e,0xd,0xa0,0x71,0xb8};return file_hash_ok(DIAGNOSTIC,h);}
int WINAPI wWinMain(HINSTANCE a,HINSTANCE b,LPWSTR command,int show){
 (void)a;(void)b;(void)show;
 int verified=baseline_ok() && diagnostic_ok();
 if(!wcscmp(command,L"--verify-only"))return verified?0:2;
 if(!verified){MessageBoxW(NULL,L"Baseline or diagnostic runtime hash mismatch. Nothing was replaced or launched.",L"Arceus World PT v3 DLSS RR 4K",MB_OK|MB_ICONERROR);return 2;}

 HANDLE lock=CreateMutexW(NULL,FALSE,L"Local\\ArceusControllerResearchLaunch");if(!lock)return fail(L"Cannot create launcher lock");if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(NULL,L"A controller test launcher is already active. Close the game yourself before another test.",L"Arceus World PT v3 DLSS RR 4K",MB_OK);CloseHandle(lock);return 0;}
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return fail(L"Cannot check for an existing game");PROCESSENTRY32W e={.dwSize=sizeof e};BOOL more=Process32FirstW(snap,&e);while(more){if(!_wcsicmp(e.szExeFile,L"ArceusTest.exe")){CloseHandle(snap);MessageBoxW(NULL,L"ArceusTest is already running. Save and close it yourself before starting another test.",L"Arceus World PT v3 DLSS RR 4K",MB_OK);CloseHandle(lock);return 0;}more=Process32NextW(snap,&e);}CloseHandle(snap);
 SYSTEMTIME t;GetLocalTime(&t);wchar_t run[1024],path[1024];swprintf(run,1024,WORK L"\\arceus-runs\\timing-%04u%02u%02u-%02u%02u%02u-%03u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);if(!CreateDirectoryW(run,NULL))return fail(L"Cannot create local capture directory");
 /* Run the verified installed baseline; never copy or replace an executable. */
 wchar_t prior[32768],search[32768];DWORD n=GetEnvironmentVariableW(L"PATH",prior,30000);if(!n||n>=30000)prior[0]=0;swprintf(search,32768,WORK L"\\msys64\\clang64\\bin;" WORK L"\\llvm-mingw-20261104-ucrt-x86_64\\bin;%ls",prior);env(L"PATH",search);env(L"APPDATA",WORK L"\\arceus-appdata");
 env(L"ARCEUS_TERRAIN_DIR",L"C:\\Users\\brett\\Documents\\Codex\\2026-11-07\\can\\outputs\\world-pt-v3-runtime\\terrain");env(L"ARCEUS_DLSS_RR",L"1");env(L"ARCEUS_NGX",L"1");env(L"ARCEUS_UPSCALER",L"DLSS");env(L"SNX_ARCEUS_FPS60",L"0");env(L"SNX_ARCEUS_TARGET_FPS",L"120");env(L"SNX_ARCEUS_CLOCK_TRACE",L"1");env(L"SNX_ARCEUS_TIMING_FIXES",L"1");env(L"SNX_ARCEUS_EVENT_CAMERA_FIX",L"1");env(L"SNX_ARCEUS_BLINK_FIX",L"1");env(L"SNX_ARCEUS_CAMERA_INPUT_FIX",L"1");env(L"SNX_ARCEUS_CAMERA_SPEED",L"2.2");env(L"SNX_ARCEUS_MENU_REPEAT_FIX",L"1");env(L"SNX_ARCEUS_DIALOGUE_TRACE",L"1");env(L"SNX_ARCEUS_TIMING_RESEARCH",L"1");env(L"SNX_ARCEUS_CAMERA_TRACE",L"0");env(L"SNX_ARCEUS_FIXED_RESOLUTION",L"1");env(L"SNX_ARCEUS_SCREEN_FETCH",L"1");env(L"SNX_RESOLUTION_TRACE",L"1");env(L"SNX_MATERIAL_TRACE",L"1");env(L"SNX_MATERIAL_SNAPSHOT",L"1");env(L"SNX_REFLECTION_CAPTURE",L"1");env(L"SNX_ARCEUS_NEUTRAL_RIM",L"1");env(L"SNX_ARCEUS_NATIVE_SHADOWS",SHADOWVALUE);env(L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS",L"1");
 swprintf(path,1024,L"%ls\\submissions.csv",run);env(L"SNX_PRESENT_TRACE",path);
 FILE* f=_wfopen(ROOT L"\\outputs\\current-world-pt-v3-rr.txt",L"wt, ccs=UTF-8");if(f){fwprintf(f,L"%ls\nWorld PT v3 DLSS RR 4K %ls; accepted native shadows; F3 saves temporal motion, adjacent scene features, lighting bindings and attachments; 120 FPS target; Vulkan; camera2.2; menu-repeat/blink/event-camera/timing fixes; manual close; controller input enabled.\n",run,MODELABEL);fclose(f);}
 swprintf(path,1024,L"%ls\\test-settings.json",run);f=_wfopen(path,L"wt");if(f){fprintf(f,"{\"target_fps\":120,\"camera_speed\":2.2,\"menu_repeat\":true,\"resolution_scale_override\":2.0,\"docked\":true,\"resolution_trace\":true,\"material_trace\":true,\"material_snapshot\":true,\"temporal_backend\":\"DLSS\",\"quality_mode\":\"Performance\",\"ray_reconstruction_requested\":true,\"sr_fallback_model\":\"M\",\"scene_width\":1920,\"scene_height\":1080,\"output_width\":3840,\"output_height\":2160,\"temporal_capture\":true,\"reflection_capture\":true,\"neutral_rim_workaround\":true,\"isolated_runtime\":true,\"native_shadows\":%s,\"screen_fetch_2x\":true,\"native_bc\":true,\"fixed_scene_resolution\":true,\"screen_fetch_fix\":true,\"engine_candidate\":true,\"fullscreen\":true,\"controller_background\":true,\"desktop_launcher\":true,\"seconds\":0}\n",SHADOWJSON);fclose(f);}
 SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};HANDLE in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);HANDLE out=output(run,L"game-out.txt"),err=output(run,L"game-err.txt");if(in==INVALID_HANDLE_VALUE||out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE)return fail(L"Cannot open capture streams");
 STARTUPINFOW si={.cb=sizeof si,.dwFlags=STARTF_USESTDHANDLES,.hStdInput=in,.hStdOutput=out,.hStdError=err};PROCESS_INFORMATION game={0};wchar_t args[]=L"\"" DIAGNOSTIC L"\" --game=\"" GAME L"\" --nvn_native=0 --native_bc=1 --fps_limit=game --fullscreen=1 --log=1 --seconds=0 --gamepad_background=1 --docked=1 --resolution=2.0";
 if(!CreateProcessW(DIAGNOSTIC,args,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,GAME,&si,&game))return fail(L"Cannot start the controller test");CloseHandle(game.hThread);CloseHandle(in);CloseHandle(out);CloseHandle(err);
 swprintf(path,1024,L"%ls\\launch-status.txt",run);f=_wfopen(path,L"wt");if(f){fprintf(f,"game_pid=%lu\nlauncher_pid=%lu\nmanual_close=1\n",game.dwProcessId,GetCurrentProcessId());fclose(f);}
 WaitForSingleObject(game.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(game.hProcess,&code);CloseHandle(game.hProcess);f=_wfopen(path,L"at");if(f){fprintf(f,"game_exit=%lu\n",code);fclose(f);}CloseHandle(lock);return 0;
}
