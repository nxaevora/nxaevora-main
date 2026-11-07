param([string]$Stage='field-route')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot 'field-route')).Path
$game=Join-Path $root 'ArceusTest'
$exe=(Resolve-Path (Join-Path $PSScriptRoot "$Stage/world-pt-v2-runtime/ArceusTest.exe")).Path
$run=New-Item -ItemType Directory (Join-Path $root ('route-'+(Get-Date -Format 'HHmmss')))
$env:ARCEUS_QA_CONTROLLER=Join-Path $run 'input.txt'
$env:ARCEUS_QA_END_ROUTE=Join-Path $run 'finish'
$env:ARCEUS_QA_ROMFS_ROOT="$env:USERPROFILE\Documents\Codex\work\arceus-runs\offline-village-20261106-221228\ArceusTest\ArceusTest_data"
$env:APPDATA=Join-Path $root 'appdata'
$env:PATH="$env:USERPROFILE\Documents\Codex\work\msys64\clang64\bin;$env:USERPROFILE\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;$game;$env:PATH"
$env:ARCEUS_UPSCALER='DLSS'
$env:ARCEUS_NGX='1'
$env:ARCEUS_DLSS_RR='0'
$env:ARCEUS_PT_FILTER='1'
$env:ARCEUS_RT_VERIFY='1'
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
$env:ARCEUS_TERRAIN_DIR=(Resolve-Path (Join-Path $PSScriptRoot 'framed-world')).Path
$env:ARCEUS_PRESS_PRESENTS='4'
$steps=@(100..640|Where-Object {($_-100)%60 -eq 0}|ForEach-Object {"${_}f:1"})
foreach($frame in 700..840|Where-Object {$_%6 -eq 0}){
    $mask=if($frame%18 -eq 0){'20000000001'}else{'20000000000'}
    $steps+="${frame}f:$mask"
}
foreach($frame in 900..1700|Where-Object {$_%27 -eq 0}){$steps+="${frame}f:1"}
$env:ARCEUS_SHOT_FIRST='1560'
$route=$steps -join ','
$options="--game=$game --seconds=840 --frames=13000 --shot_every=60 --shot=$run\screen --resolution=2.0 --fullscreen=0 --controls=0 --docked=1 --nvn_native=0 --native_bc=1 --log=1 --script=$route"
$lease=$null
while(!$lease){
    try {$lease=[IO.File]::Open((Join-Path $PSScriptRoot 'renderer-validation.lock'),'OpenOrCreate','ReadWrite','None')}
    catch [IO.IOException] {
        if(($_.Exception.HResult -band 0xffff) -notin 32,33){throw}
        Start-Sleep -Milliseconds 500
    }
}
try {
    $proc=Start-Process $exe -ArgumentList $options -WorkingDirectory $game -WindowStyle Hidden -PassThru -RedirectStandardError "$run/game.log" -RedirectStandardOutput "$run/stdout.log"
    if(!$proc.WaitForExit(870000)){$proc.Kill();throw 'Field game timed out'}
    if($proc.ExitCode){throw "Field route exited $($proc.ExitCode)"}
} finally {$lease.Dispose()}
