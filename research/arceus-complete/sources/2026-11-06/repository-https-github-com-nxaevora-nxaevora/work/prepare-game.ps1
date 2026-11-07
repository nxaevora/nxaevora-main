$ErrorActionPreference = 'Stop'
. C:/Users/brett/Documents/Codex/work/nvn-src/scripts/env.ps1
$env:PATH = 'C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;' + $env:PATH + ';C:\Users\brett\Documents\Codex\work\msys64\clang64\bin'
$env:TEMP = 'C:\Users\brett\Documents\Codex\work\tmp'
$env:TMP = $env:TEMP
$env:XDG_CACHE_HOME = 'C:/Users/brett/Documents/Codex/work/arceus-cache'
$env:SNX_SCARLETNX = 'C:/Users/brett/Documents/Codex/work/nvn-build/scarletnx.exe'
$nvnDump = 'C:/Users/brett/Documents/Codex/work/arceus-dump'
$nvnGames = 'C:/Users/brett/Documents/Codex/work/arceus-games'
& $env:SNX_SCARLETNX --data C:/Users/brett/Documents/Codex/work/arceus-state -x 'D:\Extra Games\Pokemon Legends Arceus\Pokemon Legends Arceus.nsp' --update 'D:\Extra Games\Pokemon Legends Arceus\pokemon_arceus_update.nsp' --out $nvnDump --keys 'D:\Extra Games\Pokemon Legends Arceus\ProdKeys\prod.keys' --title-keys 'D:\Extra Games\Pokemon Legends Arceus\ProdKeys\title.keys'
if ($LASTEXITCODE -ne 0) { throw "Game extraction failed: $LASTEXITCODE" }
New-Item -ItemType Directory -Force "$nvnGames/ArceusTest" | Out-Null
& C:/Users/brett/.local/bin/python3.14.exe ./work/repo/tools/jump_table_scan.py $nvnDump --hints "$nvnGames/ArceusTest/ArceusTest.hints"
if ($LASTEXITCODE -ne 0) { throw "Hint scan failed: $LASTEXITCODE" }
& $env:SNX_SCARLETNX -r $nvnDump --name ArceusTest --outdir $nvnGames --jobs 8
exit $LASTEXITCODE
