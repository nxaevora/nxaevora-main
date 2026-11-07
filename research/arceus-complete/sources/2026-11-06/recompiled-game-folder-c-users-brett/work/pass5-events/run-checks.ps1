$ErrorActionPreference='Stop'
$researchRoot='C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett'
$toolsRoot='C:\Users\brett\Documents\Codex\work'
$compiler=Join-Path $toolsRoot 'llvm-mingw-20261104-ucrt-x86_64\bin'
$build=Join-Path $toolsRoot 'nvn-build'
Set-Location -LiteralPath $researchRoot
& C:\Users\brett\.local\bin\python3.14.exe .\work\pass5-events\make-fixtures.py
if ($LASTEXITCODE) { throw 'Fixture extraction failed' }
& "$compiler\clang.exe" -std=c23 -O1 "-I$toolsRoot\nvn-src\src" "-I$build\src\common\a64dec" -c .\work\pass5-events\event-jit.c -o .\work\pass5-events\event-jit.obj
if ($LASTEXITCODE) { throw 'Harness compilation failed' }
& "$compiler\clang++.exe" .\work\pass5-events\event-jit.obj "$build\host-tools\llvm-backend\libsnx_llvm.a" "$build\src\common\libscarletnx-common.a" "$build\host-tools\llvm-backend\arm-backend\libsnx_arm_llvm.a" "$build\host-tools\llvm-backend\arm-backend\libsnx_arm_rt.a" "$toolsRoot\msys64\clang64\lib\libLLVM-23.dll.a" -pthread -o .\work\pass5-events\event-jit.exe
if ($LASTEXITCODE) { throw 'Harness linking failed' }
$env:PATH="$toolsRoot\msys64\clang64\bin;$compiler;"+$env:PATH
& .\work\pass5-events\event-jit.exe 2>&1 | Tee-Object -FilePath .\work\pass5-events\event-checks.txt
if ($LASTEXITCODE) { throw 'Guest-instruction checks failed' }

