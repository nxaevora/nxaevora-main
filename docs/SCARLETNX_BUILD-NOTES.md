# ScarletNX build notes (Windows, MSYS2 CLANG64)

This file records how `third_party/ScarletNX` was built on Windows. For the official steps, see the section
"Windows / MSYS2 CLANG64" in `third_party/ScarletNX/docs/BUILD.md`.

## Environment

- Windows 11 x64
- MSYS2 (`C:\msys64`), CLANG64 environment
- LLVM 23.1.4 (`mingw-w64-clang-x86_64-llvm` of MSYS2)
- A Vulkan 1.3 driver (needed to run games)
- No spaces in the path. The FFmpeg build fails with a space.

## 1. Install the packages

Run in an MSYS2 CLANG64 shell.

```sh
pacman -Syu
pacman -S --needed git make patch perl patchutils \
  mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-llvm mingw-w64-clang-x86_64-lld \
  mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-nasm \
  mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-spirv-tools \
  mingw-w64-clang-x86_64-glslang
```

If `pacman -Syu` closes the shell, open it again and run the command a second time.

## 2. Get the submodule

```sh
git clone --recursive <this repository>
```

If the repository is cloned already:

```sh
git submodule update --init
```

## 3. Configure and build

```sh
cd third_party/ScarletNX
cmake --preset default \
  -DCMAKE_C_FLAGS=-mcx16 \
  -DSNX_LLVM_CONFIG="$(cygpath -m "$(command -v llvm-config)")" \
  -DSNX_FFMPEG_SH="$(cygpath -m /usr/bin/sh.exe)" \
  -DSNX_FFMPEG_MAKE="$(cygpath -m /usr/bin/make.exe)" \
  -DPERL_EXECUTABLE="$(cygpath -m /usr/bin/perl.exe)" \
  -DSNX_OPENSSL_MAKE="$(cygpath -m /usr/bin/make.exe)" \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON
cmake --build --preset default
```

The output goes to `third_party/ScarletNX/build/default/` (`scarletnx.exe`, `snx-shaderc.exe`). The first build
takes a long time, because FFmpeg and OpenSSL are built from source.

## Differences from the official steps

### `-DCMAKE_C_FLAGS=-mcx16`

With the official steps, the build of two test targets (`snx-test-a64-fp` and `snx-test-a64-fp-arithmetic`)
stopped with this error:

```
helpers/integer.c:45:12: error: large atomic operation may incur significant performance penalty;
the access size (16 bytes) exceeds the max lock-free size (8 bytes) [-Werror,-Watomic-alignment]
```

The flag `-mcx16` lets the compiler use the x86-64 instruction `cmpxchg16b` for 16-byte atomic operations. With it,
the build succeeds. The upstream source is not changed.

## Portable copy

`scarletnx.exe` needs the DLLs of MSYS2. To run it without MSYS2, install it to a folder and copy the DLLs:

```sh
cmake --install build/default --prefix <folder>
# copy the DLLs that the executable imports from /clang64/bin into <folder>/bin
```

The DLLs are libLLVM-23, libc++, libxml2-16, libzstd, libiconv-2, libffi-8 and zlib1. A copy for development is in
`game/scarletnx/` (not in git). Make the copy again after each build.

## Check

```sh
build/default/scarletnx.exe --help
```

## Not verified

- `ctest` after the build
