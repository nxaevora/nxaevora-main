param([string]$Variant = 'fixed', [int]$Native = 1, [int]$Seconds = 60)
$ErrorActionPreference = 'Stop'
$env:APPDATA = 'C:\Users\brett\Documents\Codex\work\arceus-appdata'
$env:PATH = 'C:\Users\brett\Documents\Codex\work\msys64\clang64\bin;C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;' + $env:PATH
$env:TEMP = 'C:\Users\brett\Documents\Codex\work\tmp'
$env:TMP = $env:TEMP
$nvnGameFolder = 'C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest'
$nvnTestFolder = 'C:\Users\brett\Documents\Codex\work\arceus-runs'
New-Item -ItemType Directory -Force $nvnTestFolder | Out-Null
$nvnPrefix = "$nvnTestFolder\$Variant-native$Native"
$nvnArgs = @("--nvn_native=$Native", '--log=2', "--seconds=$Seconds", "--shot=$nvnPrefix", '--fullscreen=0', '--controls=0', '--script=5000:1,8000:1,11000:1,14000:1,17000:1,20000:1,25000:1,30000:1', '--swkbd_text=Test')
$nvnProcess = Start-Process -FilePath "$nvnGameFolder\ArceusTest.exe" -ArgumentList $nvnArgs -WorkingDirectory $nvnGameFolder -WindowStyle Hidden -RedirectStandardOutput "$nvnPrefix.stdout.log" -RedirectStandardError "$nvnPrefix.stderr.log" -PassThru
$null = $nvnProcess.Handle
$nvnDeadline = [DateTime]::UtcNow.AddMinutes(5)
while (!$nvnProcess.HasExited -and [DateTime]::UtcNow -lt $nvnDeadline) { [void]$nvnProcess.WaitForExit(1000); $nvnProcess.Refresh() }
$nvnTimedOut = !$nvnProcess.HasExited
if ($nvnTimedOut) { Stop-Process -Id $nvnProcess.Id; $nvnProcess.WaitForExit() }
$nvnProcess.WaitForExit()
$nvnResult = [ordered]@{ variant=$Variant; native=$Native; seconds=$Seconds; timed_out=$nvnTimedOut; exit_code=$nvnProcess.ExitCode; stdout="$nvnPrefix.stdout.log"; stderr="$nvnPrefix.stderr.log" }
$nvnResult | ConvertTo-Json | Set-Content -LiteralPath "$nvnPrefix.result.json" -Encoding utf8
$nvnResult | ConvertTo-Json
