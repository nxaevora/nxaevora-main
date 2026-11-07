. C:/Users/brett/Documents/Codex/work/nvn-src/scripts/env.ps1
$env:PATH='C:\Users\brett\Documents\Codex\work\msys64\clang64\bin;'+$env:PATH
$env:APPDATA='C:\Users\brett\Documents\Codex\work\arceus-appdata'
$dir='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\work'
$env:SNX_PRESENT_TRACE="$dir\baseline-present.csv"
$before=(Get-FileHash $env:SNX_PRESENT_TRACE).Hash
$p=Start-Process 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest\ArceusTest.exe' -ArgumentList '--nvn_native=0 --fps_limit=game --seconds=8 --fullscreen=0 --log=1 --controls=0' -WorkingDirectory 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest' -WindowStyle Hidden -PassThru -RedirectStandardError "$dir\existing-path-err.txt"
$null=$p.Handle;$p.WaitForExit()
"existing_path_exit=$($p.ExitCode) preserved=$($before -eq (Get-FileHash $env:SNX_PRESENT_TRACE).Hash)"
Remove-Item Env:SNX_PRESENT_TRACE
$p=Start-Process 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest\ArceusTest.exe' -ArgumentList '--nvn_native=0 --fps_limit=game --seconds=8 --fullscreen=0 --log=1 --controls=0' -WorkingDirectory 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest' -WindowStyle Hidden -PassThru -RedirectStandardError "$dir\disabled-err.txt"
$null=$p.Handle;$p.WaitForExit()
"disabled_exit=$($p.ExitCode)"
