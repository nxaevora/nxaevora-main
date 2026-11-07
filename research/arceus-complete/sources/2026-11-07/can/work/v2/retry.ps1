$ErrorActionPreference='Stop'
$w="$env:USERPROFILE\Documents\Codex\work"
$d="$w\arceus-runs\offline-village-20261106-221228"
$g="$d\ArceusTest"
$env:PATH="$w\msys64\clang64\bin;$w\llvm-mingw-20261104-ucrt-x86_64\bin;$env:PATH"
$env:APPDATA="$d\appdata"
$env:ARCEUS_UPSCALER='DLSS'
$env:ARCEUS_GEOMETRY_SAMPLES="$pwd\work\assets\world"
$env:ARCEUS_RT_VERIFY='1'
$env:SNX_ARCEUS_FIXED_RESOLUTION='1'
$env:SNX_ARCEUS_NEUTRAL_RIM='1'
$env:SNX_ARCEUS_NATIVE_SHADOWS='1'
$p=Start-Process (Resolve-Path outputs/world-pt-v2-runtime/ArceusTest.exe).Path -ArgumentList "--game=$g --seconds=72 --shot_every=0 --shot=$pwd\work\v2\probe --resolution=2.0 --fullscreen=0 --controls=0 --docked=1 --nvn_native=0 --native_bc=1 --log=1 --script=100f:1,160f:1,220f:1,280f:1,340f:1,400f:1,460f:1,520f:1,580f:1,640f:1" -WorkingDirectory $g -WindowStyle Hidden -RedirectStandardError "$pwd\work\v2\game-err.txt" -RedirectStandardOutput "$pwd\work\v2\game-out.txt" -PassThru
if(!$p.WaitForExit(135000)){$p.Kill();throw 'Test timed out'}
if($p.ExitCode){throw "Runtime exit $($p.ExitCode)"}
rg 'arceus-rt:|DLSS pre-HUD.*frame=1 ' work/v2/game-err.txt









