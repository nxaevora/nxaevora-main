$ErrorActionPreference='Stop'
$toolsRoot='C:\Users\brett\Documents\Codex\work'
$compiler=Join-Path $toolsRoot 'llvm-mingw-20261104-ucrt-x86_64\bin'
$build=Join-Path $toolsRoot 'nvn-build'
& "$compiler\clang.exe" -std=c23 -O1 "-I$toolsRoot\nvn-src\src" "-I$build\src\common\a64dec" -c .\work\pass4-ai\ai-jit.c -o .\work\pass4-ai\ai-jit.obj
if($LASTEXITCODE){throw 'Compile failed'}
& "$compiler\clang++.exe" .\work\pass4-ai\ai-jit.obj "$build\host-tools\llvm-backend\libsnx_llvm.a" "$build\src\common\libscarletnx-common.a" "$build\host-tools\llvm-backend\arm-backend\libsnx_arm_llvm.a" "$build\host-tools\llvm-backend\arm-backend\libsnx_arm_rt.a" "$toolsRoot\msys64\clang64\lib\libLLVM-23.dll.a" -pthread -o .\work\pass4-ai\ai-jit.exe
if($LASTEXITCODE){throw 'Link failed'}
$env:PATH="$toolsRoot\msys64\clang64\bin;$compiler;"+$env:PATH
& .\work\pass4-ai\ai-jit.exe 2>&1 | Tee-Object -FilePath .\work\pass4-ai\ai-checks.txt
if($LASTEXITCODE){throw 'Checks failed'}
