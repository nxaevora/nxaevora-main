<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Arceus timing

This document tells how the Arceus timing layer changes the game's fixed 1/30 second clocks to the elapsed time of the frame. It covers when the layer installs, its environment switches, the shared step, the physics and navigation steps, the world timers and the other hook groups. It is for contributors who change the layer.

The layer is `engine/src/nx/runtime/arceus_clock.c` with `engine/src/nx/runtime/arceus_step.h`.

## How it works

### The goal

Pokemon Legends Arceus runs its world at 30 updates per second. Most of its clocks add a fixed 1/30 second for each rendered frame. Above 30 frames per second the game therefore runs fast. The layer replaces those clocks with the elapsed time of the frame, so the world keeps its own speed at 60, 120 or an unlocked frame rate.

### When the layer installs

`snx_main` calls `arceus_clock_init` after the NVN trace and the draw record and before the JIT starts. It calls `arceus_clock_post_jit` after `jit_init`. The first call installs the clocks. The second call installs the sub-fixes.

The install has these checks in order:

1. `SNX_ARCEUS_TIMING_FIXES` must be on. It is on by default. The fixed resolution and the bare Discord path act also with it off (they are outside the layer).
2. An invalid `SNX_ARCEUS_TARGET_FPS` value logs one line and installs nothing.
3. A target (`60`, `120`, `unlocked` or `game`) installs at any rate. With no target, the frame limit must be above 30 FPS or unlimited. A runtime limit of `game` or 1 to 30 FPS then logs one line and installs nothing.
4. The game must have a module named `main` with build id `ae e8 f1 50 dd a1 b5 a8 38 80 6e 1a 5e a6 82 7a d9 f3 c5 1e` and 12 zero bytes. This is build 1.1.1. Another build logs `arceus-clock: refused unsupported main build` and nothing is installed. The code does not read the title ID.
5. Each of the three main-loop clock entries must pass its check. The entries are `read_clock` at `0x278303c`, `set_mode` at `0x2783048` and `set_duration` at `0x277bef4`. The check tests the function range, the original instruction word and the dispatch slot. A mismatch logs `refused missing or changed clock entry` and stops the install.
6. The three wrappers are written to `g_disp`. `adjust_duration` wraps the duration writes. It checks that the duration is 33333334 ns and that the caller is one of the four main-loop sites `0x278150c`, `0x2781540`, `0x2781574` and `0x27815a8`.

A sub-fix installs only when the clocks installed. `arceus_clock_post_jit` starts with the probes, which also observe at 30 FPS, and then returns when the clocks did not install. Each sub-fix checks its own instruction words and function range first. A changed function is compiled again with `jit_code_changed` and marked active with `jit_wait`.

### The switches

Every Arceus switch is an environment variable. Most switches use `arceus_switch_enabled`, which reads the variable with `getenv` and is on when the variable is unset or exactly `1`. The value switches (`SNX_ARCEUS_TARGET_FPS`, `SNX_ARCEUS_MOVEMENT_SITES`, `SNX_ARCEUS_CLIMB_STEP`, `SNX_ARCEUS_CAMERA_SPEED`, `SNX_ARCEUS_BATTLE_SEQUENCE_FIX`, `SNX_ARCEUS_CLOUD_FIX`, `SNX_ARCEUS_WAZA_SHOT_FIX`, `SNX_ARCEUS_SCRIPT_TICK` and `SNX_ARCEUS_SCRIPT_WINDOWS`) read `getenv` directly, as do `SNX_ARCEUS_SCENE_SCALE`, the `SNX_ARCEUS_TEMPORAL*` switches, `SNX_ARCEUS_UPSCALER` and the eight `SNX_ARCEUS_PERF_*` levels (`perf_level`). No Arceus switch is an `.ini` setting. `engine/src/nx/runtime/settings.txt` has no `SNX_ARCEUS` row, so a settings UI cannot list or validate these switches.

The main switches:

| Switch | Default | Effect |
| --- | --- | --- |
| `SNX_ARCEUS_TIMING_FIXES` | on | The master switch. `0` turns the whole layer off. |
| `SNX_ARCEUS_TARGET_FPS` | unset | `60`, `120` or `unlocked` sets the frame limit and the timing target. `0` is the same as `unlocked`. `game` keeps the runtime limit. Another value is refused. |
| `SNX_ARCEUS_FPS60` | unset | The legacy alias for `SNX_ARCEUS_TARGET_FPS=60`. A set `SNX_ARCEUS_TARGET_FPS` wins. |
| `SNX_ARCEUS_WHOLE_STEP` | on | `1` reports a step within 1/32 of 33333334 ns as exactly 33333334 ns. `0` reports the measured step. |
| `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX` | on | Changes the main rigid-body step to `min(1/30 s, elapsed)`. |
| `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX` | on | Changes the AI navigation step to `min(1/30 s, elapsed)`. |
| `SNX_ARCEUS_WORLD_TIMERS_FIX` | on | Moves the world timers and the frame-step cell to the elapsed step. |
| `SNX_ARCEUS_GRAVITY_FIX` | on | The gravity site loads the negated step. |
| `SNX_ARCEUS_MOVEMENT_FIX` | on | The movement sites follow the elapsed step. |
| `SNX_ARCEUS_MOVEMENT_SITES` | `0x5fc` | The hex mask of the movement sites. The default selects bits 2 to 8 and 10. |
| `SNX_ARCEUS_CAPTURE_FIX` | on | The capture waiting stages count 30 times per second of elapsed time. |
| `SNX_ARCEUS_BATTLE_SEQUENCE_FIX` | `2` | The battle sequence speed is `30 x elapsed`. `2` gives the even landing, `1` is the plain landing, `0` is off. |
| `SNX_ARCEUS_MAP_CURSOR_FIX` | on | The map cursors scale their speed by elapsed time. |
| `SNX_ARCEUS_FADE_FIX` | on | The fade color delta is multiplied by `30 x elapsed`. |
| `SNX_ARCEUS_AI_TICK_FIX` | on | The AI tick runs one frame for each 1/30 second. |
| `SNX_ARCEUS_TURN_FIX` | on | The turn slerp duration follows elapsed time. |
| `SNX_ARCEUS_LIST_SCROLL_FIX` | on | The list scroll runs only on a 30 Hz tick. |
| `SNX_ARCEUS_RIDE_SWAP_FIX` | on | Five per-call waits count 30 times a second. |
| `SNX_ARCEUS_RIDE_RAMP_FIX` | on | The ride angle ramp follows elapsed time. |
| `SNX_ARCEUS_CAPPED_FALL_FIX` | on | The capped falls follow elapsed time for the player and five ride classes. |
| `SNX_ARCEUS_AIR_STEERING_FIX` | on | The air steering at `+6F0` follows elapsed time. |
| `SNX_ARCEUS_DIRECT_MOVE_FIX` | on | The direct-position branch scales by elapsed time. |
| `SNX_ARCEUS_FIELD_WAZA_FIX` | on | The field attack waits and counters run on 30 Hz ticks. |
| `SNX_ARCEUS_WAZA_OBJECT_FIX` | on | The attack object counters advance on 30 Hz ticks. |
| `SNX_ARCEUS_WAZA_SHOT_FIX` | `2` | The shot runs on 30 Hz ticks. `2` blends the drawn transform between ticks, `1` draws it at the tick rate, `0` is off. It warns when `SNX_ARCEUS_WAZA_OBJECT_FIX` is off, because shots may then end at the muzzle. |
| `SNX_ARCEUS_CLOUD_FIX` | `2` | The eight sky cloud fields follow elapsed time. `2` scales all eight fields, `1` scales the first three, `0` is off. |
| `SNX_ARCEUS_CLIMB_STEP` | `2` | The climb mover and the climb animation follow the frame step. `2` also fixes the stop count. |
| `SNX_ARCEUS_WALL_JUMP_FIX` | on | The wall jump wait counts 30 steps a second. |
| `SNX_ARCEUS_LEDGE_FIX` | on | The ledge scripts run only on whole 1/30 second ticks. |
| `SNX_ARCEUS_EVENT_CAMERA_FIX` | on | The event camera follows elapsed time. |
| `SNX_ARCEUS_BLINK_FIX` | on | The three blink entries run their countdown on a 30 Hz cadence. |
| `SNX_ARCEUS_MENU_REPEAT_FIX` | on | The menu repeat runs only on a 30 Hz tick. |
| `SNX_ARCEUS_CAMERA_INPUT_FIX` | on | The camera input and rate follow elapsed time. |
| `SNX_ARCEUS_CAMERA_SPEED` | `1.0` | The camera rate multiplier, 0.25 to 4.0. A value outside the range logs one line and uses 1.0. |
| `SNX_ARCEUS_TIMER_CARRY_FIX` | on | Integer timers catch up, with a backlog cap of 1 second. |
| `SNX_ARCEUS_SCRIPT_TICK` | `0x4` | The hex mask of the script state classes that run on 30 Hz ticks. The default is Braviary. |
| `SNX_ARCEUS_SCRIPT_WINDOWS` | `0x1` | The hex mask of the script windows. The default is the throw window. |
| `SNX_ARCEUS_EVENT_TICK` | on | Runs the event job on a 30 Hz tick instead of every frame. `0` turns it off. |
| `SNX_ARCEUS_DEMO_TICK` | on | Runs the demo clock on a 1/60 second unit. `0` turns it off. |
| `SNX_ARCEUS_TEXT_TICK` | on | Runs the text reveal and the glyph step on the text's own tick. `0` turns it off. |
| `SNX_ARCEUS_TEXT_TICK_END` | on | With the text tick on, a text that waits (its end, a page break, closed) runs its step on every frame, so a press is seen on any frame, and an auto-advance count stays on the text's tick. `0` turns it off. |
| `SNX_ARCEUS_PASS_GUEST` | on | `0` to turn off. It acts at render scale 2.0 only (PARTICLE-SQUARES): at any other scale it does nothing and says so in one line, a warning when `1` was set, and that scale renders as without it. At scale 2 the game's bloom blur fetches whole texels of scaled render targets, and the scaled fetch gives each pixel only the texels of its own parity, so a small sparkle leaves square dots (8, 16 and 32 px apart at 4K). When it is on, a draw of at most 4 vertices and one instance with a fragment program, into color targets that are each at most half the guest width and height of the last presented picture, reads each scaled color render target it samples through a copy of that region at guest size, made by one linear blit (the mean of each 2x2 block, which is why it is 2.0 only) and blitted again when the target was drawn to. Its texel fetches and size queries then see guest texels, as the blur expects. Depth and stencil textures, targets the draw also renders to, groups, 3D targets and reads in another format stay as they are. A format without linear blits, an integer format or no memory keeps today's binding and is counted. The copies sit in the target's copy list beside its crops, apart from them, and go after 60 presents unused. A texture that resolves to a crop of a target (a header smaller than the target) is copied for the crop and then blitted for the guest copy, so such a source costs both. Each blit ends the current rendering pass, as crop copies do. Not covered: blurs the game draws at full size, and reduced passes drawn with more than 4 vertices or with instances. At 1440p a blind look at four frame rates found no tiles, and 720p and 1080p are not scaled up. Other scales would need an area-correct reduction (chained 2:1 blits for powers of two, a box pass otherwise). `arceus-pass-guest:` lines: one at start, then every 600 presents the draws served and the textures read as copies, the blits, the fallbacks by reason and the first 16 fragment programs served with their draws. At `0`, it is read once and nothing else changes. |
| `SNX_ARCEUS_NVN_BLEND_TARGETS` | on | `0` to turn off. Native NVN only. The replay keeps a blend state bind for each of the 8 colour targets and the next draw sends every target bound since the last draw, in bind order. It repairs the over-bright opening cutscene. Logs an `nvn-blend-targets:` line every 600 presents. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | on | Skips the automatic 3D resolution governor update, at every frame rate and with `SNX_ARCEUS_TIMING_FIXES=0` too. It is on unless set to a value other than `1`; then the governor stays. The game's scripted size changes stay either way. |
| `SNX_ARCEUS_DISCORD` | on | Discord presence. It is on unless set to a value other than `1`. It needs the player's `SNX_ARCEUS_DISCORD_APP`; with neither the application id nor the switch set, presence stays off. See `docs/records/game/arceus-timing.md`. |
| `SNX_ARCEUS_DISCORD_APP` | none | The player's Discord application id, digits only. It is never in the repository, and only its last four digits are logged. |
| `SNX_ARCEUS_DISCORD_BARE` | on | Discord presence without the timing layer (the game's own 30 FPS, another refused target, or `SNX_ARCEUS_TIMING_FIXES=0`). It is on unless set to a value other than `1`; without the timing layer, that value turns the presence off. |

#### Probes and traces, off by default, observe only

| Switch | Default | Effect |
| --- | --- | --- |
| `SNX_ARCEUS_CLOCK_TRACE` | off | Logs the three main-loop clock calls. |
| `SNX_ARCEUS_DIALOGUE_TRACE` | off | Installs the read-only MessageView probe. |
| `SNX_ARCEUS_CAMERA_TRACE` | off | Runs the read-only camera trace. |
| `SNX_ARCEUS_TIMING_RESEARCH` | off | Installs the read-only timing probes for the physics and navigation worlds. |
| `SNX_ARCEUS_CADENCE_CENSUS` | off | Installs the read-only census of the 30 Hz owners. It needs `SNX_ARCEUS_TIMING_RESEARCH`. |
| `SNX_ARCEUS_GRAVITY_PROBE` | off | Adds the read-only gravity probe to the census. It needs `SNX_ARCEUS_CADENCE_CENSUS`. |
| `SNX_ARCEUS_EVENT_PROBE` | off | Logs the event jobs in read-only mode. |
| `SNX_ARCEUS_DEMO_PROBE` | off | Logs the demo clock in read-only mode. |
| `SNX_ARCEUS_TEXT_PROBE` | off | Logs the text reveal in read-only mode. |

### The shared step

`arceus_step` in `arceus_step.h` keeps one step for the four consecutive main-loop updates. Channel 0 samples the wall clock. It uses the initial step when there is no earlier sample or the object changed. Otherwise the step is `now-last`. A step above `ARCEUS_MAX_STEP_NS` is cut to 100 ms. The step is never zero.

Unless `SNX_ARCEUS_WHOLE_STEP=0`, `arceus_whole_step` reports a step within 1/32 of `ARCEUS_WHOLE_STEP_NS` as exactly `ARCEUS_WHOLE_STEP_NS`. The whole step is 33333334 ns, the duration that the game's main loop passes to the four clocks at 30 FPS.

Channels 1 to 3 must follow channel 0 in order and for the same object. They reuse the step.

| Constant | Value | Meaning |
| --- | --- | --- |
| `ARCEUS_INITIAL_STEP_NS` | 16666667 | The first step at 60 FPS. |
| `ARCEUS_UNLOCKED_INITIAL_STEP_NS` | 8333333 | The first step for `unlocked`, which has no rate to start from. |
| `ARCEUS_MAX_STEP_NS` | 100000000 | The largest step, 100 ms. |
| `ARCEUS_WHOLE_STEP_NS` | 33333334 | The game's own step at 30 FPS. |
| `ARCEUS_MIN_STEP_RATIO` | 0.001 | The smallest ratio of a step to the whole step. |

`adjust_duration` writes the step into the duration argument of the clock. On channel 0 it also calls `water_step_write`, `frame_step_write`, `blink_cadence_add` and `battle_sequence_dispatch_watch`.

### The physics step

`physics_timestep_init` wraps the main rigid-body update `main+2AF25B4`, size 0x6B8. The `FCSEL` at `+0x6C` becomes `FMINNM S8,S0,S8` with the word `0x1e287808`. S0 holds 1/30 second and S8 the elapsed input. The step is `min(1/30 s, elapsed)`.

The changed function is compiled again and marked active. A guard checks the function size, the prologue and the instruction words. The physics fix refuses to install without the elapsed-time clocks.

The thrown-object flight update `main+1A7191C` is wrapped by `ball_flight_init`. It installs only when the physics fix is active, or with `SNX_ARCEUS_TIMING_RESEARCH=1`, which only logs. The ratio is `r = step / 33333334`, clamped to at most 1 and at least 0.0625. The wrapper divides `+618` and `+61C` by `r`, scales drag as `1-(1-d)^r`, scales lift by `r`, restores the authored values after the call, and multiplies `+618` back.

### The AI navigation step

`navigation_timestep_init` wraps the AI navigation world `main+2795204`, size 0x928. The mode byte at `+150` stays 0. The `FCSEL` at `+0x40` becomes `FMINNM S8,S1,S0` with the word `0x1e207828`. S1 holds 1/30 second and S0 the input. The step is `min(1/30 s, elapsed)`. The caller guard is at `main+279A6D0`.

### World timers and the frame-step cell

`frame_step_cell_init` makes one guest page for the frame step. `frame_step_write` writes it on each channel 0 update. The cell never reports more than 1/30 second.

| Offset | Value |
| --- | --- |
| `+0` | `min(1/30 s, step)` in seconds. |
| `+4` | The negative of `+0`. |
| `+8` | `-10 / step`, or -300 at the nominal step. |
| `+16` | Pi. |
| `+20` | `1 / (30 x step)`. |

`world_timers_init` installs 12 `FrameStepSite` entries and the cave light. The sites cover the world time at `1D5D59C`, the event hold at `167F988`, the event timers at `168300C` and `1683528`, and the weather fade and rise at `1B47D10`. Each site replaces a `MOV` or `MOVK W8` constant build with an `ADRP X8` and an `LDR W8` from the cell.

`cave_light_update` multiplies the increment at record `+0xC` by `step / (1/30)` for each call while the active byte `+4` is set, then restores the authored value. The ratio is clamped to at least 0.001.

The gravity site `198DA8C` in `198D7E4` loads the negated step. The movement sites are selected by `SNX_ARCEUS_MOVEMENT_SITES`. The ride angle ramp `257C9F4` in `257AF70` follows the step.

### The other hook groups

| Group | At 30 FPS | What the layer changes |
| --- | --- | --- |
| Water | The water current time adds 1/30 second for each frame. `water_fix_init` loads elapsed seconds at the two constant instructions. |
| Effects | The effect progress is converted to an integer. `effects_fix_init` keeps the fractional progress. |
| Event camera | The event camera advances by one frame. `event_camera_init` follows elapsed time. |
| Blinks | The blink countdown runs once a frame. `blink_fix_init` runs it `ticks` times through a 30 Hz cadence. |
| Camera input and rate | The stick input and the camera rate assume one frame. `camera_input_init` and `camera_rate_init` scale them by elapsed time. `SNX_ARCEUS_CAMERA_SPEED` scales the rate. |
| Menu repeat | The menu repeat advances once a frame. `pad_repeat_init` runs it on a 30 Hz tick. |
| Capture | The waiting capture stages count frames. `capture_fix_init` counts 30 times per second of elapsed time. With `SNX_ARCEUS_TIMER_CARRY_FIX` on, the carry is capped at 1 second. |
| Battle sequence | The battle sequence advances one step a frame. `battle_sequence_fix_init` uses `speed x 30 x elapsed`, cut at the next whole frame. Value 2 lands evenly, 1 is the plain cut, 0 is off. |
| Map cursors | The map cursors move one step a frame. `map_cursor_fix_init` scales the speed by elapsed time and keeps the held counters on whole 1/30 second ticks. With `SNX_ARCEUS_TIMER_CARRY_FIX` on, the carry is capped at 1 second. |
| Fade | The fade advances one step a frame. `fade_fix_init` multiplies the color delta by `elapsed x 30`. The start wrappers `fade_in_start` and `fade_out_start` reset the carried fraction. |
| AI tick | The AI runs one frame a call. `ai_tick_fix_init` runs one frame for each 1/30 second and rewrites the 8-word block at `1051860`. |
| Turn | The turn slerp uses the frame count. `turn_fix_init` makes the duration follow elapsed time. |
| List scroll | The list scroll advances a frame a call. `list_scroll_init` runs it only on a 30 Hz tick. |
| Ride swap | Five waits count frames. `ride_swap_init` counts them 30 times a second. |
| Direct move | The direct-position branch moves one step. `direct_move_fix_init` scales it by elapsed time. |
| Field attacks | The waits and counters advance per frame. `field_waza_init` runs them on 30 Hz ticks. |
| Attack object | The counters advance per frame. `waza_object_init` advances them on 30 Hz ticks. |
| Shot | The shot advances per frame. `waza_shot_init` runs it on 30 Hz ticks and blends the drawn transform. |
| Cloud | Eight cloud fields advance per frame. `cloud_fix_init` follows elapsed time. |
| Climb | The climb mover moves `speed / 30` a call and the animation divides by `speed / 30`. `climb_step_init` loads the frame step and multiplies. Value 2 also fixes the stop count at `+D04`. |
| Wall jump | The wall jump wait counts steps. `wall_jump_fix_init` counts 30 steps a second. |
| Ledge | The ledge scripts run a frame a call. `ledge_fix_init` runs them only on whole 1/30 second ticks. The smoothing at `main+1983770` follows. |
| Capped falls and air steering | The falls and the ride steering assume one frame. `capped_fall_init` follows elapsed time for the player and five ride classes and the steering at `+6F0`. |
| Script tick | The state scripts run a frame a call. `script_tick_init` runs the masked classes on the frame nearest each whole 1/30 second, keeps one-frame inputs, and holds the frame-step cell at 1/30 during a ticked run. |
| Script windows | The throw window runs between its native calls. `script_tick_init` installs the window between `main+1A91624` and `main+1A8EBA4` or `main+1A91370`. |
| Event tick | The event job runs a frame a call. `event_tick_init` has 8 job slots, runs at most one a frame, caps the carry at two runs and carries pad presses for `IsPadTrigger`, `IsPadRelease` and `IsPadRepeat`. On by default. |
| Demo tick | The demo clock runs a frame a call. `demo_tick_init` uses the clock at `C+7EC` with a 1/60 second unit, runs at most two a call, caps the carry at 4 and handles 8 controllers. On by default. |
| Text tick | The text reveal and the glyph step run a frame a call. `text_tick_init` runs the reveal every call and skips the glyph step on frames that are not the text's tick. It keeps 16 text records. On by default. |

## Current state

- On the supported build with a frame limit above 30 FPS or unlimited, the elapsed-time clocks install by default.
- The default sub-fixes install when the clocks install: physics, navigation, world timers, gravity, movement, capture, battle sequence, map cursor, fade, AI tick, turn, list scroll, ride swap, ride ramp, capped falls, air steering, direct move, field attacks, attack object, shot, cloud, climb, wall jump, ledge, event camera, blink, menu repeat, camera input and camera rate, script tick, script windows.
- Water and effects install in the same post-JIT pass, near its end.
- Off by default: the probes and traces. They only observe. The fixed resolution switch is on by default (on unless set to a value other than `1`).
- Known issue, not fixed: with `SNX_ARCEUS_TEXT_TICK` on, the "next" prompt at the bottom right of the text box shows for a short time and then goes away.
- At `fps_limit=game` or 1 to 30 FPS with no target, the whole layer installs nothing.
- The layer supports only the `main` module of build 1.1.1. Another build logs one line and installs nothing.
- Limits: a step above 100 ms is cut to 100 ms. With `SNX_ARCEUS_TIMER_CARRY_FIX` on (the default), the integer timer carry is capped at 1 second. With it off, a carry of one tick or more is set to 0. The cave light ratio is at least 0.001. The thrown-object ratio is at least 0.0625 and at most 1. The capture and map-cursor carry is capped at 1 second. The frame-step cell reports at most 1/30 second.
- Table sizes: 8 event job slots, 8 demo controllers and 16 text records. A full table leaves the item as the game runs it, with a count.
- Other contributors change this layer in open pull requests. This document describes the code of the base commit `5e35efe`.
