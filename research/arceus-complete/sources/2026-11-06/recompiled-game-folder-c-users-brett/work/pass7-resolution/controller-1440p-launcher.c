#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>
#define ROOT L"C:\\Users\\brett\\Documents\\Codex\\2026-11-06\\recompiled-game-folder-c-users-brett"
#define WORK L"C:\\Users\\brett\\Documents\\Codex\\work"
#define GAME WORK L"\\arceus-games\\ArceusTest"
static int fail(const wchar_t* what){wchar_t msg[512];swprintf(msg,512,L"%ls\nWindows error: %lu\nNo running game will be closed.",what,GetLastError());MessageBoxW(NULL,msg,L"Arceus 1440p controller test",MB_OK|MB_ICONERROR);return 1;}
static HANDLE output(const wchar_t* dir,const wchar_t* name){wchar_t path[1024];swprintf(path,1024,L"%ls\\%ls",dir,name);SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};return CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);}
static void env(const wchar_t* k,const wchar_t* v){SetEnvironmentVariableW(k,v);}
int WINAPI wWinMain(HINSTANCE a,HINSTANCE b,LPWSTR command,int show){
 (void)a;(void)b;(void)command;(void)show;
 HANDLE lock=CreateMutexW(NULL,FALSE,L"Local\\ArceusControllerResearchLaunch");if(!lock)return fail(L"Cannot create launcher lock");if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(NULL,L"A controller test launcher is already active. Close the game yourself before another test.",L"Arceus 1440p controller test",MB_OK);CloseHandle(lock);return 0;}
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return fail(L"Cannot check for an existing game");PROCESSENTRY32W e={.dwSize=sizeof e};BOOL more=Process32FirstW(snap,&e);while(more){if(!_wcsicmp(e.szExeFile,L"ArceusTest.exe")){CloseHandle(snap);MessageBoxW(NULL,L"ArceusTest is already running. Save and close it yourself before starting another test.",L"Arceus 1440p controller test",MB_OK);CloseHandle(lock);return 0;}more=Process32NextW(snap,&e);}CloseHandle(snap);
 SYSTEMTIME t;GetLocalTime(&t);wchar_t run[1024],path[1024];swprintf(run,1024,WORK L"\\arceus-runs\\timing-%04u%02u%02u-%02u%02u%02u-%03u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);if(!CreateDirectoryW(run,NULL))return fail(L"Cannot create local capture directory");
 if(!CopyFileW(ROOT L"\\work\\ArceusEngineCandidate-runtime.exe",GAME L"\\ArceusTest.exe",FALSE))return fail(L"Cannot install prepared test runtime");
 wchar_t prior[32768],search[32768];DWORD n=GetEnvironmentVariableW(L"PATH",prior,30000);if(!n||n>=30000)prior[0]=0;swprintf(search,32768,WORK L"\\msys64\\clang64\\bin;" WORK L"\\llvm-mingw-20261104-ucrt-x86_64\\bin;%ls",prior);env(L"PATH",search);env(L"APPDATA",WORK L"\\arceus-appdata");
 env(L"SNX_ARCEUS_FPS60",L"0");env(L"SNX_ARCEUS_TARGET_FPS",L"120");env(L"SNX_ARCEUS_CLOCK_TRACE",L"1");env(L"SNX_ARCEUS_TIMING_FIXES",L"1");env(L"SNX_ARCEUS_EVENT_CAMERA_FIX",L"1");env(L"SNX_ARCEUS_BLINK_FIX",L"1");env(L"SNX_ARCEUS_CAMERA_INPUT_FIX",L"1");env(L"SNX_ARCEUS_CAMERA_SPEED",L"2.2");env(L"SNX_ARCEUS_MENU_REPEAT_FIX",L"1");env(L"SNX_ARCEUS_DIALOGUE_TRACE",L"1");env(L"SNX_ARCEUS_TIMING_RESEARCH",L"1");env(L"SNX_ARCEUS_CAMERA_TRACE",L"0");env(L"SNX_ARCEUS_FIXED_RESOLUTION",L"1");env(L"SNX_ARCEUS_SCREEN_FETCH",L"1");env(L"SNX_RESOLUTION_TRACE",L"1");env(L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS",L"1");
 swprintf(path,1024,L"%ls\\submissions.csv",run);env(L"SNX_PRESENT_TRACE",path);
 FILE* f=_wfopen(ROOT L"\\outputs\\current-controller-test.txt",L"wt, ccs=UTF-8");if(f){fwprintf(f,L"%ls\n1440p render-scale test; precise screen-fetch fix; 120 FPS target; Vulkan; camera2.2; menu-repeat/blink/event-camera/timing fixes; manual close; controller input enabled.\n",run);fclose(f);}
 swprintf(path,1024,L"%ls\\test-settings.json",run);f=_wfopen(path,L"wt");if(f){fprintf(f,"{\"target_fps\":120,\"camera_speed\":2.2,\"menu_repeat\":true,\"resolution_scale_override\":1.3333333333333333,\"docked\":true,\"resolution_trace\":true,\"fixed_scene_resolution\":true,\"screen_fetch_fix\":true,\"engine_candidate\":true,\"fullscreen\":true,\"controller_background\":true,\"desktop_launcher\":true,\"seconds\":0}\n");fclose(f);}
 SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};HANDLE in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);HANDLE out=output(run,L"game-out.txt"),err=output(run,L"game-err.txt");if(in==INVALID_HANDLE_VALUE||out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE)return fail(L"Cannot open capture streams");
 STARTUPINFOW si={.cb=sizeof si,.dwFlags=STARTF_USESTDHANDLES,.hStdInput=in,.hStdOutput=out,.hStdError=err};PROCESS_INFORMATION game={0};wchar_t args[]=L"\"" GAME L"\\ArceusTest.exe\" --nvn_native=0 --fps_limit=game --fullscreen=1 --log=1 --seconds=0 --gamepad_background=1 --docked=1 --resolution=1.3333333333333333";
 if(!CreateProcessW(GAME L"\\ArceusTest.exe",args,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,GAME,&si,&game))return fail(L"Cannot start the controller test");CloseHandle(game.hThread);CloseHandle(in);CloseHandle(out);CloseHandle(err);
 swprintf(path,1024,L"%ls\\launch-status.txt",run);f=_wfopen(path,L"wt");if(f){fprintf(f,"game_pid=%lu\nlauncher_pid=%lu\nmanual_close=1\n",game.dwProcessId,GetCurrentProcessId());fclose(f);}
 WaitForSingleObject(game.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(game.hProcess,&code);CloseHandle(game.hProcess);f=_wfopen(path,L"at");if(f){fprintf(f,"game_exit=%lu\n",code);fclose(f);}CloseHandle(lock);return 0;
}
