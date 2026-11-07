$ErrorActionPreference = 'Stop'
Set-Location C:/Users/brett/Documents/Codex/work/nvn-src
. ./scripts/env.ps1
$env:PATH = 'C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;' + $env:PATH + ';C:\Users\brett\Documents\Codex\work\msys64\clang64\bin'
$env:TEMP = 'C:\Users\brett\Documents\Codex\work\tmp'
$env:TMP = $env:TEMP
$env:PERL5LIB = 'C:/Users/brett/Documents/Codex/work/perl-shim'
$env:PERL5OPT = '-MNvnUnixPaths'
$env:LC_ALL = 'C'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '8'
cmake.exe --build C:/Users/brett/Documents/Codex/work/nvn-build --target scarletnx --parallel 8
exit $LASTEXITCODE
