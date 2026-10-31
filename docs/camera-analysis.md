# Camera analysis

Goal: find the code of the game that turns the right stick into camera rotation, so that a patch can add the mouse
motion to the camera angle directly. Status: paused. The path of the stick from the pad to many copies is found. The camera code is not found yet.

## Why a patch of the game is needed

The runtime can only give the game stick values. The game turns a stick value into a rotation speed with a maximum, so a
fast flick of the mouse cannot turn the view faster than the full stick. See `docs/input-analysis.md`.

## How to read the imports of the game (a correction)

The game code calls the SDK in another module through PLT stubs in `main`, 16 bytes each (`adrp x16`, `ldr x17`, `add
x16`, `br x17`). A search for calls to the address of an SDK function finds nothing. The method that works:

1. Read the MOD0 header of `main` (offset 4 of the image gives its place) and its dynamic section.
2. Take `DT_JMPREL`, `DT_PLTRELSZ`, `DT_SYMTAB` and `DT_STRTAB`. Each 24-byte relocation gives the GOT slot and a symbol
   index, and the symbol gives the name.
3. A stub names the GOT slot it loads, so each stub has the name of its function. 816 stubs are named this way.
4. Search the `bl` and `b` instructions that target a stub.

The scripts are in the session scratchpad and are not kept in the repository yet. Move them to `tools/` (for example
`tools/import_stubs.py`) when the work goes on.

## What is found

- The game calls `nn::hid::GetNpadState` for the styles FullKey (0x8ca2b60), Handheld (0x8ca2e00), JoyDual (0x8ca308c),
  JoyLeft (0x8ca32d0) and JoyRight (0x8ca3500). It calls `GetSixAxisSensorState` (0x8ca6210) and
  `GetSixAxisSensorStates` (0x8ca62a0) once each. It never calls the mouse or the keyboard functions.
- The functions at 0x8ca2b34 to 0x8ca3600 are the per-style update of one pad class. Each one reads the state, divides
  the four stick values by 32767.0 and stores them as floats in the pad object: left stick at +0x18 and +0x1c, right
  stick at +0x20 and +0x24. Buttons are at +0x8. It then calls 0x8ca29d8, which makes a direction mask from the sticks
  (a stick used as a D-pad), and it sets button bits for it.
- The update functions have no direct caller. They are called through a table of function pointers (a vtable), so the
  next step cannot be a plain search for `bl`.
- A search for tiny accessors that return the right stick (`ldr d0, [x0, #0x20]` and `ret`) finds nothing. The camera
  reads the pad object in another way.

## Found in the second pass (static)

- The pad classes have vtables in `main` at 0xc0f2408 (FullKey), 0xc0f2478, 0xc0f24e8, 0xc0f2558 and 0xc0f25c8 (the
  other styles) and 0xc0f2638. The pointers are not in the image. They are R_AARCH64_RELATIVE relocations in the table
  at `DT_RELA` (205024 entries), and the addend is the target. Read that table to find vtables.
- Slots of the FullKey vtable (from 0xc0f2408): 0 and 1 are the destructors, 2 is the update (0x8ca2b34), 3 returns the
  button word at +0x8, 4 and 7 return a pointer to the right stick floats (this+0x20), 5 and 6 return a pointer to the
  left stick floats (this+0x18).
- The constructors (0x8ca4d50 and the next ones) make a 0x40 byte object with the allocator, put the pad in a list of
  at most 16 pads (the list is reached through a function at 0xb2a8880), and store the config floats 0.8095 (+0x2c) and
  0.5 (+0x30). These are probably the thresholds that 0x8ca29d8 uses for the stick as a D-pad.
- A function at 0x92f8f40 reads a stick pair through a helper (0x8c9f494, which returns this+0xd8) and compares its
  squared length with 0.04. So that code uses a dead zone of 0.2 of the stick travel. It is a candidate for the menu
  or the player controller. Not checked.
- The virtual calls through slot 4 (offset 0x20) and slot 7 (offset 0x38) cannot be searched by pattern. There are
  48724 virtual calls at offset 0x20 and 3090 at 0x38 in `main`, because every class has such a slot.

## Result of the static path

The static path finds the producer of the stick floats but not the camera. The consumers go through virtual calls and a
pad manager, and the haystack is too large to read by hand. A better tool is needed to see who reads the object.

## Not known

- Which code reads the right stick floats of the pad object, and what it does with them.
- What the game does with the gyro, and whether the camera or the aiming reads it.
- The rotation speed limit of the camera. The pad dead zone seems to be 0.2 where it is checked (see above).

## Next step: an access tracer in the runtime

The host is Windows, where the runtime has no memory traps ("no traps on this host" in the log). A diagnostic can still
protect the host page of the right stick floats with `VirtualProtect` and log the guest function of each access in the
exception handler. The crash report already names the guest function of a faulting host pc. The plan:

1. A debug setting (for example `trace_guest_page=ADDRESS`) that makes the page no-access, and a handler that logs the
   guest function and the access (read or write), restores access for one instruction and protects the page again.
2. The runtime knows the address of the pad object only at run time. The first run finds it by writing a known right
   stick value (for example 0.4321) and searching the guest memory for that float pair. The second run traces it.
3. The user runs one scene (for example the opening scene at sea or the first walk) with the camera stick pushed. Claude
   reads the log, gets the camera function, and then writes the patch of the game.

This is a change of the runtime (`third_party/ScarletNX`), kept as a patch in `recomp/patches/`.

## Result of the stick trace (run of 2026-10-31, patch 0002)

The right stick was held at 14158 4043 from 121 s. The search found 32 matches of its two floats in the guest memory
(3 in the input manager object at 0x1461d3808 to 0x1461d3820, 29 in a table at 0x146a75028 to 0x146a752c0 that is
written twice by one function). The reads and writes in 10 s:

- Writer: game function 8c9f9b4 (178 times) writes the three words of the input manager and reads them again. It is
  the manager's per-frame update, next to the helper 0x8c9f494.
- Readers: game function a50d75c (267 times, match 0) and 96f41a8 (276 times, match 1), about once per game frame.
  Both are input distributors. They take the manager's snapshot (the sticks at +0x130 and +0x138, a second pair at
  +0x140 and +0x148, the button words at +0x110 and +0x118) and give it to the handler of the current mode (a switch
  on a mode field at +0x70 of the receiver, 0 to 7). The camera is in one of these handlers. It is not found yet.
- Writer 8c9c3ac fills the table at 0x146a75028 (1 or 2 times, not per frame): not input.
- The host offsets in the log (+97b, +287) are offsets in the recompiled function. They do not map to a guest
  instruction. The function that the log names is the nearest start below the host pc, so it may be a neighbour of
  the real one.

So the game has an input manager with a snapshot and distributors with handlers, and not a camera that reads the pad
object directly. The next step to find the camera handler is to follow `bl 0x97ff23c` (called by 96f41a8) and the
`bl 0xa50aff8` / `0xa50a4f0` calls of a50d75c, or to run the trace with a more precise report (the guest return
address of each reader).

## Experiment: a stick value past the range (patch 0003)

The HLE (`hid.c`) does not limit the stick value, and the game divides the s32 by 32767.0. If the camera does not clamp
the value, a stick of 3.0 turns the camera three times faster than a full stick. The setting `mouse_overdrive` (100 to
1000 percent) lets the mouse look send such values. The user tests it with `mouse_overdrive=300`. If the camera
turns faster, the mouse look needs no patch of the game code. If it does not, the game limits the value and the
patch of the camera handler is needed.

## Second and later trace runs (patch 0001, trace with call chains and stack search)

- The input goes through three places only: the manager update (0x8c9f9b4, main thread, once per frame: it sums the
  pad sticks per device, clamps each component to -1.0 and 1.0, and writes the manager fields +0x130, +0x140, +0x148),
  and the two distributors a50d75c and 96f41a8 (worker thread, once per frame). The pad object (0x8ca2b34 writes it, the
  getter at 0x8ca38e0 reads it) is read by the manager update only.
- A test with the clamp limits set to 8.0 and -8.0 (`camera-unclamp.pchtxt`, recompiled) and `mouse_overdrive=300` did not
  change the camera speed. So the camera does not take its stick from the clamped sum.
- The distributors fill an input state structure on their stack (size about 0x58). The two stick pairs are at +0x24 and
  +0x2c of it. The structure goes to the listeners of a signal: the code at 0x97ff23c stores it in the receiver at
  +0x7c and calls each listener through the virtual slot at offset 0x140 with the structure in x1. Copy functions of the
  structure (0x90114d8, 0x9253a??, 0x92a88e0 stores it at +0x80 to +0xc8 of its object, sticks at +0xa0) exist, with
  no vtable slot that points to them.
- The call chains are short: thread entry, the task loop 0xa96d538, the manager 0x8c93e88 on the main thread. They do
  not name the camera.
- The stack search (to find the copy of the structure while it exists and then watch it) found nothing in three runs.
  The reason is not known. A run with the first eight searches logged shows the same main-thread sp, so the searches did
  not run on the worker thread in the logged part.

## Where the work stands

The camera handler is not found. The static and the trace methods reached the distribution point of the input but not the
listener that turns the stick into camera rotation. Options for the next step:

1. Find the camera angle in memory: hold the stick at two values, snapshot the guest memory floats, and keep the floats
   whose rate of change follows the stick. The camera yaw is one of them. Then trace the writers of that float.
2. Follow the signal: log, in the distributor, which listener objects are in the list at [receiver + 0xc0], [0xe0] and
   [0xf0] (the virtual slot 0x140 of each). The listener of the camera is one of them.
3. Keep the stick-based mouse look and tune it (smoothing, the dead zone offset, a response curve). It stays limited by
   the camera's top speed.

## Listener probe (run of 2026-10-31 14:22)

At the first reads of the distributor a50d75c, its receiver object (x19 of the CPU state, 0x152b68c40 in that run) held
eight listener objects at +0x80 to +0xf0, with slot 0x140 code at 0xa567034, 0xa5535f8, 0xa525d8c, 0xa5a9510, 0xa588e4c,
0x99f9c4c, 0xb8aa098 (not code: the vtable is shorter than slot 0x140) and 0x98ad434. The receiver of 96f41a8 shares the
listener with 0xb8aa098. The functions at the first four addresses return a bool and take no input structure: slot 0x140
is a query (for example "is the input enabled"), and not the input callback. The game has no RTTI names to read.

The input structure goes down by the signal thunk 0x97ff5cc (listener object, structure): that function walks three
more lists (the object's +0xc0, +0xe0, +0xf0), so the structure passes through nested signals before the consumer that
makes the camera move. The consumer is not found yet.

The next trace run watches the stick words of the structure in the distributors' own frames (a50d75c at sp+0x6c and
+0x74, 96f41a8 at sp+0x8 and +0x10), and the readers of them are logged with their call chains.

## Frame watch run (2026-10-31 14:34): the chain of copies of the stick

The first scan had filled all 128 words with a table (about 120 entries of the same value), so no stack word could be
added. A limit of four matches for each page fixed this. With 36 matches and the stack words of the two distributors
(a50d75c at sp+0x6c and +0x74, 96f41a8 at sp+0x8 and +0x10) one frame of readers and writers was logged (the page
protection slows the game to a crawl, so each place shows once). The chain:

- a50d75c copies the structure's stick pair to a persistent object (0x14a122f48), 99f48d8 reads it and copies it on
  (0x155872b40), and 99ea4b8 (a large function) reads that copy.
- 99510b4 reads the stack pair (3 times) and writes three similar objects, 0x13000 bytes apart (0x15046b3c8, 0x15047a3c8,
  0x15048d3c8).
- 993ed00 reads the stack pair of 96f41a8 and writes six more copies (0x15030c2ac, 0x15030c40c, 0x15030c56c, 0x14ef773f0,
  0x14ef77e90, 0x14ef77f30).
- 994104c reads a stick pair at +0x38c and +0x390 of its object. It applies a dead zone (the parameter at [x1]), an easing
  curve (a table of functions, index at [x1+4]) and a ramp: two accumulators at +0x434 and +0x438 grow by
  |stick| times a rate (+0x424, +0x42c) while the stick is past a small threshold, are limited to 1.0, and fall by +0x428
  and +0x430 when the stick is back. It looks like the auto-repeat of a menu stick, and not the camera. Not verified.
- 9a064c0 only copies a large structure.

The camera is one of the readers of these persistent copies. It is not named yet. The other candidates are the readers of
the six copies of 993ed00 and the three objects of 99510b4, which this one frame did not show.

## Angle search (2026-10-31, patch 0011, built, run pending)

The search for the readers of the stick copies found many copies and no camera. The new method looks for the result and
not for the input: the camera angle is a float that follows the stick. `trace_stick=2` (after `0003`) holds the right
stick x at 20000 in steps and keeps the floats of the writable guest memory by these tests:

1. A snapshot at rest (every float with an absolute value from 0.01 to 400).
2. The stick held: the float changes (1.5 s), then changes again (1 s), and then by the same amount again (0.7 to 1.3
   times, same sign). A timer or a random value does not pass.
3. The stick back: after 1.5 s the float does not change in the next second.
4. The opposite hold: after 1.5 s the float changes the other way, by 0.5 to 1.5 times the first change.

The survivors are logged (`angle: candidate N at guest ADDRESS, value, change in a step`). Their words (128 at most, 48
pages) are then watched for 15 s with the stick held, and the game functions that write them are in the summary
(`trace: write of match N ... by game function F`). The function F is the camera update, or its neighbour. A static
reading of 0x99ea4b8 (the large reader in the chain) showed 2 float operations only, so it is not the camera.

Run: the game stands in an open place, no menu, no input from the user for about 12 s after the start time.
Limits: the camera must not be locked to a target (a lock-on or a fixed camera shows no candidate). An angle that is
kept as a quaternion or a matrix can fail the tests of step 2 (it is not linear). Then try the pitch, or a yaw only
camera mode.

### Run 1 of the angle search (2026-10-31 19:31, stick x only)

Of 29203075 floats at rest, 3 stayed after the seven steps:

| Candidate | Guest address | Value | Change in a step | Writers in the watch |
| --- | --- | --- | --- | --- |
| 0 | 0x90e9c3920 | 0.777 | +0.262 | 0xd9548a0 (no float operation: a copy), host code of the runtime |
| 1 | 0x13000f9230 | -0.226 | -0.565 | 0x8080038 (766 float operations, a large function with matrices and rsqrt: a physics or sweep step) |
| 2 | 0x1325e66318 | -14.4 | +2.47 | none in 10 s |

The watch (page protection) slowed the game to 2 to 4 frames a second, so the counts are not per frame. No candidate is
shown to be the camera. Candidate 2 fits a yaw in radians (141 degrees a second at 0.61 of the stick) but nothing
wrote it. The run 2 version holds the stick x and y, logs the values of the last candidates at each step, counts the
candidates within 0x100 bytes of each (the yaw and the pitch of one camera lie together), and dumps the registers at
the first writes.

### Run 2 (2026-10-31 19:44, stick x and y together)

184 candidates stayed, and they are copies. The groups: about 50 words near 61.14 to 61.21 (change -0.36 to -0.46 a
second, with a pair at 50.6 and 71.65 beside some of them), 10 words of 0.6778 and 10 of 61.334 (change -0.1728, all
four-float splats in the region of guest 0x13000f6a10 to 0x13000fa944), 37.04 and 65.63 (change 5.4 to 6.4), and three
copies of -0.980 (change -1.97, a value that crosses zero). Writers once a frame (31 or 32 times in the 10 s at the
slowed speed): 0xa9ccd60, 0x99437c0, 0xadf7694, 0x9d87e30, 0x98c56c0. Readers: 0xd901cf8, 0xad793d8, 0xae01d10, 0xad77338,
0xadfe850, 0xadf5870, 0xad44550. Nothing shows which one is the camera, because the x and y holds ran together.

The run 3 version adds an x only hold and a y only hold at the end (steps 8 to 11) and calls each candidate `yaw` (the x
hold moves it at least 3 times more than the y hold), `pitch` (the opposite), `both` or `none`. Only the `yaw` and
`pitch` words are watched (the `both` ones when there are none).

### Run 3 (2026-10-31 19:50): the camera is found

With the x only and y only holds, 66 of 70 candidates were classed. The pitch class holds the many copies of 61.04
(change -0.46 a second). The yaw class holds -3.742 (change -2.10 radians in the 1 s x hold, about 120 degrees a second
at 0.61 of the stick) at 3 words, 0x15051d360, 0x15051d590 and 0x15052ad80, which are the offsets +0x360, +0x590 and
+0xdd80 of one object (base 0x15051d000 in that run, it changes from run to run). The writer is 0x99437c0.

The camera controller (all offsets from the object, x19 in the code):

- 0x99437c0 is the angle setter, `void set_angle(Camera* x0, float s0)`. It stores the angle in `[x0+0xdd80]` and
  `[x0+0x360]`, calls 0x9808f90(x0+0x5b0, d0 = `[x0+0x700]`) to rebuild the orientation, and tail paths on `[x0+0x3e4]`
  (mode 1 or 2) and `[x0+0x54c]`.
- 0x994216c is the stick update. It reads the stick pair at `[x0+0x418]`, `[x0+0x41c]` through 0x994104c (dead zone, easing
  and the ramp accumulators `[x0+0x434]` and `[x0+0x438]`, the auto repeat that was seen before). The yaw step is
  `s1 = s8 + s11*s12*sign*[x0+0x420]*s0` and the new angle is `[x0+0x360] + s1` (0x9942314). The pitch is a double
  `[x0+0x700]` in 0 to 1 (the position on the camera's arc): `[x0+0x700] += step`, limited to 0 and 1 (0x99422d8 to
  0x99422fc). With the stick idle the function jumps to 0x9942230 and tail calls the setter with the angle unchanged,
  so the setter runs once a frame in any case.
- 0x993ba64 is the second caller of the setter (an angle from a direction vector).

So a mouse look needs no stick: the mouse delta is added to the angle in the setter. The yaw is added to the angle
argument, the pitch to the double at +0x700 (limited to 0 and 1).

### The native mouse look (patch `0012`, `recomp/patches/native-mouse-look.pchtxt`)

The code patch (IPSwitch text) puts a branch at the start of the setter (0x99437e8, the `mov v8.16b, v0.16b`) to 18
words of code. The code takes one 64 bit word at 0x9943980 with an atomic exchange against zero (two floats: the yaw in
radians, then the pitch in units of the arc), adds the yaw to the angle, and adds the pitch to the double at +0x700 with
the limits 0 and 1 (`fmin` and `fmax`). The runtime adds the mouse motion to the word as it arrives
(`mouse_native_address=160708992`, which is 0x9943980; settings `mouse_native_yaw` and `mouse_native_pitch`, in millionths
for each mouse count), and keeps the stick at rest. It needs a recompile of the game with the mod in
`<id>_data/mods/native-mouse-look/exefs/`. Not tested yet.

The first version put the code in the zero padding after the end of the text segment (0xb2aa690) and the game stopped
there: "reached code at b2aa690 that the recompiler missed ... the JIT could not compile it". The recompiler and the JIT
take code only inside `text_size` of the NSO header (a patch cannot change the header), and the JIT refuses an image
that lies in the range of the main dispatch table (`snx_jit_add_image`). The text has no hole of 16 bytes (zero or nop), and
all 816 PLT stubs have callers. The space comes from the setter itself: its mode 1 path (0x99438f0) ends with a copy of the
end of the mode 0 path (0x9943820 to 0x994386c and the tail). The patch makes mode 1 set `s8` from `w21` and jump to
0x9943820, which frees the words 0x9943938 to 0x9943990 (88 bytes) for the 72 bytes of code and the 8 bytes of data.

## Status of the work (paused by the user's decision)

The investigation is closed for now. The stick trace is in its own patch (`recomp/patches/diagnostics/0003-stick-trace.patch`, apply
it after `0001`) and is not in the runtime now. To go on, apply it and set `trace_stick=1`. Then read the large reader 0x99ea4b8 and the readers of the persistent copies
(0x155872b40, the three objects of 0x99510b4, the six copies of 0x993ed00), and run the trace with a shorter watch time or
with fewer protected pages so that the game does not run at a crawl. The game-code patch `camera-unclamp.pchtxt` had no
effect on the camera and is removed from the game (the copy in `recomp/patches/` stays as a record).
