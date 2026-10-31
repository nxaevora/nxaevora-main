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

## Status of the work (paused by the user's decision)

The investigation is closed for now. The stick trace is in its own patch (`recomp/patches/0003-stick-trace.patch`, apply
it after `0001`) and is not in the runtime now. To go on, apply it and set `trace_stick=1`. Then read the large reader 0x99ea4b8 and the readers of the persistent copies
(0x155872b40, the three objects of 0x99510b4, the six copies of 0x993ed00), and run the trace with a shorter watch time or
with fewer protected pages so that the game does not run at a crawl. The game-code patch `camera-unclamp.pchtxt` had no
effect on the camera and is removed from the game (the copy in `recomp/patches/` stays as a record).
