<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Patches for ScarletNX and the game

The patches change `third_party/ScarletNX` or the game code. The submodule pin does not change. Apply a patch to the
working tree of the submodule before the build, and keep the patch file as the source of truth.

| Patch | What it does |
| --- | --- |
| `0001-native-mouse-input.patch` | Native mouse input. The mouse is captured and drives the right stick. The left button is ZR and the right button is ZL. A controller input makes the controller the active input. Hold Alt to free the cursor. New settings `mouse_*` (look, sensitivity, vertical sensitivity, invert Y, dead zone offset, curve, overdrive). See `docs/input-analysis.md` and `docs/camera-analysis.md`. |
| `0002-global-red-add-u64.patch` | Shader translation of `RED.E.ADD.64` (a 64-bit add into global memory, size 2 of `RED`). The translator refused it, so one draw in each frame was skipped. The module declares `Int64Atomics`, each global buffer gets a 64-bit view on the same binding (decoration `Aliased`), and the device enables `shaderBufferInt64Atomics`. A device without the feature refuses the program as before. A regression test with a `spirv-val` check is in `tests/shader_direct_regressions.c`. See `docs/shader-analysis.md`. |
| `0003-stick-trace.patch` | The stick trace, a diagnostic for the camera analysis. Apply it after `0001`. With `trace_stick=1` in the `.ini`, the right stick is held at a known value after `trace_stick_start` seconds, the guest memory is searched for the floats that the game makes of it, and the code that reads or writes them is logged for `trace_stick_seconds`, with the call chain of the first reads (Windows only). It is not applied now, because the camera search is paused. See `docs/camera-analysis.md`. |
| `camera-unclamp.pchtxt` | A patch of the game code (IPSwitch text). It sets the clamp limits of the input manager to 8.0 and -8.0. A test showed that the camera does not use that clamp, so it has no effect on the camera. It stays as a record. Remove the mod when the camera patch exists. |

Apply the ScarletNX patch, from `third_party/ScarletNX`:

```sh
git apply ../../recomp/patches/0001-native-mouse-input.patch
git apply ../../recomp/patches/0002-global-red-add-u64.patch
```

To go on with the camera search, apply `0003-stick-trace.patch` the same way, after `0001`.

Then build as in `docs/SCARLETNX_BUILD-NOTES.md` and copy `build/default/scarletnx.exe` over the three copies of the
runtime (`game/scarletnx/bin/scarletnx.exe` and the two game programs in `game/recompiled/<id>/`).

To use `camera-unclamp.pchtxt`, put it in `game/recompiled/<id>/<id>_data/mods/camera-unclamp/exefs/` and recompile.
