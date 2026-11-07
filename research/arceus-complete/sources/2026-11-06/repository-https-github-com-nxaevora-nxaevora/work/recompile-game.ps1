$env:APPDATA = 'C:\Users\brett\Documents\Codex\work\arceus-appdata'
New-Item -ItemType Directory -Force $env:APPDATA | Out-Null
$ErrorActionPreference = 'Stop'
. C:/Users/brett/Documents/Codex/work/nvn-src/scripts/env.ps1
$env:PATH = 'C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;' + $env:PATH + ';C:\Users\brett\Documents\Codex\work\msys64\clang64\bin'
$env:TEMP = 'C:\Users\brett\Documents\Codex\work\tmp'
$env:TMP = $env:TEMP
$env:XDG_CACHE_HOME = 'C:/Users/brett/Documents/Codex/work/arceus-cache'
$env:SNX_SCARLETNX = 'C:/Users/brett/Documents/Codex/work/nvn-build/scarletnx.exe'
$nvnDump = 'C:/Users/brett/Documents/Codex/work/arceus-dump'
$nvnGames = 'C:/Users/brett/Documents/Codex/work/arceus-games'
New-Item -ItemType Directory -Force "$nvnGames/ArceusTest" | Out-Null
& $env:SNX_SCARLETNX --data C:/Users/brett/Documents/Codex/work/arceus-state -r $nvnDump --name ArceusTest --outdir $nvnGames --jobs 8 | Out-Host
exit $LASTEXITCODE
