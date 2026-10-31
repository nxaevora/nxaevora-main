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
| `0004-hitch-dump.patch` | A diagnostic for the stop of about 1.3 s at the start. The new setting `hitch_dump` (milliseconds, 0 = off) writes the game's threads, each with its state, what it waits for and its call chain, to `hitch-N.txt` when the frame count stands still for that time, and again at twice the time (six files at most in a run). Two snapshots of one stop show the wait that holds the game. The file goes to the game's data folder, next to `threads.txt`. See `docs/stutter-analysis.md`. |
| `0005-slow-romfs-read.patch` | With `hitch_dump` on, a RomFS read through the storage service that takes 50 ms or more is logged (`fs: RomFS read of N bytes at OFFSET took M ms`). The offset is the place in the RomFS image, which names the file. Apply it after `0004`. The test `snx-test-fs-service` gets a stub of `plat_ns`. See `docs/stutter-analysis.md`. |
| `0006-stop-dump-sleep-and-gpu.patch` | Makes the `hitch-N.txt` of `0004` fuller. A thread that slept at least once since the last dump gets the registers of its last `svcSleepThread` and what they point to (a dump takes this only from 100 sleeps otherwise), and the file ends with the GPU thread's state and the host threads that wait for a lock. Apply it after `0004`. See `docs/stutter-analysis.md`. |
| `0007-stop-dump-object-chain.patch` | In the stop's dump of `0006`, the callee-saved registers of the sleeping thread also show what they point at, then what its first pointer points at, then the third level with the names of its twelve first functions (a pointer to an object, the object, its virtual function table). Apply it after `0006`. See `docs/stutter-analysis.md`. |
| `0008-sleep-time-in-dump.patch` | The dump of a thread that sleeps (`0006`) adds how much `svcSleepThread` time the thread asked for since the last dump and how long its sleeps took, in all and for one call. A sleep that takes much longer than asked shows here. Apply it after `0006`. See `docs/stutter-analysis.md`. |
| `0009-keyboard-remap.patch` | The keyboard map as a setting. The new setting `keyboard_map` (section Keyboard) lists `Button:Key` words, for example `ZL:Q A:F LStickUp:Up`. A button that the text names loses its default keys. A key that the text names belongs only to the buttons named with it. `None` frees a button. A key name is the SDL name, with `_` for a space. Escape, F1 to F12, Pause and Alt are reserved. A word that cannot be read is logged and the map stays. Apply it after `0001`. See `docs/input-analysis.md`. |
| `0010-ini-store-256.patch` | The store of the game's `.ini` held 128 keys, and the file already had 129 (the log said `more than 128 keys, renderdoc_frames and those after it are left out`). A key after the 128th was dropped at the read and so erased at the next write of the file. A key that the user added, such as `keyboard_map`, went this way. The limit is now 256. |
| `0011-angle-search.patch` | The angle search, a diagnostic for the camera analysis. Apply it after `0003`. With `trace_stick=2` the right stick is held in steps after `trace_stick_start` seconds (rest, hold, hold, rest, opposite hold). The floats of the writable guest memory that change in the hold at a steady rate, do not change at rest and change the other way in the opposite hold are the candidates for the camera angle. Their words are then watched as in `0003`, and the game functions that write them are logged. It uses about 1 GB of memory for a short time. It is not applied in the normal build. See `docs/camera-analysis.md`. |
| `0012-native-mouse-guest-words.patch` | The runtime side of the native mouse look. The setting `mouse_native_address` (0 = off) is the guest address of a 64 bit word (two floats: yaw in radians, pitch in units of the camera arc). The mouse motion is added to it as it arrives, in place of the stick, and the game's code patch `native-mouse-look.pchtxt` takes it. Settings `mouse_native_yaw` (1400) and `mouse_native_pitch` (1000), in millionths for each mouse count, and `mouse_invert_x` and `mouse_invert_y` turn the directions. At most 0.5 of each waits for the game. See `docs/camera-analysis.md`. |
| `0013-imgui-dependency.patch` | Adds Dear ImGui 1.93.0 (MIT) as a pinned CPM package (`cmake/dependencies.cmake`, SHA-256 of the release archive). It only downloads the sources. Nothing is compiled or linked yet. The in-game windows are planned in `docs/imgui-plan.md`. |
| `native-mouse-look.pchtxt` | A patch of the game code (IPSwitch text, update 1.1.1). The camera's angle setter 0x99437c0 takes the mouse word at 0x9943980 and adds it to the yaw and to the pitch arc position. The code and the word sit in the words that the setter's mode 1 path frees (it jumps into the end of the mode 0 path). Put it in `game/recompiled/<id>/<id>_data/mods/native-mouse-look/exefs/` and recompile. Set `mouse_native_address=160708992` in the game's `.ini`. |
| `camera-unclamp.pchtxt` | A patch of the game code (IPSwitch text). It sets the clamp limits of the input manager to 8.0 and -8.0. A test showed that the camera does not use that clamp, so it has no effect on the camera. It stays as a record. Remove the mod when the camera patch exists. |

The patches `0003` to `0008` are diagnostic tools and are not applied: `0003` is the stick trace, `0004` to `0008` find the
wait of a stop of the game (`hitch_dump`; apply them in the order of their numbers, `0004` first). The runtime of this project is
built with `0001`, `0002`, `0009`, `0010`, `0012` and `0013` only.

Apply the ScarletNX patch, from `third_party/ScarletNX`:

```sh
git apply ../../recomp/patches/0001-native-mouse-input.patch
git apply ../../recomp/patches/0002-global-red-add-u64.patch
git apply ../../recomp/patches/0009-keyboard-remap.patch
git apply ../../recomp/patches/0010-ini-store-256.patch
git apply ../../recomp/patches/0012-native-mouse-guest-words.patch
git apply ../../recomp/patches/0013-imgui-dependency.patch
git apply ../../recomp/patches/0004-hitch-dump.patch
git apply ../../recomp/patches/0005-slow-romfs-read.patch
git apply ../../recomp/patches/0006-stop-dump-sleep-and-gpu.patch
git apply ../../recomp/patches/0007-stop-dump-object-chain.patch
git apply ../../recomp/patches/0008-sleep-time-in-dump.patch
```

To go on with the camera search, apply `0003-stick-trace.patch` the same way, after `0001`.

Then build as in `docs/SCARLETNX_BUILD-NOTES.md` and copy `build/default/scarletnx.exe` over the three copies of the
runtime (`game/scarletnx/bin/scarletnx.exe` and the two game programs in `game/recompiled/<id>/`).

To use `camera-unclamp.pchtxt`, put it in `game/recompiled/<id>/<id>_data/mods/camera-unclamp/exefs/` and recompile.
