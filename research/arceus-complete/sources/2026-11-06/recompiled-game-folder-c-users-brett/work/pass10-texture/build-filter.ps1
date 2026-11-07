$ErrorActionPreference='Stop'
$src='C:/Users/brett/Documents/Codex/work/nvn-src'
$bld='C:/Users/brett/Documents/Codex/work/nvn-build'
$cc='C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
$cxx='C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang++.exe'
$inc=@('-DVK_NO_PROTOTYPES',"-I$src/src","-I$src/tests","-I$src/.cache/cpm/volk/1ccf","-I$src/.cache/cpm/vulkanmemoryallocator/985e/include","-I$src/.cache/cpm/vulkan-headers/ebcf/include","-I$src/.cache/cpm/xxhash/c49c","-I$src/.cache/cpm/sdl3/653f/include","-I$bld/_deps/sdl3-build/include-revision","-I$bld/settingsgen")
foreach($name in @('astc_vulkan','tracy_stubs')) {
 & $cc '-O2' '-std=gnu23' '-ffunction-sections' '-fdata-sections' @inc '-c' $(if($name -eq "astc_vulkan"){ "work/pass10-texture/actual-filter.c" }else{ "$src/tests/$name.c" }) '-o' "work/pass10-texture/$name.o"
 if($LASTEXITCODE){exit $LASTEXITCODE}
}
& $cc '-O2' '-std=gnu23' '-ffunction-sections' '-fdata-sections' @inc '-c' "$bld/settingsgen/settings.c" '-o' 'work/pass9-texture/settings.o'
if($LASTEXITCODE){exit $LASTEXITCODE}
& $cc '-O2' '-std=gnu23' '-ffunction-sections' '-fdata-sections' @inc '-c' 'work/pass9-texture/platform-test-support.c' '-o' 'work/pass9-texture/platform-test-support.o'
if($LASTEXITCODE){exit $LASTEXITCODE}
$vk="$bld/src/nx/CMakeFiles/snx_vk.dir"
$objs=@('astc','blocklinear','texconv','cmd','memory','record','vma') | ForEach-Object { "$vk/gpu/vk/$_"+$(if($_ -eq 'vma'){'.cpp.obj'}else{'.c.obj'}) }
$objs+=@("$vk/gpu/texture/blocklinear.c.obj","$vk/__/__/.cache/cpm/volk/1ccf/volk.c.obj")
$objs+=@('astc','blocklinear','texconv') | ForEach-Object { "$vk/__/__/generated/spirv/${_}_spv.c.obj" }
$objs+=@('plat_win32','plat_sdl') | ForEach-Object { "$bld/src/nx/CMakeFiles/snx_platform.dir/runtime/platform/$_.c.obj" }
$objs+=@('astc','bcn','etc2','settings','platform-test-support') | ForEach-Object { "work/pass9-texture/$_.o" }
& $cxx @objs "work/pass10-texture/astc_vulkan.o" "work/pass10-texture/tracy_stubs.o" "-Wl,--gc-sections" "$bld/src/common/libscarletnx-filesystem.a" "$bld/src/common/libscarletnx-common.a" "$bld/libsnx_xxhash.a" "$bld/_deps/sdl3-build/libSDL3.a" '-ladvapi32' '-luser32' '-lgdi32' '-limm32' '-lole32' '-loleaut32' '-lshell32' '-lversion' '-luuid' '-lwinmm' '-lsetupapi' '-lavrt' '-lcfgmgr32' '-o' 'work/pass10-texture/actual-filter.exe'
exit $LASTEXITCODE
