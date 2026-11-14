<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# NVN coverage of the host layer

Made by `tools/nvn_coverage.py` (step 4-u1 of `docs/graphics/nvn-unobserved.md`). Do not edit the
columns function to handler by hand: run the tool again. The columns checks, seen and notes are written by hand and the
tool keeps them.

- function, address: the 523 functions that the game resolves, from the `nvn:` lines of a log with `nvn_trace=1` or
  from the name table of the NVN library in the sdk module. The address is the guest address in the update program of
  update 1.1.1.
- group: G1 to G9 as in `docs/graphics/nvn-design.md`, section 2-6. G10 is a function that no run called.
- calls: the calls of the run in `docs/graphics/nvn-calls.md`, through the dispatch table and with the library (`nvn_native=0`).
- handler: `implemented` when `native_procs` of `nvn_native.c` has a host handler (or, for `nvnDeviceGetProcAddress`, the
  host lookup hook), `stub-log` when the missing-function handler answers (it stops the game unless
  `nvn_native_missing=0`, which logs the first call and returns 0).
- checks: the checks that passed, with their scope (`unit`, `selftest R/M/S/Y` with the build, `exec`). The build
  `update 1.1.1` is the library of that update: the `sdk` module with the build ID
  `B39ADD63421BE525D352E576DAB438D3812EDFF0`. The argument sets of a selftest check are the cases of the function in
  `third_party/ScarletNX/src/nx/gpu/nvn/test/nvn_selftest*.c`, and each case line of the selftest log names its
  arguments. seen: a game run with `nvn_native=1` that called it and was read. A function with checks and without seen is not verified in the game.

## Summary

| group | functions | implemented | stub-log | called | called and stub-log |
| --- | --- | --- | --- | --- | --- |
| G1 | 17 | 17 | 0 | 17 | 0 |
| G2 | 10 | 10 | 0 | 10 | 0 |
| G3 | 27 | 27 | 0 | 27 | 0 |
| G4 | 9 | 9 | 0 | 9 | 0 |
| G5 | 4 | 4 | 0 | 4 | 0 |
| G6 | 16 | 16 | 0 | 16 | 0 |
| G7 | 4 | 4 | 0 | 4 | 0 |
| G8 | 112 | 112 | 0 | 112 | 0 |
| G9 | 43 | 43 | 0 | 43 | 0 |
| G10 | 281 | 281 | 0 | 0 | 0 |
| all | 523 | 523 | 0 | 242 | 0 |

## Functions

| function | group | address | calls | handler | checks | seen | notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `nvnDeviceBuilderSetDefaults` | G1 | d82cdf0 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceBuilderSetFlags` | G1 | d82ce00 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceGetInteger` | G1 | d82d060 | 127349 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceGetTimestampInNanoseconds` | G1 | d82d0e0 | 341922 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceInitialize` | G1 | d82d110 | 1 | implemented |  |  | no check: copies the builder flags into a new host record, no pure part; the selftest makes no second library device |
| `nvnDeviceSetDepthMode` | G1 | d82d170 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceSetWindowOriginMode` | G1 | d82d190 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueBuilderGetQueueMemorySize` | G1 | d8383f0 | 1 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderSetCommandMemorySize` | G1 | d838150 | 1 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueBuilderSetComputeMemorySize` | G1 | d838160 | 1 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueBuilderSetControlMemorySize` | G1 | d838170 | 1 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueBuilderSetDefaults` | G1 | d838120 | 1 | implemented | selftest R/M (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderSetDevice` | G1 | d838110 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderSetFlags` | G1 | d838140 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueBuilderSetQueueMemory` | G1 | d838190 | 1 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderSetQueuePriority` | G1 | d838570 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueInitialize` | G1 | d839360 | 1 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolBuilderSetDefaults` | G2 | d833b90 | 3231 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolBuilderSetDevice` | G2 | d833b80 | 3231 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolBuilderSetFlags` | G2 | d833c90 | 3231 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolBuilderSetStorage` | G2 | d833bb0 | 3231 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolFinalize` | G2 | d833e60 | 1974 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolGetBufferAddress` | G2 | d833f00 | 411 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolGetFlags` | G2 | d833f90 | 909 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolInitialize` | G2 | d833ce0 | 3231 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolMap` | G2 | d833ed0 | 3485 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolSetDebugLabel` | G2 | d82ccf0 | 67 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnTextureBuilderGetHeight` | G3 | d83c3a0 | 291 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetStorageAlignment` | G3 | d83c4c0 | 295 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetStorageSize` | G3 | d83d350 | 292 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetWidth` | G3 | d83c390 | 291 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetDefaults` | G3 | d83c220 | 5883 | implemented | selftest R/M (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetDepth` | G3 | d83c2a0 | 5592 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetDepthStencilMode` | G3 | d83c320 | 291 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetDevice` | G3 | d83c210 | 5883 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetFlags` | G3 | d83c260 | 5883 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetFormat` | G3 | d83c2f0 | 5883 | implemented | selftest R/M (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetHeight` | G3 | d83c290 | 5592 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetLevels` | G3 | d83c2e0 | 5883 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetPackagedTextureLayout` | G3 | d83c7a0 | 5584 | implemented | unit (2026-11-13) test_packaged_tile |  |  |
| `nvnTextureBuilderSetSize2D` | G3 | d83c2c0 | 288 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetSize3D` | G3 | d83c2d0 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetStorage` | G3 | d83c330 | 5879 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetStride` | G3 | d83c350 | 297 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetSwizzle` | G3 | d83c310 | 291 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetTarget` | G3 | d83c270 | 5883 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetWidth` | G3 | d83c280 | 5592 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureFinalize` | G3 | d83d750 | 2900 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureGetDepth` | G3 | d83d890 | 2290 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetHeight` | G3 | d83d880 | 8444 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetStride` | G3 | d83d910 | 2 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetTarget` | G3 | d83d860 | 24 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetWidth` | G3 | d83d870 | 8444 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureInitialize` | G3 | d83d3c0 | 5879 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnSyncInitialize` | G4 | d83b800 | 2 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnSyncWait` | G4 | d83bc00 | 17838 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderSetDefaults` | G4 | d83e580 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowBuilderSetDevice` | G4 | d83e570 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowBuilderSetNativeWindow` | G4 | d83e590 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowBuilderSetTextures` | G4 | d83e5a0 | 1 | implemented | unit (2026-11-13) test_window_params |  |  |
| `nvnWindowInitialize` | G4 | d83e7d0 | 1 | implemented | unit (2026-11-13) test_window_params (nvn_window_initialize_check, nvn_window_crop) |  | the rest of the record fill is direct copies; the selftest makes no library window |
| `nvnWindowSetCrop` | G4 | d83eb30 | 8919 | implemented | unit (2026-11-13) test_window_params (nvn_window_crop) |  |  |
| `nvnWindowSetPresentInterval` | G4 | d83ea40 | 1 | implemented |  |  | no check: a direct store of a host record field, no pure part; the selftest makes no library window |
| `nvnSamplerPoolGetSize` | G5 | d83aad0 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolInitialize` | G5 | d83a700 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTexturePoolGetSize` | G5 | d83dc90 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTexturePoolInitialize` | G5 | d83d930 | 1 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnCommandBufferAddCommandMemory` | G6 | d823db0 | 1766963 | implemented | exec (update 1.1.1, 2026-11-13) |  | no own selftest case: every command buffer case calls it |
| `nvnCommandBufferAddControlMemory` | G6 | d823fa0 | 1473674 | implemented | exec (update 1.1.1, 2026-11-13) |  | no own selftest case: every command buffer case calls it |
| `nvnCommandBufferBarrier` | G6 | d856590 | 740540 | implemented | selftest S/Y (update 1.1.1, 2026-11-11) |  |  |
| `nvnCommandBufferBeginRecording` | G6 | d8240f0 | 1333339 | implemented | selftest R (update 1.1.1, 2026-11-13), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferCallCommands` | G6 | d8259e0 | 4340061 | implemented | selftest S (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferClearColor` | G6 | d84a870 | 7235 | implemented | selftest S (update 1.1.1, 2026-11-11) |  |  |
| `nvnCommandBufferEndRecording` | G6 | d8241a0 | 1333339 | implemented | selftest R (update 1.1.1, 2026-11-13), exec (update 1.1.1, 2026-11-13) |  | R compares the handle as nonzero only: its value differs by design (nvn-design.md, 3c decision 4) |
| `nvnCommandBufferFinalize` | G6 | d823d40 | 29629 | implemented | exec (update 1.1.1, 2026-11-13) |  | no own selftest case: every command buffer case calls it |
| `nvnCommandBufferInitialize` | G6 | d823cf0 | 32971 | implemented | selftest R (update 1.1.1, 2026-11-13), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetMemoryCallback` | G6 | d823d50 | 32971 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetMemoryCallbackData` | G6 | d823d60 | 32971 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetRenderTargets` | G6 | d855da0 | 383398 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetSamplerPool` | G6 | d84c470 | 8919 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest: the pool base registers are not compared (per-side addresses) |
| `nvnCommandBufferSetScissors` | G6 | d84b140 | 616033 | implemented | selftest S (update 1.1.1, 2026-11-11) |  |  |
| `nvnCommandBufferSetTexturePool` | G6 | d84c1f0 | 8919 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest: the pool base registers are not compared (per-side addresses) |
| `nvnCommandBufferSetViewports` | G6 | d84acb0 | 383398 | implemented | selftest S (update 1.1.1, 2026-11-11) |  |  |
| `nvnQueueFenceSync` | G7 | d83bbf0 | 8919 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueuePresentTexture` | G7 | d83ef40 | 8919 | implemented | unit (2026-11-13) test_present_refusal |  | no selftest case: the selftest makes no library window |
| `nvnQueueSubmitCommands` | G7 | d839ff0 | 8919 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowAcquireTexture` | G7 | d83ecb0 | 8920 | implemented | unit (2026-11-13) test_window_acquire_helpers, test_window_sync_cycle_params |  | no selftest case: the selftest makes no library window |
| `nvnBlendStateGetBlendEquation` | G8 | d83ad70 | 52 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateGetBlendFunc` | G8 | d83ad30 | 130 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateGetBlendTarget` | G8 | d83ad20 | 436 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateSetBlendEquation` | G8 | d83ac70 | 834 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetBlendFunc` | G8 | d83ac30 | 834 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetBlendTarget` | G8 | d83ac10 | 834 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetDefaults` | G8 | d83abf0 | 834 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferBuilderGetSize` | G8 | d823b10 | 47967 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferBuilderSetDefaults` | G8 | d823ac0 | 48878 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferBuilderSetDevice` | G8 | d823ab0 | 48878 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferBuilderSetStorage` | G8 | d823ad0 | 48878 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferFinalize` | G8 | d823b70 | 41583 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferGetAddress` | G8 | d823bc0 | 316689 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferGetSize` | G8 | d823c60 | 1055626 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferInitialize` | G8 | d823b20 | 48878 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferMap` | G8 | d823b80 | 2291052 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferSetDebugLabel` | G8 | d82ccc0 | 164 | implemented |  |  | the library stores no label; no check: the host handler does nothing, the buffer case calls it but compares nothing after it |
| `nvnChannelMaskStateGetChannelMask` | G8 | d83ae30 | 25 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnChannelMaskStateSetChannelMask` | G8 | d83ade0 | 841 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnChannelMaskStateSetDefaults` | G8 | d83add0 | 826 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnColorStateGetAlphaTest` | G8 | d83af20 | 9278 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnColorStateGetBlendEnable` | G8 | d83af00 | 83560 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnColorStateGetLogicOp` | G8 | d83af10 | 9278 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnColorStateSetAlphaTest` | G8 | d83aee0 | 78 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnColorStateSetBlendEnable` | G8 | d83ae80 | 857 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnColorStateSetDefaults` | G8 | d83ae70 | 828 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnColorStateSetLogicOp` | G8 | d83aec0 | 4 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnCommandBufferBindBlendState` | G8 | d858670 | 1125187 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindChannelMaskState` | G8 | d847340 | 1028726 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindColorState` | G8 | d8472e0 | 1354349 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindDepthStencilState` | G8 | d8473a0 | 966264 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindMultisampleState` | G8 | d847460 | 830653 | implemented |  |  | selftest: expected no-op, the host replay logs the multisample state only (2026-11-11) |
| `nvnCommandBufferBindPolygonState` | G8 | d847400 | 1114717 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindProgram` | G8 | d8474d0 | 528702 | implemented | unit (2026-11-13) test_bind_program_words |  | no selftest case: needs shader code |
| `nvnCommandBufferBindStorageBuffer` | G8 | d847f70 | 267197 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindTexture` | G8 | d8483b0 | 2397725 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindUniformBuffer` | G8 | d847b70 | 2805883 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindVertexAttribState` | G8 | d855340 | 485209 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest mutation: only this row fails (S, r3d.0x458 stream index, 2026-11-12) |
| `nvnCommandBufferBindVertexBuffer` | G8 | d8479d0 | 1131889 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindVertexStreamState` | G8 | d8554b0 | 485209 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferDrawArrays` | G8 | d849670 | 15896 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferDrawArraysInstanced` | G8 | d849890 | 772558 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferDrawElements` | G8 | d8496e0 | 47293 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferDrawElementsBaseVertex` | G8 | d849770 | 491647 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferDrawElementsInstanced` | G8 | d849930 | 131190 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetAlphaRef` | G8 | d84a790 | 441654 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetBlendColor` | G8 | d84a580 | 1425748 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetDepthBounds` | G8 | d84b0b0 | 640478 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetDepthClamp` | G8 | d84bec0 | 498092 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetDepthRange` | G8 | d84afb0 | 26954 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetDepthRanges` | G8 | d84aea0 | 356444 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetLineWidth` | G8 | d84a680 | 776 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetPointSize` | G8 | d84a600 | 13670 | implemented | unit (nvn_params, nvn_replay) |  | library read: 0x546, then 0 to 0x68B |
| `nvnCommandBufferSetPolygonOffsetClamp` | G8 | d84a6f0 | 498092 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetRasterizerDiscard` | G8 | d84be50 | 525432 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetStencilMask` | G8 | d84a480 | 1571050 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetStencilRef` | G8 | d84a500 | 2446866 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferSetStencilValueMask` | G8 | d84a400 | 1571050 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnDepthStencilStateGetDepthFunc` | G8 | d83b150 | 23879 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateGetDepthTestEnable` | G8 | d83b130 | 66275 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateGetDepthWriteEnable` | G8 | d83b140 | 47691 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateGetStencilFunc` | G8 | d83b170 | 18544 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateGetStencilOp` | G8 | d83b190 | 18544 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateGetStencilTestEnable` | G8 | d83b160 | 14577 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDepthStencilStateSetDefaults` | G8 | d83b000 | 955 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetDepthFunc` | G8 | d83b060 | 955 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetDepthTestEnable` | G8 | d83b020 | 955 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetDepthWriteEnable` | G8 | d83b040 | 955 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetStencilFunc` | G8 | d83b0a0 | 1910 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetStencilOp` | G8 | d83b0e0 | 1910 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnDepthStencilStateSetStencilTestEnable` | G8 | d83b080 | 955 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateGetAlphaToCoverageDither` | G8 | d83b380 | 36535 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetAlphaToCoverageEnable` | G8 | d83b370 | 56749 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetCoverageModulationMode` | G8 | d83b3e0 | 23322 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetCoverageToColorEnable` | G8 | d83b410 | 23322 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetCoverageToColorOutput` | G8 | d83b440 | 23322 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetMultisampleEnable` | G8 | d83b350 | 56749 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetRasterSamples` | G8 | d83b3b0 | 23322 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetSamples` | G8 | d83b360 | 23322 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateSetAlphaToCoverageDither` | G8 | d83b310 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetAlphaToCoverageEnable` | G8 | d83b2f0 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetCoverageModulationMode` | G8 | d83b3c0 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetCoverageToColorEnable` | G8 | d83b3f0 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetCoverageToColorOutput` | G8 | d83b420 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetDefaults` | G8 | d83b2b0 | 818 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetMultisampleEnable` | G8 | d83b2d0 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetRasterSamples` | G8 | d83b390 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetSampleLocationsEnable` | G8 | d83b450 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetSampleLocationsGridEnable` | G8 | d83b500 | 3 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateSetSamples` | G8 | d83b330 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnPolygonStateGetCullFace` | G8 | d83b270 | 9938 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnPolygonStateGetFrontFace` | G8 | d83b280 | 9269 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnPolygonStateGetPolygonMode` | G8 | d83b290 | 9269 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnPolygonStateGetPolygonOffsetEnables` | G8 | d83b2a0 | 9269 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnPolygonStateSetCullFace` | G8 | d83b1f0 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnPolygonStateSetDefaults` | G8 | d83b1e0 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnPolygonStateSetFrontFace` | G8 | d83b210 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnPolygonStateSetPolygonMode` | G8 | d83b230 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnPolygonStateSetPolygonOffsetEnables` | G8 | d83b250 | 818 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnProgramFinalize` | G8 | d836330 | 384 | implemented |  |  | no check: the host removes the program record, no getter shows it; the program case calls it but compares nothing after it |
| `nvnProgramInitialize` | G8 | d8362a0 | 5179 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnProgramSetShadersExt` | G8 | d836da0 | 5179 | implemented | unit (2026-11-13) test_program_set_helpers, test_program_header_stage, test_program_build_*, test_program_xfb_* |  | no selftest case: needs shader code |
| `nvnVertexAttribStateGetFormat` | G8 | d83af80 | 4172578 | implemented | unit (nvn_params) |  | library read: field bits 21-30 and 7-20 |
| `nvnVertexAttribStateGetStreamIndex` | G8 | d83afa0 | 4188266 | implemented | unit (nvn_params) |  | library read: bits 0-4 |
| `nvnVertexAttribStateSetDefaults` | G8 | d83af30 | 2291 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnVertexAttribStateSetFormat` | G8 | d83af40 | 2237 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnVertexAttribStateSetStreamIndex` | G8 | d83af60 | 2237 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnVertexStreamStateGetDivisor` | G8 | d83aff0 | 676554 | implemented |  |  | library read: the second word |
| `nvnVertexStreamStateGetStride` | G8 | d83afe0 | 1444082 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnVertexStreamStateSetDefaults` | G8 | d83afb0 | 1801 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnVertexStreamStateSetDivisor` | G8 | d83afd0 | 1801 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnVertexStreamStateSetStride` | G8 | d83afc0 | 1801 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnCommandBufferBeginTransformFeedback` | G9 | d84b5a0 | 13670 | implemented | unit (nvn_params, nvn_replay) |  | library read: macro 0 with the address |
| `nvnCommandBufferBindImage` | G9 | d8491c0 | 2 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindTransformFeedbackBuffer` | G9 | d84b400 | 41010 | implemented | unit (nvn_params) |  | library read: sent at the bind, not at the draw |
| `nvnCommandBufferClearDepthStencil` | G9 | d84aae0 | 17178 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferClearTexture` | G9 | d857610 | 2290 | implemented | selftest S (update 1.1.1, 2026-11-12), exec (update 1.1.1, 2026-11-13) |  | selftest: the render target address registers (r3d.0x200, 0x201) are not compared |
| `nvnCommandBufferCopyTextureToTexture` | G9 | d82bfd0 | 150 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest: the per-side texture address registers are not compared |
| `nvnCommandBufferDispatchCompute` | G9 | d84da90 | 41793 | implemented |  |  | no check: packs the three group counts into the op, no pure part; the replay of the op: test_dispatch (tests/nvn/nvn_replay.c) |
| `nvnCommandBufferEndTransformFeedback` | G9 | d84b610 | 13670 | implemented | unit (nvn_params, nvn_replay) |  | library read: 0x1D1 off, four reports |
| `nvnCommandBufferReportCounter` | G9 | d856900 | 37762 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferResetCounter` | G9 | d856b40 | 9962 | implemented | unit (nvn_params, nvn_replay) |  | library read: 0x54C from the table at de877bc |
| `nvnDeviceGetImageHandle` | G9 | d82d050 | 2 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceGetTextureHandle` | G9 | d82d0d0 | 2240087 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetBorderColor` | G9 | d83a350 | 168 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetCompare` | G9 | d83a340 | 168 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetDefaults` | G9 | d83a2c0 | 168 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetDevice` | G9 | d83a2b0 | 168 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetLodBias` | G9 | d83a330 | 168 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetLodClamp` | G9 | d83a320 | 168 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetLodSnap` | G9 | d83a400 | 13 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetMaxAnisotropy` | G9 | d83a3e0 | 13 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetMinMagFilter` | G9 | d83a300 | 168 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetReductionFilter` | G9 | d83a3f0 | 168 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetWrapMode` | G9 | d83a310 | 168 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerCompare` | G9 | d83aae0 | 2362 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerFinalize` | G9 | d83a5b0 | 30 | implemented |  |  | library read: one ret, the record stays |
| `nvnSamplerInitialize` | G9 | d83a570 | 168 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolRegisterSampler` | G9 | d83a910 | 82 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerSetDebugLabel` | G9 | d82cd20 | 85 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnTextureFlushTexels` | G9 | d83e290 | 1 | implemented |  |  | no check: a logged no-op (the renderer reads the same host memory); the texture case calls it but compares nothing after it |
| `nvnTexturePoolRegisterImage` | G9 | d83dae0 | 1 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTexturePoolRegisterTexture` | G9 | d83d950 | 6443 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewGetFormat` | G9 | d83c8f0 | 6 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureViewGetLayers` | G9 | d83c8d0 | 2335 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureViewGetLevels` | G9 | d83c8b0 | 2335 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureViewGetTarget` | G9 | d83c960 | 2290 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureViewSetDefaults` | G9 | d83c800 | 5852 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetDepthStencilMode` | G9 | d83c890 | 5598 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetFormat` | G9 | d83c850 | 5828 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetLayers` | G9 | d83c830 | 5827 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetLevels` | G9 | d83c810 | 5852 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetSwizzle` | G9 | d83c870 | 5598 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewSetTarget` | G9 | d83c980 | 5828 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureWriteTexels` | G9 | d83e020 | 1 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateGetAdvancedMode` | G10 | d83ad90 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateGetAdvancedNormalizedDst` | G10 | d83adc0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateGetAdvancedOverlap` | G10 | d83ada0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateGetAdvancedPremultipliedSrc` | G10 | d83adb0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnBlendStateSetAdvancedMode` | G10 | d83aca0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetAdvancedNormalizedDst` | G10 | d83ad00 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetAdvancedOverlap` | G10 | d83acc0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBlendStateSetAdvancedPremultipliedSrc` | G10 | d83ace0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferBuilderGetDevice` | G10 | d823ae0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferBuilderGetMemoryOffset` | G10 | d823b00 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferBuilderGetMemoryPool` | G10 | d823af0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferFlushMappedRange` | G10 | d823c00 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnBufferGetDebugID` | G10 | d823c70 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferGetMemoryOffset` | G10 | d823c50 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferGetMemoryPool` | G10 | d823c40 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnBufferInvalidateMappedRange` | G10 | d823c20 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferBindCoverageModulationTable` | G10 | d84cd20 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | no-op for table 0 (disable), refused for a table: identity not verified |
| `nvnCommandBufferBindImages` | G10 | d849430 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindSeparateSampler` | G10 | d848ad0 | 0 | implemented |  |  | refused: the host has only the combined texture table |
| `nvnCommandBufferBindSeparateSamplers` | G10 | d848d40 | 0 | implemented |  |  | refused: the host has only the combined texture table |
| `nvnCommandBufferBindSeparateTexture` | G10 | d848620 | 0 | implemented |  |  | refused: the host has only the combined texture table |
| `nvnCommandBufferBindSeparateTextures` | G10 | d848890 | 0 | implemented |  |  | refused: the host has only the combined texture table |
| `nvnCommandBufferBindStorageBuffers` | G10 | d8481d0 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindTextures` | G10 | d848f80 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | one combined texture bind per entry (stride 8) |
| `nvnCommandBufferBindTransformFeedbackBuffers` | G10 | d84b4d0 | 0 | implemented | unit (nvn_params) |  | library read: sent at the bind, not at the draw |
| `nvnCommandBufferBindUniformBuffers` | G10 | d847e00 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferBindVertexBuffers` | G10 | d847a80 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferClearBuffer` | G10 | d84cee0 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferClearColori` | G10 | d84a940 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferClearColorui` | G10 | d84aa10 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferClearTexturei` | G10 | d857a10 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest: the render target address registers (r3d.0x200, 0x201) are not compared |
| `nvnCommandBufferClearTextureui` | G10 | d857e10 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | selftest: the render target address registers (r3d.0x200, 0x201) are not compared |
| `nvnCommandBufferCopyBufferToBuffer` | G10 | d82be80 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferCopyBufferToTexture` | G10 | d82b510 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  | selftest: the per-side texture address registers are not compared; selftest mutation: the host region is one texel narrower, only this function's rows fail (S, rdma.0x104 and r2d.0x091, 2026-11-12) |
| `nvnCommandBufferCopyCommands` | G10 | d825a30 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferCopyTextureToBuffer` | G10 | d82bb70 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12), exec (update 1.1.1, 2026-11-13) |  | selftest: the per-side texture address registers are not compared; selftest mutation: the host region is one texel narrower, only this function's rows fail (S, rdma.0x105, 2026-11-12) |
| `nvnCommandBufferDiscardColor` | G10 | d8564c0 | 0 | implemented |  |  | selftest: expected no-op, the host handler is a logged no-op by design (2026-11-12) |
| `nvnCommandBufferDiscardDepthStencil` | G10 | d856530 | 0 | implemented |  |  | selftest: expected no-op, the host handler is a logged no-op by design (2026-11-12) |
| `nvnCommandBufferDispatchComputeIndirect` | G10 | d84dc60 | 0 | implemented |  |  | refused: no host path for a QMD at a GPU address |
| `nvnCommandBufferDownsample` | G10 | d82cb10 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  | selftest: the per-side texture address registers are not compared |
| `nvnCommandBufferDownsampleTextureView` | G10 | d82cb20 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  | selftest: the per-side texture address registers are not compared |
| `nvnCommandBufferDrawArraysIndirect` | G10 | d849a00 | 0 | implemented |  |  | refused: the GPU reads the record at draw time, the replay has no fetch |
| `nvnCommandBufferDrawElementsIndirect` | G10 | d849bd0 | 0 | implemented |  |  | refused: the GPU reads the record at draw time, the replay has no fetch |
| `nvnCommandBufferDrawTexture` | G10 | d84cad0 | 0 | implemented |  |  | refused: needs the driver program at device+0x1b00 |
| `nvnCommandBufferDrawTransformFeedback` | G10 | d84bb40 | 0 | implemented |  |  | refused: the renderer does not write the byte counts |
| `nvnCommandBufferFenceSync` | G10 | d825a80 | 0 | implemented | unit, exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferGetCommandMemoryFree` | G10 | d8240a0 | 0 | implemented |  |  | refused, returns 0: the host does not write the game's command memory; selftest: expected refusal, the library gives the real value and the host 0 (2026-11-13) |
| `nvnCommandBufferGetCommandMemorySize` | G10 | d824080 | 0 | implemented |  |  | refused, returns 0: the host does not write the game's command memory; selftest: expected refusal, the library gives the real value and the host 0 (2026-11-13) |
| `nvnCommandBufferGetCommandMemoryUsed` | G10 | d824090 | 0 | implemented |  |  | refused, returns 0: the host does not write the game's command memory; selftest: expected refusal, the library gives the real value and the host 0 (2026-11-13) |
| `nvnCommandBufferGetControlMemoryFree` | G10 | d8240e0 | 0 | implemented |  |  | refused, returns 0: the host does not write the game's command memory; selftest: expected refusal, the library gives the real value and the host 0 (2026-11-13) |
| `nvnCommandBufferGetControlMemorySize` | G10 | d8240b0 | 0 | implemented | unit (nvn_params) |  | chunk size, 0x10 before the first Add |
| `nvnCommandBufferGetControlMemoryUsed` | G10 | d8240d0 | 0 | implemented |  |  | refused, returns 0: the host does not write the game's command memory; selftest: expected refusal, the library gives the real value and the host 0 (2026-11-13) |
| `nvnCommandBufferGetCopyImageStride` | G10 | d82cb60 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferGetCopyRowStride` | G10 | d82cb50 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferGetMemoryCallback` | G10 | d823d70 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  | host record value |
| `nvnCommandBufferGetMemoryCallbackData` | G10 | d823d80 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  | host record value |
| `nvnCommandBufferInsertCheckpointDynamic` | G10 | d823c90 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  | returns 1, as the library body |
| `nvnCommandBufferInsertCheckpointStatic` | G10 | d823c80 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  | returns 1, as the library body |
| `nvnCommandBufferInsertDebugMarker` | G10 | d84d190 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferInsertDebugMarkerDynamic` | G10 | d84d3b0 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferInsertDebugMarkerStatic` | G10 | d84d200 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferIsRecording` | G10 | d826400 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  | host record value |
| `nvnCommandBufferMultiDrawArraysIndirectCount` | G10 | d849dd0 | 0 | implemented |  |  | refused: the record feed of the macro is not known |
| `nvnCommandBufferMultiDrawElementsIndirectCount` | G10 | d84a230 | 0 | implemented |  |  | refused: the record feed of the macro is not known |
| `nvnCommandBufferPauseTransformFeedback` | G10 | d84b6f0 | 0 | implemented | unit (nvn_params) |  | library read: End words, address 0 sends only the disable |
| `nvnCommandBufferPopDebugGroup` | G10 | d84cfc0 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferPopDebugGroupId` | G10 | d84d030 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferPushDebugGroup` | G10 | d84d0a0 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferPushDebugGroupDynamic` | G10 | d84d280 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferPushDebugGroupStatic` | G10 | d84d110 | 0 | implemented | selftest S (update 1.1.1, 2026-11-13) |  | no-op: debug marker for an external debugger; selftest: the library macro call changes no compared register (2026-11-13) |
| `nvnCommandBufferReportValue` | G10 | d856ab0 | 0 | implemented | unit, exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferResolveDepthBuffer` | G10 | d84ce80 | 0 | implemented |  |  | selftest: expected no-op, the host handler is a logged no-op by design (2026-11-12) |
| `nvnCommandBufferRestoreZCullData` | G10 | d84c8e0 | 0 | implemented |  |  | logged no-op: the host has no ZCull; selftest: expected no-op, the library writes r3d 0x1FA..0x1FD (2026-11-13) |
| `nvnCommandBufferResumeTransformFeedback` | G10 | d84b810 | 0 | implemented | unit (nvn_params) |  | library read: address 0 sends the enable; an address is refused |
| `nvnCommandBufferSaveZCullData` | G10 | d84c840 | 0 | implemented |  |  | logged no-op: the host has no ZCull; selftest: expected no-op, the library writes r3d 0x1FA..0x1FD (2026-11-13) |
| `nvnCommandBufferSetColorReductionEnable` | G10 | d84d4e0 | 0 | implemented |  |  | logged no-op: full color precision is kept; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetColorReductionThresholds` | G10 | d84d550 | 0 | implemented |  |  | logged no-op: full color precision is kept; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetCommandMemoryCallbackEnabled` | G10 | d823d90 | 0 | implemented |  |  | no-op: the host never calls the memory callback; no selftest case: no getter reads the flag |
| `nvnCommandBufferSetConservativeRasterDilate` | G10 | d84c6c0 | 0 | implemented | unit (nvn_params) |  | logged no-op for level 0, else refused |
| `nvnCommandBufferSetConservativeRasterEnable` | G10 | d84c630 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | logged no-op for disable, enable is refused |
| `nvnCommandBufferSetCopyImageStride` | G10 | d82cb40 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetCopyRowStride` | G10 | d82cb30 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetDebugLabel` | G10 | d82ccd0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferSetInnerTessellationLevels` | G10 | d84b310 | 0 | implemented | unit (nvn_params) |  | library read: sent as draw state at the next draw |
| `nvnCommandBufferSetOuterTessellationLevels` | G10 | d84b380 | 0 | implemented | unit (nvn_params) |  | library read: sent as draw state at the next draw |
| `nvnCommandBufferSetPatchSize` | G10 | d84b2b0 | 0 | implemented | unit (nvn_params) |  | library read: sent as draw state at the next draw, no mask |
| `nvnCommandBufferSetPrimitiveRestart` | G10 | d84a370 | 0 | implemented | unit (nvn_params) |  | library read: sent as draw state at the next draw |
| `nvnCommandBufferSetProgramSubroutines` | G10 | d826180 | 0 | implemented |  |  | refused: the words of the subroutine rewrite are not known |
| `nvnCommandBufferSetRenderEnable` | G10 | d856bb0 | 0 | implemented | unit (nvn_params) |  | library read: one draw state with the conditional form (both write 0x556) |
| `nvnCommandBufferSetRenderEnableConditional` | G10 | d856c20 | 0 | implemented | unit (nvn_params) |  | library read: condition 0 or 1, another value is a logged no-op |
| `nvnCommandBufferSetSampleMask` | G10 | d84a800 | 0 | implemented |  |  | logged no-op: the host renders one sample; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetScissor` | G10 | d84b280 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | library read: the SetScissors op with index 0 |
| `nvnCommandBufferSetShaderScratchMemory` | G10 | d825f10 | 0 | implemented |  |  | refused: two SDK constants are not known, no host scratch state |
| `nvnCommandBufferSetStencilCullCriteria` | G10 | d84d600 | 0 | implemented |  |  | logged no-op: the host has no stencil cull; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetSubpixelPrecisionBias` | G10 | d84c770 | 0 | implemented |  |  | logged no-op: the device subpixel grid is used; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetTiledCacheAction` | G10 | d84bf30 | 0 | implemented |  |  | no-op: the host has no tiled cache; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetTiledCacheTileSize` | G10 | d84c000 | 0 | implemented |  |  | no-op: the host has no tiled cache; selftest: expected no-op, the host handler is a logged no-op by design (2026-11-11) |
| `nvnCommandBufferSetViewport` | G10 | d84ae60 | 0 | implemented | selftest S (update 1.1.1, 2026-11-12) |  | library read: the SetViewports op with index 0 |
| `nvnCommandBufferSetViewportSwizzles` | G10 | d84afe0 | 0 | implemented | unit (nvn_params) |  | logged no-op for identity entries, else refused |
| `nvnCommandBufferSignalEvent` | G10 | d832ef0 | 0 | implemented | unit, exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnCommandBufferTiledDownsample` | G10 | d8575f0 | 0 | implemented | unit |  | selftest: skipped, the library recording has record op 21, which the decoder does not read (2026-11-12) |
| `nvnCommandBufferTiledDownsampleTextureView` | G10 | d857600 | 0 | implemented | unit |  | selftest: skipped, the library recording has record op 21, which the decoder does not read (2026-11-12) |
| `nvnCommandBufferUpdateUniformBuffer` | G10 | d84c980 | 0 | implemented | unit, selftest S (update 1.1.1, 2026-11-12) |  |  |
| `nvnCommandBufferWaitEvent` | G10 | d832e30 | 0 | implemented | unit |  |  |
| `nvnCommandBufferWaitSync` | G10 | d825ce0 | 0 | implemented | unit |  |  |
| `nvnDeviceApplyDeferredFinalizes` | G10 | d82cdd0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceBuilderGetFlags` | G10 | d82cde0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnDeviceFinalize` | G10 | d82ce10 | 0 | implemented |  |  | refused, logged: the host keeps the records to the process end; no check: no pure part |
| `nvnDeviceFinalizeCommandHandle` | G10 | d826410 | 0 | implemented |  |  | the library body is one ret (sdk d826410); no check: the host handler is the same ret |
| `nvnDeviceGenerateDebugDomainId` | G10 | d82cfd0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  | one process counter, returns the old value |
| `nvnDeviceGetCurrentTimestampInNanoseconds` | G10 | d82cff0 | 0 | implemented | unit (nvn_params, timestamp), selftest R (update 1.1.1, 2026-11-10) |  | nvn_timestamp_ns(gpu_ticks()) |
| `nvnDeviceGetDepthMode` | G10 | d82d040 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceGetProcAddress` | G10 | d82d090 | 0 | implemented | unit (`nvn_proc_name_index`, `nvn_proc_table_check`) |  | the host answers from the library's name and address tables (sdk d832940 is the library lookup, which no longer runs); `nvnBootstrapLoader` gets the same answer |
| `nvnDeviceGetSeparateSamplerHandle` | G10 | d82d0a0 | 0 | implemented | unit (nvn_params), selftest R (update 1.1.1, 2026-11-10) |  | (int64)(int32)index << 20, no tag |
| `nvnDeviceGetSeparateTextureHandle` | G10 | d82d0b0 | 0 | implemented | unit (nvn_params), selftest R (update 1.1.1, 2026-11-10) |  | nvn_texture_handle(index, 0) |
| `nvnDeviceGetTexelFetchHandle` | G10 | d82d0c0 | 0 | implemented | unit (nvn_params), selftest R (update 1.1.1, 2026-11-10) |  | nvn_texture_handle(index, 0) |
| `nvnDeviceGetWindowOriginMode` | G10 | d82d100 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnDeviceInstallDebugCallback` | G10 | d82ccb0 | 0 | implemented |  |  | the library body is one ret |
| `nvnDeviceIsExternalDebuggerAttached` | G10 | d82d120 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  | returns 0, as the library |
| `nvnDeviceRegisterFastClearColor` | G10 | d82d130 | 0 | implemented | unit (nvn_params) |  | no-op returning 1, 0 for a format outside the library range |
| `nvnDeviceRegisterFastClearColori` | G10 | d82d140 | 0 | implemented | unit (nvn_params) |  | no-op returning 1, 0 for a format outside the library range |
| `nvnDeviceRegisterFastClearColorui` | G10 | d82d150 | 0 | implemented | unit (nvn_params) |  | no-op returning 1, 0 for a format outside the library range |
| `nvnDeviceRegisterFastClearDepth` | G10 | d82d160 | 0 | implemented |  |  | no-op returning 1: the host clear reads its value |
| `nvnDeviceSetAutomaticCheckpointsByDebugGroup` | G10 | d823cc0 | 0 | implemented |  |  | returns 1, as the library body |
| `nvnDeviceSetAutomaticCheckpointsEnable` | G10 | d823ca0 | 0 | implemented |  |  | returns 1, as the library body |
| `nvnDeviceSetAutomaticCheckpointsSamplingInterval` | G10 | d823cb0 | 0 | implemented |  |  | returns 1, as the library body |
| `nvnDeviceSetCheckpointCallStacksEnable` | G10 | d823cd0 | 0 | implemented |  |  | returns 1, as the library body |
| `nvnDeviceSetCheckpointMaxCallStacksDepth` | G10 | d823ce0 | 0 | implemented |  |  | returns 1, as the library body |
| `nvnDeviceSetDebugLabel` | G10 | d82cce0 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret |
| `nvnDeviceSetIntermediateShaderCache` | G10 | d82d180 | 0 | implemented |  |  | the library body is one ret |
| `nvnDeviceWalkDebugDatabase` | G10 | d82d1a0 | 0 | implemented |  |  | the library body is one ret |
| `nvnEventBuilderGetMemoryOffset` | G10 | d832d30 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnEventBuilderGetMemoryPool` | G10 | d832d20 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnEventBuilderGetStorage` | G10 | d832d10 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnEventBuilderSetDefaults` | G10 | d832cf0 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventBuilderSetStorage` | G10 | d832d00 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10), exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventFinalize` | G10 | d832dd0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventGetMemoryOffset` | G10 | d832e00 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventGetMemoryPool` | G10 | d832df0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventGetValue` | G10 | d832de0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventInitialize` | G10 | d832d40 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnEventSignal` | G10 | d832e10 | 0 | implemented | unit, exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolBuilderGetDevice` | G10 | d833ca0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolBuilderGetFlags` | G10 | d833cd0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolBuilderGetMemory` | G10 | d833cb0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolBuilderGetSize` | G10 | d833cc0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolFlushMappedRange` | G10 | d833ee0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolGetSize` | G10 | d833f80 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMemoryPoolInvalidateMappedRange` | G10 | d833ef0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnMemoryPoolMapVirtual` | G10 | d833f10 | 0 | implemented | unit, selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnMultisampleStateGetSampleLocationsEnable` | G10 | d83b470 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetSampleLocationsGrid` | G10 | d83b480 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateGetSampleLocationsGridEnable` | G10 | d83b520 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnMultisampleStateSetSampleLocations` | G10 | d83b530 | 0 | implemented | unit (2026-11-13) test_multisample_locations_fit, test_u1_multisample_locations |  |  |
| `nvnProgramSetDebugLabel` | G10 | d82cd00 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnProgramSetSampleShading` | G10 | d837180 | 0 | implemented |  |  | no-op: the host renders one sample |
| `nvnProgramSetShaders` | G10 | d836760 | 0 | implemented | unit (2026-11-13) test_program_set_helpers, test_program_header_stage, test_program_build_*, test_program_xfb_* |  | SetShadersExt path with 16-byte entries, no control |
| `nvnProgramSetSubroutineLinkage` | G10 | d836e90 | 0 | implemented |  |  | returns 0: the host has no subroutine table |
| `nvnQueueAcquireTexture` | G10 | d83eed0 | 0 | implemented |  |  | shares the pick of nvnWindowAcquireTexture, no sync |
| `nvnQueueBuilderGetCommandFlushThreshold` | G10 | d838540 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetCommandMemorySize` | G10 | d838510 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetComputeMemorySize` | G10 | d838520 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetControlMemorySize` | G10 | d838530 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetDevice` | G10 | d8384f0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetFlags` | G10 | d838500 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetMemory` | G10 | d838560 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetMemorySize` | G10 | d838550 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderGetQueuePriority` | G10 | d8385a0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueBuilderSetCommandFlushThreshold` | G10 | d838180 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-10) |  |  |
| `nvnQueueFinalize` | G10 | d8393d0 | 0 | implemented |  |  | refused, logged: the host keeps the records to the process end; no check: no pure part |
| `nvnQueueFinish` | G10 | d839480 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  | exec covers only an idle queue: E6 calls it after a bounded wait, since its own wait has no limit and no watchdog |
| `nvnQueueFlush` | G10 | d83a070 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnQueueGetError` | G10 | d839560 | 0 | implemented |  |  | returns 0: the host has no channel error notification |
| `nvnQueueGetTotalCommandMemoryUsed` | G10 | d83a080 | 0 | implemented |  |  | refused, returns 0: the host has no usage count |
| `nvnQueueGetTotalComputeMemoryUsed` | G10 | d83a0e0 | 0 | implemented |  |  | refused, returns 0: the host has no usage count |
| `nvnQueueGetTotalControlMemoryUsed` | G10 | d83a0a0 | 0 | implemented |  |  | refused, returns 0: the host has no usage count |
| `nvnQueueResetMemoryUsageCounts` | G10 | d83a0f0 | 0 | implemented |  |  | no-op: the host has no usage counters |
| `nvnQueueSetDebugLabel` | G10 | d82cd10 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret |
| `nvnQueueWaitSync` | G10 | d83bfa0 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetBorderColor` | G10 | d83a4b0 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetBorderColori` | G10 | d83a4e0 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetBorderColorui` | G10 | d83a510 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetCompare` | G10 | d83a490 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetDevice` | G10 | d83a410 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetLodBias` | G10 | d83a480 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetLodClamp` | G10 | d83a460 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetLodSnap` | G10 | d83a560 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetMaxAnisotropy` | G10 | d83a540 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetMinMagFilter` | G10 | d83a420 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetReductionFilter` | G10 | d83a550 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderGetWrapMode` | G10 | d83a440 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnSamplerBuilderSetBorderColori` | G10 | d83a380 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerBuilderSetBorderColorui` | G10 | d83a3b0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetBorderColor` | G10 | d83a650 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetBorderColori` | G10 | d83a680 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetBorderColorui` | G10 | d83a6b0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetCompare` | G10 | d83a630 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetDebugID` | G10 | d83abe0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetLodBias` | G10 | d83a620 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetLodClamp` | G10 | d83a600 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetMaxAnisotropy` | G10 | d83a6e0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetMinMagFilter` | G10 | d83a5c0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetReductionFilter` | G10 | d83a6f0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerGetWrapMode` | G10 | d83a5e0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolFinalize` | G10 | d83a9d0 | 0 | implemented |  |  | the library body is one ret |
| `nvnSamplerPoolGetMemoryOffset` | G10 | d83aac0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolGetMemoryPool` | G10 | d83aab0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolRegisterSamplerBuilder` | G10 | d83a9e0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnSamplerPoolSetDebugLabel` | G10 | d82cd70 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnSyncCreateGLSync` | G10 | d83c0a0 | 0 | implemented |  |  | fails with the library result for no backend (4-u2); no check: a constant result, no GL backend |
| `nvnSyncFinalize` | G10 | d83b860 | 0 | implemented | exec (update 1.1.1, 2026-11-13) |  |  |
| `nvnSyncInitializeFromFencedGLSync` | G10 | d83bfb0 | 0 | implemented |  |  | fails with the library result for no backend (4-u2); no check: a constant result, no GL backend |
| `nvnSyncSetDebugLabel` | G10 | d82cd30 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret |
| `nvnTextureBuilderGetDepth` | G10 | d83c3b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetDepthStencilMode` | G10 | d83c420 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetDevice` | G10 | d83c360 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetFlags` | G10 | d83c370 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetFormat` | G10 | d83c3d0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetGLTextureName` | G10 | d83c720 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetLevels` | G10 | d83c3c0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetMemoryOffset` | G10 | d83c4b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetMemoryPool` | G10 | d83c4a0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetPackagedTextureData` | G10 | d83c430 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderGetPackagedTextureLayout` | G10 | d83c440 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetRawStorageClass` | G10 | d83c740 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetSamples` | G10 | d83c3e0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetSparseTileLayout` | G10 | d83dca0 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetStorageClass` | G10 | d83c620 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetStride` | G10 | d83c490 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetSwizzle` | G10 | d83c3f0 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetTarget` | G10 | d83c380 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderGetZCullStorageSize` | G10 | d83d760 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureBuilderSetGLTextureName` | G10 | d83c340 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetPackagedTextureData` | G10 | d83c730 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetSamples` | G10 | d83c300 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureBuilderSetSize1D` | G10 | d83c2b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureCompare` | G10 | d83e3a0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetDebugID` | G10 | d83e500 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetDepthStencilMode` | G10 | d83d900 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetFlags` | G10 | d83d850 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetFormat` | G10 | d83d8b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetLevels` | G10 | d83d8a0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetMemoryOffset` | G10 | d83e380 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetMemoryPool` | G10 | d83e370 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetRawStorageClass` | G10 | d83e510 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetSamples` | G10 | d83d8c0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetSparseTileLayout` | G10 | d83dd40 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetStorageClass` | G10 | d83d800 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetStorageSize` | G10 | d83e390 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetSwizzle` | G10 | d83d8d0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetTextureAddress` | G10 | d83d920 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetViewOffset` | G10 | d83d810 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureGetZCullStorageSize` | G10 | d83d7b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureInvalidateTexels` | G10 | d83e300 | 0 | implemented |  |  | no check: a logged no-op (the renderer reads the same host memory); the texture case calls it but compares nothing after it |
| `nvnTexturePoolFinalize` | G10 | d83d940 | 0 | implemented |  |  | the library body is one ret |
| `nvnTexturePoolGetMemoryOffset` | G10 | d83dc80 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTexturePoolGetMemoryPool` | G10 | d83dc70 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTexturePoolSetDebugLabel` | G10 | d82cd60 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnTextureReadTexels` | G10 | d83e280 | 0 | implemented | unit, selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureReadTexelsStrided` | G10 | d83e030 | 0 | implemented | unit, selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureSetDebugLabel` | G10 | d82cd40 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret, the selftest calls it but compares nothing after it |
| `nvnTextureViewCompare` | G10 | d83c9a0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnTextureViewGetDepthStencilMode` | G10 | d83c940 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureViewGetSwizzle` | G10 | d83c910 | 0 | implemented | selftest R/M (update 1.1.1, 2026-11-13) |  |  |
| `nvnTextureWriteTexelsStrided` | G10 | d83ddd0 | 0 | implemented | selftest M (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowBuilderGetDevice` | G10 | d83e5d0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderGetNativeWindow` | G10 | d83e630 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderGetNumActiveTextures` | G10 | d83e620 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderGetNumTextures` | G10 | d83e5e0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderGetPresentInterval` | G10 | d83e610 | 0 | implemented | selftest R (update 1.1.1, 2026-11-13) |  |  |
| `nvnWindowBuilderGetTexture` | G10 | d83e5f0 | 0 | implemented | unit (2026-11-13) test_window_texture_slot |  | no selftest case: the selftest makes no library window |
| `nvnWindowBuilderSetNumActiveTextures` | G10 | d83e5c0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowBuilderSetPresentInterval` | G10 | d83e5b0 | 0 | implemented | selftest R (update 1.1.1, 2026-11-10) |  |  |
| `nvnWindowFinalize` | G10 | d83ea90 | 0 | implemented |  |  | removes the host window record |
| `nvnWindowGetCrop` | G10 | d83eba0 | 0 | implemented |  |  | no check: four direct stores of the host record crop, no pure part; the crop value: test_window_params |
| `nvnWindowGetNativeWindow` | G10 | d83eb10 | 0 | implemented |  |  | no check: a direct read of a host record field, no pure part; the selftest makes no library window |
| `nvnWindowGetNumActiveTextures` | G10 | d83ebe0 | 0 | implemented |  |  | no check: a direct read of a host record field, no pure part; the selftest makes no library window |
| `nvnWindowGetNumTextures` | G10 | d83ebd0 | 0 | implemented |  |  | no check: a direct read of a host record field, no pure part; the selftest makes no library window |
| `nvnWindowGetPresentInterval` | G10 | d83eb20 | 0 | implemented |  |  | no check: a direct read of a host record field, no pure part; the selftest makes no library window |
| `nvnWindowSetDebugLabel` | G10 | d82cd50 | 0 | implemented |  |  | the library body is one ret; no check: the host handler is the same ret |
| `nvnWindowSetNumActiveTextures` | G10 | d83ea30 | 0 | implemented | unit (2026-11-13) test_u1_window_fields |  |  |
