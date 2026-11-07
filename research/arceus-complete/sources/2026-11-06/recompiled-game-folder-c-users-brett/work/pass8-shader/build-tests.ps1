$ErrorActionPreference = 'Stop'
$compiler = 'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
$src = 'C:/Users/brett/Documents/Codex/work/nvn-src'
$bld = 'C:/Users/brett/Documents/Codex/work/nvn-build'
$inc = @("-I$src/src", "-I$bld/src/nx/shaderdec", "-I$src/.cache/cpm/vulkan-headers/ebcf/include")
$sh = "$bld/src/nx/CMakeFiles/snx_shader.dir/gpu/shader"
$objs = @("$sh/shader.c.obj", "$sh/decode.c.obj", "$sh/structure.c.obj", "$sh/spirv_compute.c.obj", "$bld/src/common/libscarletnx-common.a")
foreach ($variant in @('original','fixed')) {
 & $compiler -O2 -std=gnu23 @inc work/pass8-shader/flow-tests.c "work/pass8-shader/translate-$variant.o" @objs -o "work/pass8-shader/flow-tests-$variant.exe" -ladvapi32
 if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
