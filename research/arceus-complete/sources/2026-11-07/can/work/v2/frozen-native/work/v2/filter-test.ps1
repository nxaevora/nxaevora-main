param([ValidateSet('on','off')]$Mode, [string]$Stage='temporal-m',[string]$Label=$Mode,[switch]$Village,[switch]$Fsr,
      [string]$Terrain='work/v2/retained/world-pt-v2-runtime/terrain',[switch]$LongRun,[switch]$LauncherTiming,[switch]$Validation,[switch]$RayReconstruction,[int]$ShotInterval=-1,[switch]$ExpectRrFallback,[ValidateSet('Create','Evaluate','NoNgx')][string]$FallbackKind='Create')
$ErrorActionPreference='Stop'
$run="$env:USERPROFILE\Documents\Codex\work\arceus-runs\offline-village-20261106-221228"
$game=Join-Path $run 'ArceusTest'
$toolsHome="$env:USERPROFILE\Documents\Codex\work"
$env:PATH="$toolsHome\msys64\clang64\bin;$toolsHome\llvm-mingw-20261104-ucrt-x86_64\bin;$env:PATH"
$env:APPDATA=Join-Path $run 'appdata'
$env:ARCEUS_UPSCALER=if($Fsr){'FSR'}else{'DLSS'}
$env:ARCEUS_NGX=if($Fsr){'0'}else{'1'}
$env:ARCEUS_NGX_LOG='1'
if($ExpectRrFallback -and !$RayReconstruction){throw "Fallback test needs RR requested"}
$expectRR=$RayReconstruction -and !$ExpectRrFallback
if($RayReconstruction -and $Fsr){throw "RR requires the DLSS backend"}
$env:ARCEUS_DLSS_RR=if($RayReconstruction){"1"}else{"0"}
$env:ARCEUS_PT_FILTER=if($Mode -eq 'on'){'1'}else{'0'}
$env:ARCEUS_TERRAIN_DIR=(Resolve-Path $Terrain).Path
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
if($LauncherTiming){
    $env:SNX_ARCEUS_CLOCK_TRACE='1'
    $env:SNX_ARCEUS_TARGET_FPS='120'
    $env:SNX_ARCEUS_FPS60='0'
    $env:SNX_ARCEUS_TIMING_FIXES='1'
    $env:SNX_ARCEUS_EVENT_CAMERA_FIX='1'
    $env:SNX_ARCEUS_BLINK_FIX='1'
    $env:SNX_ARCEUS_CAMERA_INPUT_FIX='1'
    $env:SNX_ARCEUS_CAMERA_SPEED='2.2'
    $env:SNX_ARCEUS_MENU_REPEAT_FIX='1'
    $env:SNX_ARCEUS_TIMING_RESEARCH='1'
    $env:SNX_ARCEUS_DIALOGUE_TRACE='1'
    $env:SNX_ARCEUS_SCREEN_FETCH='1'
}
$directory=New-Item -Path "work/v2/filter-$Label" -ItemType Directory -Force
if($expectRR -and $Validation){$env:ARCEUS_QA_RR_CAPTURE=Join-Path $directory "rr-guides.bin"}
$exe=(Resolve-Path "work/v2/$Stage/world-pt-v2-runtime/ArceusTest.exe").Path
$route='100f:1,160f:1,220f:1,280f:1,340f:1,400f:1,460f:1,520f:1,580f:1,640f:1'
$frames=1440
$interval=48
if($Village){
    $steps=@($route)
    foreach($frame in 700..840 | Where-Object {$_ % 6 -eq 0}){
        $mask=if($frame % 18 -eq 0){'20000000001'}else{'20000000000'}
        $steps+=("${frame}f:$mask")
    }
    foreach($frame in 900..1700 | Where-Object {$_ % 27 -eq 0}){$steps+=("${frame}f:1")}
    foreach($frame in 1770..1860 | Where-Object {$_ % 6 -eq 0}){$steps+=("${frame}f:20000000000")}
    foreach($frame in 1944..2016 | Where-Object {$_ % 6 -eq 0}){$steps+=("${frame}f:100000000000")}
    $route=$steps -join ','
    $frames=2208
    $interval=24
    if($env:ARCEUS_SHOT_FIRST){$interval=1}
}
$seconds=160
if($LongRun){
    $seconds=360
    $frames=7200
    $interval=1200
}
if($Validation){$seconds=[Math]::Max(300,$seconds);$interval=0;$env:ARCEUS_RT_VERIFY="1"}
if($ShotInterval -ge 0){$interval=$ShotInterval}
$options="--vk_validation=$([int]$Validation.IsPresent) --game=$game --seconds=$seconds --frames=$frames --shot_every=$interval --shot=$directory\screen --resolution=2.0 --fullscreen=0 --controls=0 --docked=1 --nvn_native=0 --native_bc=1 --log=1 --script=$route"
$lockPath=Join-Path $PSScriptRoot 'renderer-validation.lock'
$lease=$null
while(!$lease){
    try {$lease=[IO.File]::Open($lockPath,'OpenOrCreate','ReadWrite','None')}
    catch [IO.IOException] {
        if(($_.Exception.HResult -band 0xffff) -notin 32,33){throw}
        Start-Sleep -Milliseconds 500
    }
}
$folderFile=Join-Path $game 'ArceusTest.folders'
$originalFolders=if(Test-Path -LiteralPath $folderFile){[IO.File]::ReadAllBytes($folderFile)}
try {
    # Each historical renderer keeps its own mutable QA cache in the offline game copy.
    $cacheRoot=New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot "$Stage/test-cache")
    $oldText=if($originalFolders){[Text.Encoding]::UTF8.GetString($originalFolders)}else{''}
    $folderText=($oldText -replace '(?m)^cache=[^\r\n]*','').TrimEnd()+[Environment]::NewLine+'cache='+$cacheRoot.FullName+[Environment]::NewLine
    [IO.File]::WriteAllText($folderFile,$folderText)
    $process=Start-Process $exe -ArgumentList $options -WorkingDirectory $game -WindowStyle Hidden -PassThru -RedirectStandardError "$directory/game.log" -RedirectStandardOutput "$directory/stdout.log"
    if(!$process.WaitForExit(($seconds+30)*1000)){$process.Kill();throw 'Capture timed out'}
    if($process.ExitCode){throw "Capture exited $($process.ExitCode)"}
} finally {
    if($null -ne $originalFolders){[IO.File]::WriteAllBytes($folderFile,[byte[]]$originalFolders)}
    elseif(Test-Path -LiteralPath $folderFile){Remove-Item -LiteralPath $folderFile}
    $lease.Dispose()
}
$fallbackFsr=$ExpectRrFallback -and $FallbackKind -eq 'NoNgx'
$backend=if($expectRR){'DLSS RR'}elseif($Fsr -or $fallbackFsr){'FSR 3.1.6'}else{'DLSS'}
$log=Join-Path $directory 'game.log'
if(!$expectRR -and !(Select-String -LiteralPath $log -SimpleMatch "$backend pre-HUD 1920x1080 -> 3840x2160" -Quiet)){throw "Expected $backend reconstruction"}
if(!$Fsr -and !$expectRR -and !$fallbackFsr -and !(Select-String -LiteralPath $log -Pattern 'ModelPreset, Value: 13,.*Applied: Yes' -Quiet)){throw 'DLSS preset M was not confirmed'}
if($expectRR -and !(Select-String -LiteralPath $log -Pattern 'arceus-rr: evaluated raw radiance at present (19|20|21)\d\d' -Quiet)){throw "Missing a successful late RR evaluation"}
if(!$expectRR -and $Mode -eq 'on' -and !(Select-String -LiteralPath $log -SimpleMatch 'temporal radiance filter ready' -Quiet)){throw 'Filter did not initialize'}
if($ExpectRrFallback -and $FallbackKind -eq 'Create' -and (!(Select-String -LiteralPath $log -SimpleMatch "arceus-rr: feature creation failed" -Quiet) -or (Select-String -LiteralPath $log -SimpleMatch "evaluating raw radiance" -Quiet))){throw "Injected RR creation failure did not fall back"}
if($ExpectRrFallback -and $FallbackKind -ne 'Create'){
    $required=if($fallbackFsr){@('NGX unavailable, selecting standard FSR','backend=FSR 3.1.6 present=2160 standardFilter=1')}
              else{@('evaluating raw radiance with four guides','evaluation failed (0xbad00002)','standard path resumed','backend=DLSS present=2160 standardFilter=1')}
    foreach($signal in $required){if(!(Select-String -LiteralPath $log -SimpleMatch $signal -Quiet)){throw "Missing fallback evidence: $signal"}}
}
if(!(Select-String -LiteralPath $log -Pattern "^frames=$frames hash=" -Quiet)){throw "Capture ended before $frames presents"}
Select-String -Path "$directory/game.log" -Pattern 'frames=.*hash=|temporal radiance filter|ModelPreset, Value'
