from pathlib import Path
p=Path('outputs/Capture-Arceus-Timing.ps1');s=p.read_text().replace('[switch]$CaptureDisplay)','[switch]$CaptureDisplay, [switch]$Experimental60)')
pos=s.index("$runDir=Join-Path")
s=s[:pos]+'''$env:SNX_ARCEUS_FPS60=if ($Experimental60) { '1' } else { '0' }
if ($Experimental60) {
    if (Get-Process -Name ArceusTest -ErrorAction SilentlyContinue) { throw 'Close the current game window before starting the 60 FPS experiment.' }
    $runtime='C:\\Users\\brett\\Documents\\Codex\\2026-11-06\\recompiled-game-folder-c-users-brett\\work\\ArceusClockExperiment-runtime.exe'
    Copy-Item -LiteralPath $runtime -Destination "$gameDir\\ArceusTest.exe"
    $env:SNX_ARCEUS_CLOCK_TRACE='1'
}
$modeLabel=if ($Experimental60) { 'EXPERIMENTAL 60 FPS elapsed-time clocks' } else { '30 FPS baseline' }
'''+s[pos:]
s=s.replace('30 FPS baseline capture.','$modeLabel capture.')
p.write_text(s)
