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
$nvnArgs = @("--nvn_native=$Native", '--log=2', "--seconds=$Seconds", "--shot=$nvnPrefix", '--fullscreen=0', '--controls=0', '--script=5000:1,7000:1,9000:1,11000:1,13000:1,15000:1,17000:1,19000:1,21000:1,23000:1,25000:1,27000:1,29000:1,31000:1,33000:1,35000:1,37000:1,39000:1,41000:1,43000:1,45000:1,47000:1,49000:1,51000:1,53000:1,55000:1,57000:1,59000:1,61000:1,63000:1,65000:1,67000:1,69000:1,71000:1,73000:1,75000:1,77000:1,79000:1,81000:1,83000:1,85000:1,87000:1,89000:1,91000:1,93000:1,95000:1,97000:1,99000:1,101000:1,103000:1,105000:1,107000:1,109000:1,111000:1,113000:1,115000:1', '--swkbd_text=Test')
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
