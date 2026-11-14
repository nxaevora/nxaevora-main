<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Arceus timing above 30 FPS

Pokémon Legends: Arceus 1.1.1 runs its world at 30 updates per second. Most of its clocks add a fixed 1/30 second
on each rendered frame, so above 30 FPS the game runs fast. `src/nx/runtime/arceus_clock.c` in the vendored
ScarletNX changes those clocks to the real elapsed time. The timing fixes are on by default and can be disabled with the master or individual switches below. Setting
the master to `0` also suppresses the opt-in traces, research probes and fixed-resolution hook. The layer does
nothing for another game or another build. Each change first checks the build id of `main` and the instruction
words it replaces, and it logs a refusal and changes nothing when they differ.

## Switches

Unset timing switches use the defaults below. `SNX_ARCEUS_TIMING_FIXES=0` turns off the entire timing layer. For every individual fix, setting its switch to `0` turns off that fix; probes and graphics settings remain opt-in.

| Switch | Default | What `=0` does |
| --- | --- | --- |
| `SNX_ARCEUS_TIMING_FIXES` | on | Installs nothing and suppresses the traces, research probes and fixed-resolution hook too. |
| `SNX_ARCEUS_TARGET_FPS` | runtime setting | Not a boolean: unset or empty keeps the runtime's `fps_limit` and does not write `g_opt_fps` (unless legacy `FPS60=1` is set). At game pace or 30 FPS and below, nothing installs and one reason line is logged. Above 30 FPS or at 0 (unlimited), clocks and defaults install. `game` is a test value that installs at the current runtime pace without changing the limit. This choice is made at startup. `60`, `120`, `unlocked` or `0` set the limit; other values are refused. |
| `SNX_ARCEUS_FPS60` | off | Legacy alias; `=1` selects 60 FPS. |
| `SNX_ARCEUS_WHOLE_STEP` | on | Reports a measured step near 1/30 second as measured, not as the game's own step. |
| `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX` | on | Leaves the physics step at its authored value. |
| `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX` | on | Leaves AI navigation at its authored step. |
| `SNX_ARCEUS_WORLD_TIMERS_FIX` | on | Leaves world timers at their authored rates. |
| `SNX_ARCEUS_GRAVITY_FIX` | on | Leaves gravity integration at its authored rate. |
| `SNX_ARCEUS_CAPTURE_FIX` | on | Leaves capture timing at its authored rate. |
| `SNX_ARCEUS_BATTLE_SEQUENCE_FIX` | on (2) | Leaves battle sequence timing at its authored rate. `2` is the even landing, `1` the plain landing. |
| `SNX_ARCEUS_MAP_CURSOR_FIX` | on | Leaves map cursor movement at its authored rate. |
| `SNX_ARCEUS_FADE_FIX` | on | Leaves fade timing at its authored rate. |
| `SNX_ARCEUS_EVENT_CAMERA_FIX` | on | Leaves event camera timing at its authored rate. |
| `SNX_ARCEUS_BLINK_FIX` | on | Leaves blink timing at its authored rate. |
| `SNX_ARCEUS_CAMERA_INPUT_FIX` | on | Leaves camera response and manual rotation at their authored rates. |
| `SNX_ARCEUS_CAMERA_SPEED` | 1.0 | Not a boolean: selects the default manual rotation multiplier. |
| `SNX_ARCEUS_MENU_REPEAT_FIX` | on | Leaves menu repeat at its authored rate. |
| `SNX_ARCEUS_TIMER_CARRY_FIX` | on | Disables bounded catch-up for integer timers. |
| `SNX_ARCEUS_AI_TICK_FIX` | on | Leaves AI script ticks at their authored rate. |
| `SNX_ARCEUS_TURN_FIX` | on | Leaves turn interpolation at its authored rate. |
| `SNX_ARCEUS_LIST_SCROLL_FIX` | on | Leaves list scrolling at its authored rate. |
| `SNX_ARCEUS_WALL_JUMP_FIX` | on | Leaves wall-jump timing at its authored rate. |
| `SNX_ARCEUS_LEDGE_FIX` | on | Leaves ledge movement at its authored rate. |
| `SNX_ARCEUS_CLIMB_STEP` | 2 | Disables climb step and stop timing fixes. `1` enables the step fix only; `2` also enables the stop fix. |
| `SNX_ARCEUS_MOVEMENT_FIX` | on | Leaves the selected character movement sites at their authored rates. |
| `SNX_ARCEUS_MOVEMENT_SITES` | `0x5fc` | Not a boolean: selects movement sites by bit mask. Explicit masks are used as provided. |
| `SNX_ARCEUS_CAPPED_FALL_FIX` | on | Disables the capped-fall correction. |
| `SNX_ARCEUS_AIR_STEERING_FIX` | on | Leaves air steering at its authored rate. |
| `SNX_ARCEUS_RIDE_SWAP_FIX` | on | Leaves ride-swap countdowns at their authored rate. |
| `SNX_ARCEUS_RIDE_RAMP_FIX` | on | Leaves the ride controller's angle ramp at its authored rate. |
| `SNX_ARCEUS_CLOUD_FIX` | 2 | Leaves the sky cloud layers at their authored rates. `1` fixes three of the eight fields; `2` all eight. |
| `SNX_ARCEUS_DIRECT_MOVE_FIX` | on | Leaves the direct ground-move step at its authored rate. |
| `SNX_ARCEUS_FIELD_WAZA_FIX` | on | Leaves field-move countdowns and turn input at their authored rates. |
| `SNX_ARCEUS_WAZA_OBJECT_FIX` | on | Leaves attack-object timers at their authored rates; if shot fix stays on, logs one warning. |
| `SNX_ARCEUS_WAZA_SHOT_FIX` | 2 | Disables shot timing/blending. `1` runs at tick rate; `2` also blends between ticks. |
| `SNX_ARCEUS_SCRIPT_TICK` | `0x4` | Not a boolean: a mask of state script classes that run 30 times a second. `0` leaves every script on every frame. Explicit masks are used as provided. |
| `SNX_ARCEUS_SCRIPT_WINDOWS` | `0x1` | Not a boolean: a mask of script windows. Bit 0 is the throw window. `0` turns it off. |
| `SNX_ARCEUS_CLOCK_TRACE` | off | `=0` keeps the trace off; set to `1` to log clock calls. |
| `SNX_ARCEUS_DIALOGUE_TRACE` | off | `=0` keeps the read-only trace off. |
| `SNX_ARCEUS_CAMERA_TRACE` | off | `=0` keeps the read-only trace off. |
| `SNX_ARCEUS_TIMING_RESEARCH` | off | `=0` keeps read-only timing counters off. |
| `SNX_ARCEUS_CADENCE_CENSUS` | off | `=0` keeps the read-only census off. |
| `SNX_ARCEUS_GRAVITY_PROBE` | off | `=0` keeps the read-only probe off. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | off | `=0` leaves the game's automatic 3D resolution governor enabled. |

The default movement mask selects bits 2 through 8, the seven player movement sites, and bit 10, the player's horizontal position step. Bits 0 and 1 remain off. An explicit mask such as `7fc` adds bit 9's capped-fall selection while preserving the same sites. `1fc` leaves bit 10 out.

## The shared step

The four main-loop clocks are updated one after another on each frame. `arceus_step.h` takes one wall-clock sample
for the four updates, so they always agree. The step is never zero: a clock that did not move gives the initial
step of the target rate. A stall longer than 0.1 second is cut to 0.1 second, so the world does not jump. Test
`arceus_timing` checks this and the camera stick update of `arceus_camera.h`.

A measured step within 1/32 of 1/30 second (32.3 to 34.4 ms) is the game's own pace with the jitter of a real
frame. It is reported as exactly the 33333334 ns the game passes its clocks, unless `SNX_ARCEUS_WHOLE_STEP=0`.
A played session at the game's pace with no frame limit set measured steps of 32.8 to 33.8 ms, half of them a
hair under 1/30 second. On about 7 % of the frames the 30 Hz gates then skipped the frame and did two frames of
work on a later one, and the battle sequence player ran two whole frames in one call, so the once-per-frame step
of its commands never saw the first. In that session models disappeared, health bars did not move and moves did
not play, while the unchanged game at the same pace was fine.
With the whole step every gate ticks once per frame and every scaled value is the authored one. Test
`arceus_clock` runs 16 wrapper cases on that session's steps and on 33.0 to 33.6 ms and compares guest memory
and return registers with the game's function called alone, call for call. Game time at 29.1 to 31 FPS then
follows the frame count, as in the unchanged game, and is up to 3 % off the wall clock. A step outside the
window is still the measured one.

## Physics step

The main rigid-body world takes one fixed 1/30 second step per rendered frame, so above 30 FPS physics runs at
FPS/30 speed. The default-on `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX` makes the step the smaller of 1/30 second and
the elapsed time. The `arceus-physics-timestep` line in the log confirms it.

With only the step corrected, thrown Poké Balls flew in slow motion. The flight update at `main+1A7191C` applies
drag, lift and its flight timer once per update and assumes 1/30 second each. While the step fix is on, a wrapper
scales those three by the step times 30 for each call and restores the authored values afterwards. With timing
research on and the step fix off, the same wrapper only observes. `arceus-ball-flight` lines record each launch and
flight end.

`SNX_ARCEUS_CADENCE_CENSUS=1` with `SNX_ARCEUS_TIMING_RESEARCH=1` counts calls per second of 32 functions that use
other fixed 1/30 second steps, and logs `arceus-cadence` lines every five seconds. It only observes. It shows which
of them run once per rendered frame (a high-FPS bug) and which run at 30 Hz.

## AI navigation step

`main+2795204` steps the AI navigation world once per rendered frame. Its mode byte at +150 stays at the
constructor's 0, which selects a fixed 1/30 second, so at about 66 FPS navigation advanced about 2.2 times real
time. The default-on `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX` turns the FCSEL at `main+2795244` into FMINNM, so
the step is the smaller of 1/30 second and the elapsed time. The path has no zero-step check, and the shared step
is never zero. The `arceus-navigation-timestep` line confirms it.

## World timers

The cadence census found these owners running once per rendered frame, each adding a fixed 1/30 second per call:

- `main+1D5D59C` passes 1/30 to the day clock `main+24AC8C4`, which counts in-game minutes. At 60 FPS a day took
  half as long.
- `main+167F988` counts how long an event condition has held against a threshold. Its callees `main+168300C` and
  `main+1683528` keep three more such timers.
- `main+1B47D10` raises and lowers three weather blend values by a configured rate times 1/30.
- `main+1E1A25C` adds 1/120 per call to a cave light transition.

The default-on `SNX_ARCEUS_WORLD_TIMERS_FIX` gives them the smaller of 1/30 second and the elapsed time. The
twelve constant builds load it from a cell that the runtime writes on each frame, as the water clock does. The
cave light increment is scaled for each call and restored. The `arceus-world-timers` line confirms it. With the
census on, `arceus-cadence: world_minutes` lines show the day clock, which should advance about five minutes in
each five-second window.

The shared character movement at `main+1982408` also scales velocity by 1/30, but only to probe a collision-aware
move, and multiplies the result by 30 again. It does not depend on the frame rate.

## Gravity probe

In its airborne branch the shared acceleration at `main+198D7E4` writes a vertical term of gravity times a
per-character factor and rate times 1/30, and it does not add the current velocity. If its caller adds that term to
the velocity on every call, falls and jumps run faster at high FPS. With the census on, `SNX_ARCEUS_GRAVITY_PROBE=1`
logs `arceus-gravity` lines. While that term is negative, it follows one character for up to four seconds. Each
call logs the velocity the function read, the position it was given and its result, with real time. If the velocity
gains the vertical term on every call, gravity runs at FPS/30 speed, and the positions give the real acceleration.
It only observes.

## Capture fix

The capture director at `main+180C338` counts its waiting stages in calls. It runs once per rendered frame, so at
120 FPS a field catch took about 3.0 seconds against 6.1 at 30 FPS. The default-on `SNX_ARCEUS_CAPTURE_FIX`
keeps a sum of frame steps for each capture object. In stages 3, 5, 7, 8, 9, 14 and 16, a call only counts the
stage timer at `+1DC` down once a whole 1/30 second has built up. Otherwise the timer is raised by one before the
call and put back after it, so the call leaves it unchanged. Stage 8 moves from that timer, so its motion updates
at 30 Hz.

Before stages 3, 5 and 7 count, the director waits until its effect handles are ready, which takes a fixed number
of calls. The fix repeats the director's checks of those handles. A call that waits counts as one whole 1/30
second, as it takes at 30 FPS, so the countdown afterwards waits until the elapsed time catches up. That debt is
capped at one second. The `arceus-capture-fix` line confirms the fix.

## Battle sequence fix

Battle effect sequences, the `.wbin` player whose update is `main+9DBEAC`, add their speed at `+F0` to the frame at
`+E8` once per call, so captures in battle and battle moves ran four times fast at 120 FPS. The default-on `SNX_ARCEUS_BATTLE_SEQUENCE_FIX` passes speed times 30 times the elapsed time for the call and puts the speed
back after it.

The game depends on whole frames in two places. The update makes a finished command's end check, which can make a
track wait, only in the call that is entered with `+E8` on a whole frame. And the commands' step slot, called by
`main+9DB748` once per rendered frame, has actions that fire when the frame it is given equals a whole frame exactly.
So every whole frame has to be the value one call ends on and the value the next call is entered on. The step is
therefore cut at the next whole frame and the rest of the budget waits for the next rendered frame. A call makes one
update for a speed up to 1 and at most as many as whole frames in the game's own step above that, and the budget keeps
at most one such step, so a stall is not caught up, as in the game. An earlier form caught up with further updates in
the same call. At 30 FPS, where a rendered frame is a hair under or over one sequence frame, that passed whole frames
no rendered frame ended on: character models stayed hidden and moves did not play.

At exactly 60 FPS the cut moves the frame unevenly (8.50, 8.99, 9.00, 10.00), which the battle send-out and recall
camera shows as stepping. The default is the even landing (unset or `2`, with `1` for the plain cut): a step that would end within
half an advance before a whole frame goes onto it and the difference is carried, signed. Nothing else changes. On
frame times of a 60 FPS session every rendered frame then moves by 0.7 to 1.3 of its share. Two earlier forms of
value 2 moved on from the whole frame in the same rendered frame, one by writing the rest to `+E8` and one with a
second update. The first skipped the end checks, so thrown balls came back before the Pokémon was out, and both hid
the whole frame from the step slot. The present form was played at 60 FPS, where the camera is smooth, and unlocked,
where the user found everything normal. In a 60 FPS session 52 sequences ended at their authored length (81 frames in
2.68 s) with two calls per sequence frame.
Speeds outside (0, 4] only
update once a whole 1/30 second has built up. `arceus-battle-sequence` lines count the calls, the partial steps,
the skipped calls and the speeds seen. With `SNX_ARCEUS_TIMING_RESEARCH=1` that line comes every 250 calls, each
sequence that ends logs its frames, calls and seconds (81 frames must take about 2.7 s at any frame rate), and once a
second a line says whether the dispatch entry of `main+9DBEAC` still is the wrapper. A sequence that has only had the game's own step (see the shared step)
runs the game's update unchanged, at any speed. Its first other step starts the cutting.

## Map cursor fix

The map cursors at `main+120F1F4` (field map) and `main+1245DB8` (world map) add stick times speed to the cursor
position once per call, and count held frames in two counters up to 9, so the fast-travel cursor ran four times
fast at 120 FPS. The default-on `SNX_ARCEUS_MAP_CURSOR_FIX` scales the speed by the elapsed time over 1/30
second for the call and restores it, and lets the counters rise only once a whole 1/30 second has built up. The
`arceus-map-cursor` line says how many of the two cursors it covers.

## Gravity fix

The gravity probe showed that the airborne branch of `main+198D7E4` runs once per rendered frame and adds gravity
times a factor, a rate and 1/30 second to the velocity on each call, so falls accelerated at FPS/30 times gravity.
The default-on `SNX_ARCEUS_GRAVITY_FIX` makes that constant the negated frame step: the MOV/MOVK pair at
`main+198DA8C` loads it from the frame-step cell of the world timers. The cell setup is now shared by every fix
that loads it, and the first one maps it. The `arceus-gravity-fix` line confirms the fix.

## Movement fix

With only gravity on the elapsed time, sideways motion in the air still ran four times fast against the fall,
because other character movement steps add acceleration times 1/30 once per call. The default-on `SNX_ARCEUS_MOVEMENT_FIX` covers ten of those constants and can make each load the frame step from the
same cell. By default it turns on eight of them (bits 2 to 8 and 10, the player sites). Bits 0 and 1, the shared sites, turn on
only when an explicit mask sets them. Each is a MOV/MOVK pair of plus or minus 1/30:

| Bit | Site | Owner |
| --- | --- | --- |
| 0 | `main+198E020` | shared airborne force (`main+198D7E4`), used by the player and Pokémon |
| 1 | `main+1984A70` | shared vertical (`main+19843A0`) |
| 2 | `main+1A0BEE4` | player velocity acceleration (`main+1A08560`) |
| 3 | `main+1A0BAF0` | player speed acceleration (`main+1A08560`) |
| 4 | `main+1A0B3AC` | player countdown `+750` (`main+1A08560`) |
| 5 | `main+1A0EEA0` | player airborne force (`main+1A0E0EC`) |
| 6 | `main+1A158C8` | player vertical (`main+1A14F80`) |
| 7 | `main+1A17B10` | player state acceleration (`main+1A173B0`) |
| 8 | `main+1A17F3C` | player state force (`main+1A173B0`) |

| 10 | `main+1A0DA20` | player horizontal displacement (`main+1A0D9DC`) |

Bit 10 is a different kind of step. `main+1A0D9DC` sets the player's position from the velocity at `+118` times 1/30
on every call, after its caller has integrated the acceleration, so without it that displacement is 2 times too long
at 60 FPS. It was on (mask `7fc`) in 59 play sessions at 60, 120 and unlocked frame rates. No session isolates it.

`SNX_ARCEUS_MOVEMENT_SITES` is an optional hex mask over these bits, to test the sites one by one. Without it the
default `0x5fc` turns on the eight sites of bits 2 to 8 and 10. An empty value turns on all ten. The
`arceus-movement-fix` line gives the number installed and the mask.

## Fade fix

The common fade at `main+C21490`, in its linear color mode, adds one to the frame at `+538` and the delta at `+550`
to the color at `+540` on each active call, so fades ran four times fast at 120 FPS: an eight-frame fade ended in
67 ms. The default-on `SNX_ARCEUS_FADE_FIX` makes each call add the delta times the elapsed time times 30.
The frame only advances once a whole 1/30 second has built up, by staging `+538` one lower before the call. The
call that completes the last frame adds only the share up to the end, so a fade-out does not overshoot while it
waits to finish, and the authored delta is put back after each call unless the call replaced it. The fraction is
kept for each fade object, up to 16. A fade-in frees its slot on the call that ends it. It starts over when the fade starts again (`main+C21D00` and `main+C227BC`)
or when the frame, duration or state differ from what the last call left. With all slots in use a call runs as
the original. The other fade modes are unchanged. `arceus-fade` lines count the calls.

## Sky clouds

The sky update `main+29BFDAC` advances eight fields of the sky block once per call, so the cloud layers and their
ground shadows moved 2 times fast at 60 FPS and 4 times at 120. `SNX_ARCEUS_CLOUD_FIX=1` makes three of them use the
elapsed time by loading derived words of the frame-step cell: `+614` divides its parameter by minus 10/step where the
game divides by minus 300, and
`+64C` and `+650` divide by the parameter times 30 times the step. The default, `2`, also covers `+5DC` in the same
way and wraps the function to scale the four phase increments at `+604` to `+610` by the frame's share of 1/30 s.
At the game's own step the derived words are the game's constants (minus 300 and 1), so all eight fields get the
game's own values bit for bit, and a sky block that the call replaced is left alone.
The fix patches game code only and needs nothing from the renderer.

Played: at 30 FPS the clouds and their shadows look normal and at 120 FPS both were too fast. With value 1 the user
called the clouds normal. Value 2 followed after a measurement showed five fields still stepping per call (2.04 times
at 60 FPS, 4.08 at 120) and was on in 39 play sessions.

## Unlocked frame rate

With `SNX_ARCEUS_TARGET_FPS=unlocked` the runtime has no frame limit, and every fix in this file follows the
measured frame step, whatever the rate. The shared step keeps its rules: it is never zero, and a stall longer than
0.1 second is cut to 0.1 second. The cave light ratio had a floor of 1/16 of a 1/30 second step, which bound above
480 FPS. It is now 1/1000 and only keeps the ratio above zero. The thrown-object flight keeps its own floor. Tests
`arceus_timing` and `arceus_clock` check the shared step and the fixes with variable and jittered steps up to
1000 FPS.

## Sneasler climb

The Sneasler wall climb mover, `main+25540E8`, moves the climber by its direction times the climb speed divided by
30 on each call. That is a fixed distance per rendered frame, so at 120 FPS Sneasler climbed four times as fast.
By default, `SNX_ARCEUS_CLIMB_STEP=2`; setting it to `1` replaces its `FMOV #30` and `FDIV` at `+2F4` with a
load of the frame step and a multiply, so each call moves speed times the elapsed time. The patch makes room by
storing the `+D20` copy with a second `STUR` instead of an `LDP`/`STP` pair. It checks the function, the speed load
and all twelve words it replaces, and refuses otherwise.

The climb animation, `main+2549FA0`, divides the eased distance at `+D50` by the speed divided by 30 for its blend
and playback ratio. With the mover fixed that ratio was 1/4 at 120 FPS, which looked like a nearly still climb, so
the same switch replaces its `FMOV #30` and `FDIV` at `+950` with the frame step times the speed. The room comes
from an `MVN` and `AND` pair becoming one `BIC`. If this part's guard fails, the mover fix stays and the log says
the animation ratio is off.

With `SNX_ARCEUS_CLIMB_STEP=2` a wrapper on the climb animation also fixes the climb's stop. In stop state (`+D00`
is 2) the animation adds one to the count at `+D04` on each call and eases the stop over its authored number of
frames, so at 120 FPS the stop lasted a quarter of its time. A call that does not complete a whole 1/30 second now
gets the count staged one lower, so it rises 30 times a second. The fraction is kept for each climbing object, up
to four, and starts over with each stop or when the count is not what the last call left. The wrapper's guard
checks the state test, the count's load, add and store, and the compare that ends the stop.

After a jump up the wall, the ride script waits on `climbingJumpWaitCount`. The script runs on the game's Lua 5.3
VM and sets that bound field, `+F4` of the object its `RideNyuuraStateContext` userdata points to, to 15, then
counts it down by one each update. At 120 FPS the wait took 0.125 second instead of 0.5. The default-on `SNX_ARCEUS_WALL_JUMP_FIX` wraps `lua_tointegerx` (`main+50BA0`). When the ride's field setter (the C closure at
`main+2335E5C`, from `main+2331B64`) converts the value of a write to that key, and the write steps the count down
by one, the conversion returns the old value unless the call completes a whole 1/30 second. The fraction is kept
for each object, up to four. Any other write, such as the set to 15, starts it over.

Climbing over a ledge onto the ground is driven by the ride state script. The script ends the climb with the native
`EndClimbing` (`main+2553EE0`) with type 0, then moves Sneasler up and over the top by setting its position once per
script update, waiting 6 updates and then moving over 15. It ends with `EndClimbingEnd` (`main+2554008`). The script
runs once per rendered frame through `RideNyuuraStatePreUpdate` (`main+26163E0`) and `PostUpdate` (`main+2616AB0`),
so the move took 175 ms at 120 FPS instead of 700. The default-on `SNX_ARCEUS_LEDGE_FIX` wraps all four. The
state object whose script calls `EndClimbing(0)` owns a window until `EndClimbingEnd`, another end type of the same
controller, or 2 seconds. In the window its updates run only on calls that complete a whole 1/30 second. A skipped
update returns at once, as both functions do when no script is bound. A PostUpdate follows the PreUpdate of the same
main-loop frame. Other states are not affected. On its own this places the ride 30 times a second during the move.

To keep the move smooth, the same switch also wraps the shared set position (`main+1983770`). When the script sets
the ride's position in the window on a frame shorter than 1/30 second, the ride is placed at the script's previous
target and the new one is kept. On the updates in between, the ride is placed on the point between the two that the
elapsed time has reached, so it moves every frame about 1/30 second behind the script. Before each script run the
ride is put back on the script's own last target, so the script only reads positions it set. A window that closes
while the ride is behind places it on the last target, unless the script set a newer position first. A set position
on the ride from any other caller ends the steering, so movement from elsewhere is never overwritten.

## Capped falls and air steering

At capped horizontal speed a fall takes its vertical term from the queue that `main+19843A0` fills: it holds one
-g x factor x 1/30 contribution, added to the velocity once per call, and the call runs once per rendered frame. With
The default-on `SNX_ARCEUS_CAPPED_FALL_FIX` (or bit 9 of `SNX_ARCEUS_MOVEMENT_SITES` with the movement fix on)
corrects that contribution on a new queue entry to the elapsed step, for the player and the five ride classes whose
slots point at the shared consumer `main+198D7E4`. Other classes and unchanged queues keep their values.

The same function queues an air steering increment, direction x config x stick at `+6F0`, which the consumer also
adds to the velocity once. The script native at `main+1FF4F44`, and for one ride class `main+252B974`, call it once
per frame, so air acceleration doubled at 60 FPS and quadrupled at 120. The default-on `SNX_ARCEUS_AIR_STEERING_FIX` scales a new
steering entry by the step over 1/30 second. At 30 FPS or slower nothing is written. Both switches share one wrapper on
`main+19843A0`, and `arceus-capped-fall` and `arceus-air-steering-fix` lines count what they change. With movement
bit 1 on, which already loads the frame step into the same gravity term, the capped fall correction stays off with a
warning and air steering still installs. Test
`arceus_clock` checks that one second of steering entries sums to 30 authored ones at 60, 120 and uneven steps.

## Timer carry

The capture stage countdown and the map cursor's held-frame counters tick once per 1/30 second of built-up time, but
dropped the whole remainder when one step held more than a tick. After a 100 ms frame one tick was used and about
67 ms lost, so after slow or uneven frames these timers fell behind for good. The default-on `SNX_ARCEUS_TIMER_CARRY_FIX` keeps the remainder, capped at one second, and later calls catch up one tick at a
time. No game call is replayed. Test `arceus_clock` runs 300 frames of 8 to 40 ms with 100 ms stalls and checks that
the ticks equal the elapsed time over 1/30 with the exact remainder.

## AI frame count

The AI context scheduler `main+1050E00` adds 1 to every script context's frame count at `+88` per pass, and it runs
once per rendered frame. Wild Pokémon vigilance, hunger, sleep, look-around waits and AI turns read that count, so at
60 FPS they ran twice as fast: wild Pokémon noticed the player twice as quickly. The default-on `SNX_ARCEUS_AI_TICK_FIX` makes a pass add 1 only once a 1/30 second has built up for that scheduler, else 0. The count
travels in a callee-saved vector lane of the calling thread, and eight words at `main+1051860` are rewritten in place,
so no shared state is needed. The AI Turn and TurnKey actions turn one whole step per call, and the actor calls them outside the
scheduler pass: every pass on a forced context, every few passes otherwise. Each call earns the ticks per pass since the
node's last call, and the action runs when a whole step is earned, so a turn makes the same number of steps as at
30 FPS. The first call always runs. `arceus-ai-tick` lines count the passes, ticks, held and run turn calls.

## Turn easing

The orientation helper `main+1727DF4` turns a character by a slerp from its current orientation toward the target
with a fixed fraction of the remaining angle per call, and the player and ride updates call it once per rendered frame,
so turning eased faster with the frame rate. The default-on `SNX_ARCEUS_TURN_FIX` changes the duration argument
so the fraction becomes 1 - (1 - t)^(step x 30), the share a 30 FPS sequence covers in the same time. The 1/30 that two
callers pass as a duration stays as it is, and AI turns go through another function, so nothing is scaled twice.

## List scroll

A scrolling UI list, the satchel's item list for example, keeps a scroller at `+7F8` that `main+272F150` updates once
per rendered frame. A one-row scroll is an ease counted in updates, and when it ends while the direction is still held
the list starts a free scroll that adds a speed per update until release. At 30 FPS the ease lasts longer than a tap.
At 150 FPS it ends inside the tap, so a single D-pad press ran the free scroll and jumped five to seven rows. The default-on `SNX_ARCEUS_LIST_SCROLL_FIX` runs the scroller only on frames that complete a 1/30 second tick, so the
ease takes its 30 FPS time. Every list that scrolls through this function steps 30 times a second, which can look less
smooth than the frame rate. Test `arceus_clock` checks that a 100 ms tap scrolls one row at 30, 60, 150 and 300 FPS.

## Ride swap

The ride controller's update `main+2537B28` runs once per rendered frame and counts five waits down by one per call
(`+2BC`, `+4A0`, `+2C0`, `+3C0` and `+2A8`). The wait before a quick swap to another ride is 18 to 20 of those calls, so
the swap that takes about 0.6 s at 30 FPS took 0.17 to 0.24 s unlocked. The default-on `SNX_ARCEUS_RIDE_SWAP_FIX`
lets the waits count 30 times a second: on the other frames a running count goes in one higher and comes out as it was.
A count the call did not touch is left as the game set it. `arceus-ride-swap` lines log counts that start or end.

## Ride angle ramp

The ride controller update `main+257AF70` advances an angle at `+324` by a parameter times 1/30 on every call, up to
the limit at `+690`. The default-on `SNX_ARCEUS_RIDE_RAMP_FIX` makes the constant at `main+257C9F4` load the frame
step, so the ramp keeps its authored rate and its limit. Nothing else in the function is changed. It refuses unless
the next use of the constant is the float move it expects. The fix was on in 58 play sessions at 60, 120 and
unlocked frame rates. No session isolates it.

## Wild Pokémon attacks

The ground movement helper `main+1982408` has a direct-position branch that sets the probed position by speed/30 on
every rendered frame. The default-on `SNX_ARCEUS_DIRECT_MOVE_FIX` scales that position step by the elapsed
frame time. It only changes short steps from the guarded branch. The host test compares a half-second, 10-unit/s move
at 30, 60, 190 and 300 FPS.

Wild field moves update at `main+1DC32C0`. The observed attack uses the wait at `+3B0` and frame count at `+370`;
The default-on `SNX_ARCEUS_FIELD_WAZA_FIX` holds them on frames without a 30 Hz tick. The measured 35-frame wait and 75-frame
count lasted about 1.13 s and 2.47 s unlocked, close to their authored 1.17 s and 2.5 s. The wrapper also holds
`+218` and scales the turn at `+3AC`, based on static code reading and host tests. Those states were not observed in
the six play sessions, so that part has not been gameplay-validated.

The field attack object update at `main+1D91DF4` counts the warning and attack life in `+5E4` and `+5E8` calls.
The default-on `SNX_ARCEUS_WAZA_OBJECT_FIX` advances those counters on 30 Hz ticks. It also holds `+5F0`, the terrain-hit
countdown, at the same rate. The host test checks 9- and 15-frame waits and the 6-frame terrain hold at 30, 60, 190
and 300 FPS. In play, the fix restored Luxio's warning before its shot.

The field shot update at `main+1DBF4F4` moves a projectile or places it according to whole frame counters. With a
by default, `SNX_ARCEUS_WAZA_SHOT_FIX=2` runs that update on 30 Hz ticks and blends the drawn transform between
those updates, with a position up to one tick behind the game state. Value `1` runs at tick rate without blending;
`0` disables the fix. If shot fix is enabled while `SNX_ARCEUS_WAZA_OBJECT_FIX=0`, the runtime logs one warning and
leaves both settings as requested. Value `1` produced the expected speed but visibly stepped at high frame rates.
Value `2` flew smoothly and left Luxio cleanly in the test session. Measured against a 30 FPS session, the shot turned
1.60 (value `1`) and 1.68 (value `2`) times the change in bearing to the player, against 1.58 at 30 FPS. That is within
the spread of 5 to 6 shots per session, so a difference under about 15 percent would not show.

## Script tick

The state scripts of the player, the player's own Pokémon and the rides get no time from the engine. They count
their own updates and call natives that apply a fixed amount per call, and each state class runs its script once per
rendered frame through a PreUpdate and a PostUpdate bridge. `SNX_ARCEUS_SCRIPT_TICK` is a hex mask of state classes
whose two bridges run only when a whole 1/30 s has built up: bit 0 Sneasler, 1 Ursaluna, 2 Braviary, 3 Basculegion,
4 and 5 Wyrdeer, 6 the player's Pokémon, 7 to 9 the three player classes. On the other frames a bridge returns the
current state index, as it does without a script. One decision is made per engine frame and shared by both bridges.
The tick comes on the frame nearest to the moment a whole 1/30 s is due (the frame that brings the budget within half
a step), so at 60 FPS the script runs on every second frame even when a frame is a little shorter than 1/60 s.
Inputs that are true for one frame are kept for the next run. While a ticked script runs, the frame-step cell holds
1/30 s, so every fix that scales by the step sees a whole tick.

The default is `0x4`, Braviary alone. Without it Braviary turns several times too fast above 30 FPS, because its
flight script sets a new rotation on every update. The script sets that rotation through `main+25AB490`, so with
the Braviary bit the wrapper blends the rotation from the previous one to the new one over the time to the next
run and stops as soon as anything else writes the rotation. The time to the next run is taken from the tick budget by
the same rule as the tick itself. An earlier form ended the blend a frame early at 60 FPS and the turn was drawn in steps. The user's words after the two steps: "braviary turning
feels right now", then, with the blend, "braviary turning feels smooth now".

`SNX_ARCEUS_SCRIPT_WINDOWS` is a mask of windows in which a class is ticked only while a named part of its script
runs. Bit 0, on by default, is the throw. The player's throw script waits at most 19 updates for the release event
of the throw animation and then gives the throw up, so above about 60 FPS a slow throw never left the hand. The
window opens when the script calls `main+1A91624` and closes at `main+1A8EBA4` or `main+1A91370`, or after one
second, or when the state that opened it stops being updated. Inside it the three player classes run 30 times a
second, and the release event, a short pulse, is kept for the script's next run. Outside it the player script runs
on every frame as before. In play the slow throw came out every time in four sessions.

Limits. The other ride bits were never played and are off. The player bits as a plain mask (`0x380`) made strafing
and rolling choppy, which is why the throw uses a window. The Sneasler bit refuses while the ledge fix owns the same
two bridges. During a ticked script run the shared frame-step cell holds 1/30 s. The game runs its frame on three
threads, so another reader could meet 1/30 s. In 1844 ticked runs the day clock never did. No other reader is
measured. Only the rotation that
`main+25AB490` sets is blended. The throw wait is the only window, and other waits counted in script updates may
exist. The kept release event is asked for again only while the same state object is updated on consecutive frames
and the object's first word is unchanged. An earlier form that asked more queries crashed on a released object.
