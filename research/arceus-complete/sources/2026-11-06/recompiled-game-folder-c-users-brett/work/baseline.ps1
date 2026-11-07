$ErrorActionPreference='Stop'
. C:/Users/brett/Documents/Codex/work/nvn-src/scripts/env.ps1
$env:PATH='C:\Users\brett\Documents\Codex\work\msys64\clang64\bin;'+$env:PATH
$env:APPDATA='C:\Users\brett\Documents\Codex\work\arceus-appdata'
$env:SNX_PRESENT_TRACE='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\work\baseline-present.csv'
$dir='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\work'
$pm=Start-Process -FilePath 'C:\Program Files\NVIDIA Corporation\FrameViewSDK\bin\PresentMon_x64.exe' -ArgumentList @('--process_name','ArceusTest.exe','--output_file',"$dir\baseline-display.csv",'--timed','40','--terminate_after_timed','--no_console_stats','--session_name','ArceusTimingBaseline') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$dir\presentmon-out.txt" -RedirectStandardError "$dir\presentmon-err.txt"
$game=Start-Process -FilePath 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest\ArceusTest.exe' -ArgumentList @('--nvn_native=0','--fps_limit=game','--log=1','--seconds=35','--fullscreen=0','--controls=0') -WorkingDirectory 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$dir\baseline-out.txt" -RedirectStandardError "$dir\baseline-err.txt"
$null=$game.Handle
$game.WaitForExit(); $pm.WaitForExit()
"game_exit=$($game.ExitCode) presentmon_exit=$($pm.ExitCode)"
