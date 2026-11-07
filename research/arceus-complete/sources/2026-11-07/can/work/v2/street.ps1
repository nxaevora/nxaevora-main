$ErrorActionPreference='Stop'
$w="$env:USERPROFILE\Documents\Codex\work"
$d="$w\arceus-runs\offline-village-20261106-221228"
$g="$d\ArceusTest"
$env:PATH="$w\msys64\clang64\bin;$w\llvm-mingw-20261104-ucrt-x86_64\bin;$env:PATH"
$env:APPDATA="$d\appdata"
$env:ARCEUS_UPSCALER='DLSS'
$env:ARCEUS_GEOMETRY_SAMPLES="$pwd\work\assets\street"
$env:ARCEUS_RT_VERIFY='1'
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
$p=Start-Process (Resolve-Path outputs/world-pt-v2-runtime/ArceusTest.exe).Path -ArgumentList "--game=$g --seconds=90 --shot_every=0 --shot=$pwd\work\v2\probe --resolution=2.0 --fullscreen=0 --controls=0 --docked=1 --nvn_native=0 --native_bc=1 --log=1 --script=5000:1,8000:1,11000:1,14000:1,17000:1,25000:20000000001,25250:20000000000,25500:20000000000,25750:20000000001,26000:20000000000,26250:20000000000,26500:20000000001,26750:20000000000,27000:20000000000,27250:20000000001,27500:20000000000,27750:20000000000,28000:20000000001,28250:20000000000,28500:20000000000,28750:20000000001,29000:20000000000,29250:20000000000,29500:20000000001,29750:20000000000,33250:1,34000:1,34750:1,35500:1,36250:1,37000:1,37750:1,38500:1,39250:1,40000:1,40750:1,41500:1,42250:1,43000:1,43750:1,44500:1,45250:1,46000:1,46750:1,47500:1,48250:1,49000:1,49750:1,50500:1,51250:1,52000:1,52750:1,53500:1,54250:1,55000:1,55750:1,56500:1,59000:20000000000,59250:20000000000,59500:20000000000,59750:20000000000,60000:20000000000,60250:20000000000,60500:20000000000,60750:20000000000,61000:20000000000" -WorkingDirectory $g -WindowStyle Hidden -RedirectStandardError "$pwd\work\v2\game-err.txt" -RedirectStandardOutput "$pwd\work\v2\game-out.txt" -PassThru
if(!$p.WaitForExit(135000)){$p.Kill();throw 'Test timed out'}
if($p.ExitCode){throw "Runtime exit $($p.ExitCode)"}
rg 'arceus-rt:|DLSS pre-HUD.*frame=1 ' work/v2/game-err.txt









