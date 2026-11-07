$ErrorActionPreference = 'Stop'
$compiler = 'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
$src = 'C:/Users/brett/Documents/Codex/work/nvn-src'
$bld = 'C:/Users/brett/Documents/Codex/work/nvn-build'
$out = 'work/pass10-render'
$inc = @("-I$src/src", "-I$bld/src/nx/shaderdec", "-I$src/.cache/cpm/vulkan-headers/ebcf/include")
$sh = "$bld/src/nx/CMakeFiles/snx_shader.dir/gpu/shader"
& $compiler -O2 -std=gnu23 -mcx16 -DVK_NO_PROTOTYPES @inc -c "$out/spirv_translate.c" -o "$out/spirv_translate.obj"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$objs = @("$out/spirv_translate.obj", "$sh/shader.c.obj", "$sh/decode.c.obj", "$sh/structure.c.obj", "$sh/spirv_compute.c.obj", "$bld/src/common/libscarletnx-common.a")
& $compiler -O2 -std=gnu23 @inc "$out/shader_screen_fetch.c" @objs -o "$out/shader-screen-fetch.exe" -ladvapi32
if ($LASTEXITCODE) { exit $LASTEXITCODE }
New-Item -ItemType Directory -Path "$out/fixtures" -Force | Out-Null
& "$out/shader-screen-fetch.exe" "$out/fixtures" 2>&1 | Tee-Object "$out/recognizer-results.txt"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $compiler -O2 -std=gnu23 @inc "$src/tools/snx_shaderc.c" @objs "$sh/corpus.c.obj" -o "$out/shaderc.exe" -ladvapi32
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $compiler -O2 -std=gnu23 @inc work/shader-cache-replay.c @objs "$sh/corpus.c.obj" -o "$out/shaderc-replay.exe" -ladvapi32
exit $LASTEXITCODE
