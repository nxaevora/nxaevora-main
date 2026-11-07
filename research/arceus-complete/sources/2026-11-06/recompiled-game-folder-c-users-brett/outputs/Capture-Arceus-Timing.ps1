# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
param([int]$Seconds=0, [switch]$CaptureDisplay, [switch]$Experimental60, [switch]$DialogueTrace, [ValidateSet(0,60,120)][int]$ExperimentalFps=0, [switch]$TimingFixes, [switch]$EventCameraFix, [switch]$BlinkFix, [switch]$CameraInputFix, [switch]$MenuRepeatFix, [ValidateRange(0,16)][double]$ResolutionScale=0, [switch]$Fullscreen, [switch]$ResolutionTrace, [switch]$FixedSceneResolution, [switch]$ScreenFetchFix, [switch]$EngineCandidate, [switch]$CameraTrace, [switch]$TimingResearch, [ValidateRange(0.25,4.0)][double]$CameraSpeed=1.0)
$ErrorActionPreference='Stop'
if ($Seconds -ne 0 -and ($Seconds -lt 10 -or $Seconds -gt 3600)) { throw 'Seconds must be 0 (close manually), or 10 through 3600.' }
. C:/Users/brett/Documents/Codex/work/nvn-src/scripts/env.ps1
$env:PATH='C:\Users\brett\Documents\Codex\work\msys64\clang64\bin;'+$env:PATH
$env:APPDATA='C:\Users\brett\Documents\Codex\work\arceus-appdata'
$gameDir='C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest'
if ($Experimental60) {
    if ($ExperimentalFps -ne 0 -and $ExperimentalFps -ne 60) { throw 'Conflicting experimental FPS selections.' }
    $ExperimentalFps=60
}
$env:SNX_ARCEUS_FPS60=if ($Experimental60) { '1' } else { '0' }
$env:SNX_ARCEUS_TARGET_FPS=if ($ExperimentalFps) { "$ExperimentalFps" } else { '' }
$env:SNX_ARCEUS_DIALOGUE_TRACE=if ($DialogueTrace) { '1' } else { '0' }
$env:SNX_ARCEUS_BLINK_FIX=if ($BlinkFix) { '1' } else { '0' }
if ($ResolutionScale -gt 0 -and $ResolutionScale -lt 0.25) { throw 'ResolutionScale must be 0 (saved setting) or at least 0.25.' }
$env:SNX_ARCEUS_FIXED_RESOLUTION=if ($FixedSceneResolution) { '1' } else { '0' }
$env:SNX_ARCEUS_SCREEN_FETCH=if ($ScreenFetchFix) { '1' } else { '0' }
$env:SNX_RESOLUTION_TRACE=if ($ResolutionTrace) { '1' } else { '0' }
$env:SNX_ARCEUS_MENU_REPEAT_FIX=if ($MenuRepeatFix) { '1' } else { '0' }
if ($MenuRepeatFix -and !$TimingFixes) { throw 'Menu repeat fix requires TimingFixes.' }
$env:SNX_ARCEUS_CAMERA_SPEED=$CameraSpeed.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:SNX_ARCEUS_TIMING_RESEARCH=if ($TimingResearch) { '1' } else { '0' }
$env:SNX_ARCEUS_CAMERA_TRACE=if ($CameraTrace) { '1' } else { '0' }
$env:SNX_ARCEUS_CAMERA_INPUT_FIX=if ($CameraInputFix) { '1' } else { '0' }
if (($BlinkFix -or $CameraInputFix) -and !$TimingFixes) { throw 'Blink/camera input fixes require TimingFixes.' }
$env:SNX_ARCEUS_EVENT_CAMERA_FIX=if ($EventCameraFix) { '1' } else { '0' }
if ($EventCameraFix -and !$TimingFixes) { throw 'Event camera correction requires TimingFixes.' }
$env:SNX_ARCEUS_TIMING_FIXES=if ($TimingFixes) { '1' } else { '0' }
if ($TimingFixes -and !$ExperimentalFps) { throw 'Timing fixes require experimental 60 or 120 FPS.' }
if ($ExperimentalFps -or $CameraTrace -or $ScreenFetchFix -or $EngineCandidate) {
    if (Get-Process -Name ArceusTest -ErrorAction SilentlyContinue) { throw 'Close the current game window before starting the FPS experiment.' }
    $runtime='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\work\ArceusClockExperiment-runtime.exe'
    if ($ScreenFetchFix -or $EngineCandidate) {
        $runtime='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\work\ArceusEngineCandidate-runtime.exe'
    }
    Copy-Item -LiteralPath $runtime -Destination "$gameDir\ArceusTest.exe"
    $env:SNX_ARCEUS_CLOCK_TRACE='1'
}
$modeLabel=if ($ExperimentalFps) { "EXPERIMENTAL $ExperimentalFps FPS elapsed-time clocks" } else { '30 FPS baseline' }
$runDir=Join-Path 'C:\Users\brett\Documents\Codex\work\arceus-runs' ('timing-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$env:SNX_PRESENT_TRACE=Join-Path $runDir 'submissions.csv'
$pm=$null
$sessionName='ArceusTiming'+$PID
$presentMon='C:\Program Files\NVIDIA Corporation\FrameViewSDK\bin\PresentMon_x64.exe'
if ($CaptureDisplay) {
    $pmArgs=@('--process_name','ArceusTest.exe','--output_file',"$runDir\display.csv",'--no_console_stats','--session_name',$sessionName,'--terminate_on_proc_exit')
    if ($Seconds -gt 0) { $pmArgs+=@('--timed',($Seconds+5),'--terminate_after_timed') }
    $pm=Start-Process -FilePath $presentMon -ArgumentList $pmArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$runDir\presentmon-out.txt" -RedirectStandardError "$runDir\presentmon-err.txt"
    $null=$pm.Handle
}
if ($Seconds -eq 0) {
    Write-Host "$modeLabel capture. No time limit. Reach a save point, save in-game, then close the game window."
} else {
    Write-Host "$modeLabel capture. The game will close after $Seconds seconds."
}
Write-Host "Local results: $runDir"
$gameArgs=@('--nvn_native=0','--fps_limit=game',('--fullscreen='+[int][bool]$Fullscreen),'--log=1',"--seconds=$Seconds",'--gamepad_background=1')
if ($ResolutionScale -gt 0) { $gameArgs+=('--resolution='+$ResolutionScale.ToString([Globalization.CultureInfo]::InvariantCulture)) }
@{target_fps=$ExperimentalFps;camera_speed=$CameraSpeed;menu_repeat=[bool]$MenuRepeatFix;resolution_scale_override=$ResolutionScale;resolution_trace=[bool]$ResolutionTrace;fixed_scene_resolution=[bool]$FixedSceneResolution;screen_fetch_fix=[bool]$ScreenFetchFix;engine_candidate=[bool]($EngineCandidate -or $ScreenFetchFix);fullscreen=[bool]$Fullscreen;runtime_sha256=(Get-FileHash "$gameDir/ArceusTest.exe").Hash} | ConvertTo-Json | Set-Content "$runDir/test-settings.json"
$game=Start-Process -FilePath "$gameDir\ArceusTest.exe" -ArgumentList $gameArgs -WorkingDirectory $gameDir -PassThru -RedirectStandardOutput "$runDir\game-out.txt" -RedirectStandardError "$runDir\game-err.txt"
$null=$game.Handle
$game.WaitForExit()
Write-Host "Game exit: $($game.ExitCode)"
if ($pm) {
    if (!$pm.WaitForExit(3000)) {
        $stop=Start-Process -FilePath $presentMon -ArgumentList @('--session_name',$sessionName,'--terminate_existing_session') -WindowStyle Hidden -PassThru
        if (!$stop.WaitForExit(5000)) { Stop-Process -Id $stop.Id }
        if (!$pm.WaitForExit(10000)) { Stop-Process -Id $pm.Id; $pm.WaitForExit() }
    }
    Write-Host "PresentMon exit: $($pm.ExitCode)"
    if (!(Test-Path "$runDir\display.csv")) { Write-Warning 'PresentMon did not produce display evidence. Try this capture from an Administrator PowerShell. The submission trace is not display FPS.' }
}
& C:/Users/brett/.local/bin/python3.14.exe (Join-Path $PSScriptRoot 'summarize-present.py') "$runDir\submissions.csv" | Tee-Object -FilePath "$runDir\summary.json"
& C:/Users/brett/.local/bin/python3.14.exe (Join-Path $PSScriptRoot 'summarize-arceus-research.py') "$runDir" | Set-Content "$runDir/research-summary.json"
Write-Host "Results remain local: $runDir"

