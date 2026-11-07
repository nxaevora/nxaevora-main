from pathlib import Path
p=Path('outputs/Capture-Arceus-Timing.ps1')
s=p.read_text()
s=s.replace('param([int]$Seconds=120, [switch]$CaptureDisplay)','param([int]$Seconds=0, [switch]$CaptureDisplay)')
s=s.replace("if ($Seconds -lt 10 -or $Seconds -gt 3600) { throw 'Seconds must be 10 through 3600.' }", "if ($Seconds -ne 0 -and ($Seconds -lt 10 -or $Seconds -gt 3600)) { throw 'Seconds must be 0 (close manually), or 10 through 3600.' }")
s=s.replace('$pm=$null',"$pm=$null\n$sessionName='ArceusTiming'+$PID\n$presentMon='C:\\Program Files\\NVIDIA Corporation\\FrameViewSDK\\bin\\PresentMon_x64.exe'")
a=s.index('    $pm=Start-Process')
b=s.index('\n}',a)
s=s[:a]+'''    $pmArgs=@('--process_name','ArceusTest.exe','--output_file',"$runDir\\display.csv",'--no_console_stats','--session_name',$sessionName,'--terminate_on_proc_exit')
    if ($Seconds -gt 0) { $pmArgs+=@('--timed',($Seconds+5),'--terminate_after_timed') }
    $pm=Start-Process -FilePath $presentMon -ArgumentList $pmArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$runDir\\presentmon-out.txt" -RedirectStandardError "$runDir\\presentmon-err.txt"
    $null=$pm.Handle'''+s[b:]
s=s.replace('Write-Host "30 FPS baseline capture. Play normally for $Seconds seconds. Local results: $runDir"','''if ($Seconds -eq 0) {
    Write-Host "30 FPS baseline capture. No time limit. Reach a save point, save in-game, then close the game window."
} else {
    Write-Host "30 FPS baseline capture. The game will close after $Seconds seconds."
}
Write-Host "Local results: $runDir"''')
s=s.replace('    if (!$pm.WaitForExit(10000)) { Stop-Process -Id $pm.Id }','''    if (!$pm.WaitForExit(3000)) {
        $stop=Start-Process -FilePath $presentMon -ArgumentList @('--session_name',$sessionName,'--terminate_existing_session') -WindowStyle Hidden -PassThru
        if (!$stop.WaitForExit(5000)) { Stop-Process -Id $stop.Id }
        if (!$pm.WaitForExit(10000)) { Stop-Process -Id $pm.Id; $pm.WaitForExit() }
    }
    Write-Host "PresentMon exit: $($pm.ExitCode)"''')
p.write_text(s)
p=Path('outputs/validation.txt');s=p.read_text().replace('It runs the installed instrumented runtime for 120 seconds','It runs the installed instrumented runtime until you close the game').replace('Further 120-second runs can cover separate scenes.','Reach a save point and save in-game before closing the game window. There is no automatic timeout by default.').replace('Its full interactive 120-second workflow remains for user testing.','The revised manual-close workflow is syntax checked and remains for user testing. Pass -Seconds 120 only if you explicitly want a timed run.')
p.write_text(s)
