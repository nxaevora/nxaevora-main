<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Input analysis: keyboard and mouse

Goal: the keyboard and the mouse work as native input, and the mouse look is good. This note records how the input of
ScarletNX works now, what the game reads, and the plan. Nothing is changed yet.

## How ScarletNX makes the input now

All of this is in `third_party/ScarletNX/src/nx/runtime/platform/backend/sdl3.c` (the SDL3 backend) and
`runtime/platform/unattended.c`. The game program is the same binary as `scarletnx.exe`, so a change here is a
change of the runtime.

- Keyboard: a fixed table (`keymap`) turns scancodes into console buttons. X or Space is A, Z is B, C is X, V is Y,
  Q and E are L and R, 1 and 3 are ZL and ZR, the arrow keys are the D-pad. W, A, S, D are the left stick and I, J,
  K, L are the right stick. A stick key gives the full value (32767) at once, so the camera moves at full speed or
  not at all. There is no setting to change the table. The only input settings in the `.ini` are `touch`,
  `gamepad_swap_ab`, `gamepad_deadzone`, `gamepad_rumble` and the on-screen controls (`controls*`).
- Mouse: the left button is one finger on the touch screen (`touch=2`) or on the on-screen controls. The motion of the
  mouse is the position of that finger. The mouse never reaches a stick.
- The sticks of the game are composed in `video_poll`: keyboard sticks first, then the pad of player 1 when the keys
  give 0, then the on-screen controls. `input_stick_clamp` limits the result to the circle of 32767.

## What the game reads

I searched all modules of the update for calls to `nn::hid::GetMouseState(s)`, `InitializeMouse`,
`GetKeyboardState(s)` and `InitializeKeyboard`. The SDK has these functions, but no code calls them (0 callers). So the
game takes no mouse and no keyboard from the console. It reads Npad (pad) state, the touch screen, and probably the
six-axis sensor. The HID service of ScarletNX pushes empty mouse and keyboard states, which is correct for this game.

Result: native mouse look cannot be given to the game as a mouse. The mouse movement must become right-stick values
(or sensor values). This is the same as every PC port of a console game that has no mouse code.

## Why the mouse look is poor now

- The mouse does not move the camera at all. The camera has only the keys I, J, K, L (full deflection, no
  fine control) or a pad.
- A mouse delta is a distance for each frame, and a stick is a speed. A direct copy does not work, and a stick has a
  dead zone in the game that swallows small values.

## Plan

1. Read the six-axis path first (`gamepad_motion`, `hid_six_axis`). Find out whether the game uses the sensor for
   the camera or for aiming. If it does, the mouse can feed the sensor as well. Not verified.
2. Mouse look: capture the mouse (SDL relative mouse mode) while the game has the focus and the menu is closed. In each
   poll, sum the relative motion and turn it into a right-stick value. Use a sensitivity setting, a separate setting
   for the vertical axis, an optional inverted Y axis, and a curve. Add a small offset that skips the dead zone of
   the game, so that a slow motion still moves the camera. Find the dead zone and the camera speed of the game by
   measurement in a test (the user runs the scene).
3. Keyboard: make the table `keymap` a setting (a mapping file or `.ini` keys), with the current table as the default.
   Mouse buttons also become buttons (for example left for A or ZR, right for ZL or B). The wheel can be L and R.
   Add the sticks with a smooth ramp for the left stick, so that a key can also walk slowly (a hold key for walking).
4. Touch: keep `touch=2` for the menus. When the mouse is captured for the look, the touch screen from the mouse is
   off. A key (for example Alt or Tab) frees the cursor.
5. Launcher: show the bindings, the sensitivity and the invert setting. The launcher writes the settings.

## Where the code goes

- The change is inside `sdl3.c` and `unattended.c` of ScarletNX. The `CLAUDE.md` rule says do not edit the submodule
  unless it is necessary, and tell the user. Options:
  a. A patch file in `recomp/patches/` that is applied to the submodule before the build. The pin of the submodule stays.
  b. A change that is proposed to the upstream project, with the patch kept until it is accepted.
  c. A fork of ScarletNX. This is the largest cost, so it is not the first choice.
- The game-specific values (dead zone, camera speed, button names) belong to NXAevora (`runtime/` or `recomp/config`).
  The general part (mouse capture, remap table) is useful for any game, so it fits upstream.

## Decisions of the user

- The keyboard keys stay as they are now (WASD left stick, IJKL right stick, X or Space A, Z B, C X, V Y, Q and E L
  and R, 1 and 3 ZL and ZR, arrows D-pad).
- Mouse buttons: left is ZR (throw), right is ZL (aim).
- The pad and the mouse are not used together. The device that gave the last input is the active one, and the other
  is ignored. This is a plan from that answer and is not confirmed in detail.

## Result of the check of the six-axis sensor (corrected)

The first check searched for direct calls to the SDK functions and found none. That was wrong. The game calls the SDK
through PLT stubs in `main`, so a direct search misses every call. With the stub names resolved from the relocation
table (see `docs/camera-analysis.md`) the game does call `GetSixAxisSensorState(s)` (one call site each, at 0x8ca6210
and 0x8ca62a0) and `GetNpadState` for each pad style. It has no stub for `GetMouseState` or `GetKeyboardState`, so
the earlier result for the mouse and the keyboard stands. The game reads the gyro, and its use (camera, aim) is not
known yet.

## How a patch would be built

- `game/scarletnx/bin/scarletnx.exe` and the two game programs are byte-equal copies of one runtime. After a change in
  ScarletNX the three files must be replaced by the new build. The `.abi` file records only `opt`, `aot`, `found` and
  `code`. It is not verified that a rebuilt runtime is accepted without a recompile.
- The settings live in the ini tables of the backend (`g_backend_ini`, `IniOpt`). New keys for the mouse look
  (on or off, sensitivity, vertical sensitivity, invert Y, curve) go in the same table, so that the in-game menu and the
  launcher show them with no extra code.
- The change is a patch in `recomp/patches/`. For the build it is applied to the working tree of the submodule, and the
  pin does not change. The patch file is the source of truth.

## Status: first version built (not tested)

The patch `recomp/patches/0001-native-mouse-input.patch` (see `recomp/patches/README.md`) adds the mouse look to `sdl3.c` and the
settings `mouse_look`, `mouse_sensitivity`, `mouse_vertical_sensitivity`, `mouse_invert_y`, `mouse_dead_zone_offset`
and `mouse_curve` to `settings.txt`. It builds. The runtime is copied over the three program copies, and the old
runtime is kept as `game/scarletnx/bin/scarletnx.exe.orig`.

How it works:

- The mouse is the active input after a key or mouse event. A controller button or a stick travel of about half
  makes the controller the active input. In controller mode the mouse is the touch screen as before.
- While the mouse is active, the window has the focus and no menu is open, the cursor is captured (SDL relative mouse
  mode). Holding Alt frees it.
- The motion of each poll becomes a speed (pixels a second), smoothed a little, shaped by the curve, and moved past
  the dead zone offset. The result is the right stick. Left button is ZR and right button is ZL.

## Not verified

- That the game takes the camera only from the right stick.
- The dead zone of the game. The default offset 20 percent is a guess, and the user must tune it in a test.
- That the old generated code runs with the rebuilt runtime without a recompile (the `.abi` file records no runtime
  version, so it should).
- That the cursor capture does not conflict with the menu overlay and the software keyboard.

## Keyboard map (patch 0009, tested)

The setting `keyboard_map` in `settings.txt` (the `.ini` section Keyboard) holds words `Button:Key`. The buttons are
A B X Y L R ZL ZR L3 R3 Plus Minus Left Up Right Down and LStick or RStick with Left Up Right Down. The key is the SDL
name (`SDL_GetScancodeFromName`), with `_` for a space. Rules:

- A button that the text names loses its default keys. More than one word for one button gives more than one key.
- A key that the text names belongs only to the buttons named with it, so `LStickUp:Up` takes the arrow key from the D-pad.
- `None` as the key frees the button. Empty text gives the default table.
- Escape, F1 to F12, Pause and Alt are reserved, because the runtime reads them as hotkeys.
- A word that cannot be read is logged (`settings: keyboard_map's "..."`) and the map stays as it was.

Tested by the user with `keyboard_map=ZL:Q A:F` in the game's `.ini`: the keys changed as set. The patch `0010` was
needed, because the `.ini` store held 128 keys and dropped a key past that. Not tested: the in-game menu, and a map that
frees a button (`None`).

## Native mouse look (done, 2026-10-31)

The stick based mouse look below answered late: a big move of the mouse turned the camera slowly, because the game limits
the camera's speed. The camera code was found (`docs/camera-analysis.md`) and the mouse now adds its motion to the
camera's yaw and pitch directly (patch `0012` and the mod `native-mouse-look.pchtxt`, `mouse_native_address=160708992`).
The user tested it in the field and it works. The settings are `mouse_native_yaw`, `mouse_native_pitch`, `mouse_invert_x`
and `mouse_invert_y`. With `mouse_native_address=0` the old stick based version runs. Not tested: cutscenes, dialogs,
menus, battles.

## Result so far (of the stick based version)

- The stick-based mouse look works as designed (capture, ZR and ZL on the mouse buttons, switch to the controller by its
  last input). A fast flick cannot turn the camera faster than a full stick, because the game turns the stick value into a
  rotation speed with a top speed and a ramp. Allowing a stick value past the range (`mouse_overdrive`) and removing the
  clamp of the input manager did not change this.
- A real fix needs a patch of the game's camera code, which is not found yet (`docs/camera-analysis.md`). It is later
  work. Until then the mouse look stays stick based. The settings `mouse_sensitivity`, `mouse_curve` and
  `mouse_dead_zone_offset` tune it.
