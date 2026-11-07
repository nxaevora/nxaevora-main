$ErrorActionPreference='Stop'
$w="$env:USERPROFILE\Documents\Codex\work"
$d="$w\arceus-runs\offline-village-20261106-221228"
$g="$d\ArceusTest"
$env:PATH="$w\msys64\clang64\bin;$w\llvm-mingw-20261104-ucrt-x86_64\bin;$env:PATH"
$env:APPDATA="$d\appdata"
$env:ARCEUS_UPSCALER='DLSS'
$env:ARCEUS_RT_VERIFY='1'
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
$p=Start-Process (Resolve-Path outputs/world-pt-v2-runtime/ArceusTest.exe).Path -ArgumentList "--game=$g --seconds=68 --shot_every=600 --shot=$pwd\work\v2\probe --resolution=2.0 --fullscreen=0 --controls=0 --docked=1 --nvn_native=0 --native_bc=1 --log=1 --script=5000:1,8000:1,11000:1,14000:1,17000:1" -WorkingDirectory $g -WindowStyle Hidden -RedirectStandardError "$pwd\work\v2\game-err.txt" -RedirectStandardOutput "$pwd\work\v2\game-out.txt" -PassThru
if(!$p.WaitForExit(135000)){$p.Kill();throw 'Test timed out'}
if($p.ExitCode){throw "Runtime exit $($p.ExitCode)"}
rg 'arceus-rt:|DLSS pre-HUD.*frame=1 ' work/v2/game-err.txt







