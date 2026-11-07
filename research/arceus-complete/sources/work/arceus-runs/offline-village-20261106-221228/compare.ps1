param([string]$Label='baseline',[int]$Profile=1)
$ErrorActionPreference='Stop'
$w='C:\Users\brett\Documents\Codex\work'
$g=Join-Path $PSScriptRoot 'ArceusTest'
$r=Join-Path $PSScriptRoot $Label
New-Item -ItemType Directory -Path $r | Out-Null
$env:PATH="$w\msys64\clang64\bin;$w\llvm-mingw-20261104-ucrt-x86_64\bin;"+$env:PATH
$env:APPDATA=Join-Path $PSScriptRoot 'appdata'
$launcher=Get-Content 'C:\Users\brett\Documents\Codex\2026-11-06\fix\outputs\Capture-Village-CPU.cmd'
foreach($line in $launcher){if($line -match '^set "(SNX_[A-Z0-9_]+)=(\S+)"$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
$env:SNX_PRESENT_TRACE=Join-Path $r 'frames.csv'
$argsList=@("--profile=$Profile",'--nvn_native=0','--native_bc=1','--fps_limit=game','--fullscreen=0','--log=1','--seconds=60','--docked=1','--resolution=1.3333333333333333','--script=5000:1,8000:1,11000:1,14000:1,17000:1,25000:20000000001,25250:20000000000,25500:20000000000,25750:20000000001,26000:20000000000,26250:20000000000,26500:20000000001,26750:20000000000,27000:20000000000,27250:20000000001,27500:20000000000,27750:20000000000,28000:20000000001,28250:20000000000,28500:20000000000,28750:20000000001,29000:20000000000,29250:20000000000,29500:20000000001,29750:20000000000','--controls=0',('--shot='+$r+'\scene'))
$p=Start-Process -FilePath "$g\ArceusTest.exe" -ArgumentList $argsList -WorkingDirectory $g -WindowStyle Hidden -RedirectStandardError "$r\game-err.txt" -RedirectStandardOutput "$r\game-out.txt" -PassThru
for($i=0;$i -lt 25 -and !$p.HasExited;$i++){if($p.WaitForExit(5000)){break}; $p.Refresh(); Add-Content "$r\cpu.csv" ("{0},{1}" -f ([DateTime]::Now-$p.StartTime).TotalSeconds,$p.TotalProcessorTime.TotalSeconds); if($Profile -and (Test-Path "$g\profile.txt")){Copy-Item -LiteralPath "$g\profile.txt" -Destination "$r\sample-$i.txt"}}
if(!$p.HasExited){$p.Kill();throw 'Runtime exceeded its test deadline'}
if($p.ExitCode){throw "Runtime exit $($p.ExitCode)"}


