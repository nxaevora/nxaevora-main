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
$argsList=@("--profile=$Profile",'--nvn_native=0','--native_bc=1','--fps_limit=game','--fullscreen=0','--log=1','--seconds=65','--docked=1','--resolution=1.3333333333333333','--script=5000:1,8000:1,11000:1,14000:1,17000:1','--controls=0',('--shot='+$r+'\scene'))
$p=Start-Process -FilePath "$g\ArceusTest.exe" -ArgumentList $argsList -WorkingDirectory $g -WindowStyle Hidden -RedirectStandardError "$r\game-err.txt" -RedirectStandardOutput "$r\game-out.txt" -PassThru
for($i=0;$i -lt 25 -and !$p.HasExited;$i++){if($p.WaitForExit(5000)){break}; if($Profile -and (Test-Path "$g\profile.txt")){Copy-Item -LiteralPath "$g\profile.txt" -Destination "$r\sample-$i.txt"}}
if(!$p.HasExited){$p.Kill();throw 'Runtime exceeded its test deadline'}
if($p.ExitCode){throw "Runtime exit $($p.ExitCode)"}
