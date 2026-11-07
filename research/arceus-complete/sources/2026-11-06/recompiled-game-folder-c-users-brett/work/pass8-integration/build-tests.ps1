$ErrorActionPreference = 'Stop'
$compiler = 'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
$src = 'C:/Users/brett/Documents/Codex/work/nvn-src'
$bld = 'C:/Users/brett/Documents/Codex/work/nvn-build'
$inc = @("-I$src/src", "-I$bld/src/nx/shaderdec", "-I$src/.cache/cpm/vulkan-headers/ebcf/include")
$sh = "$bld/src/nx/CMakeFiles/snx_shader.dir/gpu/shader"
$objs = @("$sh/spirv_translate.c.obj", "$sh/shader.c.obj", "$sh/decode.c.obj", "$sh/structure.c.obj", "$sh/spirv_compute.c.obj", "$bld/src/common/libscarletnx-common.a")
& $compiler -O2 -std=gnu23 @inc "$src/tests/shader_screen_fetch.c" @objs -o work/pass8-integration/shader-screen-fetch.exe -ladvapi32
if ($LASTEXITCODE) { exit $LASTEXITCODE }
New-Item -ItemType Directory -Path work/pass8-integration/fixtures -Force | Out-Null
& work/pass8-integration/shader-screen-fetch.exe work/pass8-integration/fixtures 2>&1 | Tee-Object work/pass8-integration/recognizer-results.txt
exit $LASTEXITCODE
