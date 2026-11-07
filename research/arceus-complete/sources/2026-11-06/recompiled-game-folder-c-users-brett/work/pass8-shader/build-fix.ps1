$ErrorActionPreference = 'Stop'
$compiler = 'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin/clang.exe'
$src = 'C:/Users/brett/Documents/Codex/work/nvn-src'
$bld = 'C:/Users/brett/Documents/Codex/work/nvn-build'
$inc = @("-I$src/src", "-I$bld/src/nx/shaderdec", "-I$src/.cache/cpm/vulkan-headers/ebcf/include")
foreach ($variant in @('original','fixed')) {
 & $compiler -O2 -std=gnu23 @inc -c "work/pass8-shader/spirv_translate-$variant.c" -o "work/pass8-shader/translate-$variant.o"
 if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
