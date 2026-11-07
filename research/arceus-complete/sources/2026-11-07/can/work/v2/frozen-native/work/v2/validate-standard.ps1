param([Parameter(Mandatory)][string]$Stage,[int]$TextureLimit=0,[switch]$Fsr,
      [Parameter(Mandatory)][string]$Terrain,[switch]$RayReconstruction,[int]$ShotInterval=-1,[switch]$ExpectRrFallback,[ValidateSet('Create','Evaluate','NoNgx')][string]$FallbackKind='Create')
$ErrorActionPreference='Stop'
if (!(Test-Path -LiteralPath $Terrain) -and $Terrain -eq (Split-Path -Leaf $Terrain)) {$Terrain=Join-Path work/v2 $Terrain}
$runtime="work/v2/$Stage/world-pt-v2-runtime"
$env:VK_LAYER_PATH=(Resolve-Path work/v2/validation/clang64/bin).Path
foreach($name in 'libc++.dll','libSPIRV-Tools.dll','libSPIRV-Tools-opt.dll'){
    Copy-Item (Join-Path $env:VK_LAYER_PATH $name) $runtime
}
$env:VK_LAYER_DUPLICATE_MESSAGE_LIMIT="180"
$env:ARCEUS_PT_TEXTURE_LIMIT=[string]$TextureLimit
$env:ARCEUS_NGX=if($Fsr){'0'}else{'1'}
$env:ARCEUS_NGX_LOG='1'
$api=if($Fsr){'FSR'}else{'DLSS'}
if($RayReconstruction){$api=if($ExpectRrFallback){"RR-fallback"}else{"RR"}}
if($ExpectRrFallback -and $FallbackKind -ne 'Create'){$api="RR-$FallbackKind"}
$log="work/v2/validate-$Stage-$api-$TextureLimit.log"
if(!$RayReconstruction -and $TextureLimit -eq 0 -and $ShotInterval -lt 0){$ShotInterval=720}
$label="validate-$Stage-$api-$TextureLimit"
try {
    & ./work/v2/filter-test.ps1 on -Stage $Stage -Label $label -Village -Fsr:$Fsr -Terrain $Terrain -Validation -RayReconstruction:$RayReconstruction -ShotInterval $ShotInterval -ExpectRrFallback:$ExpectRrFallback -FallbackKind $FallbackKind | Out-Null
} finally {
    Copy-Item "work/v2/filter-$label/game.log" $log
}
$fallbackFsr=$ExpectRrFallback -and $FallbackKind -eq 'NoNgx'
$backend=if($Fsr -or $fallbackFsr){'FSR 3\.1\.6'}else{'DLSS'}
$slots=if($TextureLimit){$TextureLimit}else{192}
$signals=if($RayReconstruction -and !$ExpectRrFallback){@("arceus-rr: Ray Reconstruction created 1920x1080 -> 3840x2160","arceus-rr: evaluated raw radiance at present 2160")}else{@("temporal radiance filter ready","arceus-upscaler: $backend pre-HUD 1920x1080 -> 3840x2160 frame=1 ")}
foreach($required in (@($signals)+@("^frames=2208 ","^arceus-rt: $slots material texture slots","retainedPbrHits=[1-9]","debug: vk_validation=1","vulkan validation:"))){
    if(!(Select-String -Quiet -Path $log -Pattern $required)){throw "Missing $required in $log"}
}
if(!$Fsr -and !$fallbackFsr -and (!$RayReconstruction -or $ExpectRrFallback) -and !(Select-String -Quiet -Path $log -Pattern 'ModelPreset, Value: 13, Requested: Yes, Applied: Yes')){throw 'DLSS preset M missing'}
$issues=Select-String -Path $log -Pattern 'VUID-[A-Za-z]|device lost|vendor dispatch failed|undefined values|validation layer.*not (found|available)'
$unexpected=Select-String -Path $log -Pattern "vulkan validation:" | Where-Object { $_.Line -notmatch "has an Output value declared at Location|and this write is unused|which is the duplicate_message_limit value" }
if($unexpected){throw $unexpected[0].Line}
if($issues){$issues | Select-Object -First 1;throw "Graphics validation failed in $log"}
Select-String -Path $log -Pattern '^frames=|material texture slots'

