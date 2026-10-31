<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Patches for ScarletNX and the game

The patches change `third_party/ScarletNX` or the game code. The submodule pin does not change. Apply a patch to the
working tree of the submodule before the build, and keep the patch file as the source of truth.

| Patch | What it does |
| --- | --- |
| `0001-native-mouse-input.patch` | Native mouse input. The mouse is captured and drives the right stick. The left button is ZR and the right button is ZL. A controller input makes the controller the active input. Hold Alt to free the cursor. New settings `mouse_*` (look, sensitivity, vertical sensitivity, invert Y, dead zone offset, curve, overdrive). It also holds the stick trace, a diagnostic for the camera analysis: with `trace_stick=1` in the `.ini`, the right stick is held at a known value after `trace_stick_start` seconds, the guest memory is searched for the floats that the game makes of it, and the code that reads or writes them is logged for `trace_stick_seconds`, with the call chain of the first reads (Windows only). See `docs/input-analysis.md` and `docs/camera-analysis.md`. |
| `camera-unclamp.pchtxt` | A patch of the game code (IPSwitch text). It sets the clamp limits of the input manager to 8.0 and -8.0. A test showed that the camera does not use that clamp, so it has no effect on the camera. It stays as a record. Remove the mod when the camera patch exists. |

Apply the ScarletNX patch, from `third_party/ScarletNX`:

```sh
git apply ../../recomp/patches/0001-native-mouse-input.patch
```

Then build as in `docs/SCARLETNX_BUILD-NOTES.md` and copy `build/default/scarletnx.exe` over the three copies of the
runtime (`game/scarletnx/bin/scarletnx.exe` and the two game programs in `game/recompiled/<id>/`).

To use `camera-unclamp.pchtxt`, put it in `game/recompiled/<id>/<id>_data/mods/camera-unclamp/exefs/` and recompile.
