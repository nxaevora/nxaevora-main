# NXAevora roadmap

NXAevora aims to run Pokemon Legends Arceus natively on the PC. The code of the game is recompiled ahead of time with
ScarletNX, and the result runs with no emulation of the CPU. This file shows where the project is and what comes next.
It has no dates. A phase ends when its exit condition is true.

The word "miss" in this file means a pc that the recompiled code does not have. When the game reaches a miss, the
ScarletNX runtime compiles the code at that moment (the JIT). A fully static build has no misses.

## Phase 0: Foundation (done)

- The repository, the ScarletNX submodule and the folder layout are in place.
- ScarletNX builds on Windows with MSYS2 CLANG64. The steps are in `docs/SCARLETNX_BUILD-NOTES.md`.
- A portable copy of the recompiler runs without MSYS2.
- The base game and update 1.1.1 recompile in a few minutes.
- The recompiled game boots to the language selection screen and runs without a crash for 45 seconds.

## Phase 1: Understand the misses (closed for now)

The update program had 17 different misses in a run of 84 seconds, and the base program had 19 in the boot. Most
are inside functions. A few are PLT stubs.

- Done: read how ScarletNX decides where a miss can happen (`docs/jit-analysis.md`).
- Done: verify one miss as a jump-table target. The recompiler does not read the entries of a jump table.
- Done: scan the jump tables of all modules. 49 of the 51 pcs that are not PLT stubs are targets of a table.
- Open: 2 pcs are not explained (0x8cbe780 in main and 0xd92f310 in sdk), and the cause of the PLT-stub misses is not
  known. None of them missed again in the runs with the hints.

This phase is closed for now, because the runs so far found no new miss. Open it again when a miss appears that the
scan does not explain.

Exit condition: every miss of the update program in a boot run has a known cause.

## Phase 2: Remove the misses of the boot (closed for now)

Result: with the targets of all jump tables as hints (45728 pcs from `tools/jump_table_scan.py`), the opening scene
at sea ran for 252 seconds with 0 misses. Before, the same scene had 114 misses.

- Feed the recorded pcs to the next recompile and check that the boot run has no miss. `scarletnx -r` reads
  `<outdir>/<ID>/<name>.hints` by itself. The runtime names the file after the program that started. The base
  program runs the base game code and the update program runs the update code. The modules have other addresses in
  each, so a hints file is valid only for the program that wrote it. For the update, the user starts the update
  program.
- Done: the scan of the jump tables is part of the recompile procedure (the skill `nx-recompile`). It makes the
  hints from the game files, so the list is not in git. Other game-specific settings and patches go to
  `recomp/config`.
- Open: the cause is a rule of the recompiler (it does not read the entries of a jump table). The hints are a
  workaround that needs no change in ScarletNX. Decide whether to fix `explore()` in ScarletNX and report it to the
  upstream project.

Exit condition: the boot run to the language selection screen ends with 0 misses.

## Phase 3: Cover the game (closed for now)

The user plays a scene. Claude reads the log and `<id>.hints` of the run. Each round adds the new pcs to the hints
and recompiles again.

This phase is closed for now, because every run after the jump-table hints had 0 misses. Open it again when a miss
appears in a scene that is not tested yet.

- Scenes to cover: the title screen, the start of the story, the village, the wild areas, battles, menus, and the save
  and load flow.
- Covered so far, with 0 misses: the opening scene at sea, the first menus with the software keyboard, the write of
  a save, the load of that save, a play of 7 minutes to the entry of the village with a capture, and a play of
  13 minutes to the point before the Obsidian Fieldlands with a battle against an NPC, and a play of 4 minutes in
  the Obsidian Fieldlands. Not tested yet: long play in the open areas, the other wild areas, and battles against wild
  Pokemon of the later areas.
- The number of misses for each scene is the measure. It is written down after each round.
- Findings about the game (crashes, wrong rendering, missing services) go to `docs/`.

Exit condition: a long play session in each scene ends with 0 misses.

## Phase 4: The JIT stays as a safety net

Decision: the released build keeps the JIT. A miss in a scene that was not tested is then compiled and run, and the
game goes on. A build without the JIT would end the game at the first such miss. Because the JIT stays, LLVM stays
in the runtime and `libLLVM-23.dll` is shipped.

A build with the JIT turned off is still useful as a check, and it is optional.

What the source of ScarletNX shows (`guest_jit.c`, `rtapi.c`, `jit_service.c`):

- No setting turns the JIT off. `jit_init()` always starts it. `jit_threads` can only be 0 (automatic) to 4.
- A miss is already a hard error when the JIT is not running. `rt_miss` then ends the game with the message "The game
  reached code at <pc> that the recompiler missed". The runtime has this path for a JIT that cannot start.
- The smallest change is a setting or a switch that skips `snx_jit_service_new` in `jit_init()`. It is a few lines.
  It would be kept as a patch in `recomp/patches/` and not as an edit of the submodule.
- The game program is the same program as `scarletnx.exe`, with the recompiler and the JIT in it, and it imports
  `libLLVM-23.dll` (154 MB).

The line `jit: ... N misses` of the log shows exactly how often the JIT was needed, so 0 misses in a run means that
the run did not need it.

Optional step: make the patch that turns the JIT off, and let the user play the covered scenes with it. A miss ends
the game and names the pc.

Exit condition: the decision above is recorded. The optional check can be done at any time.

## Phase 5: Quality of the port

- Rendering: look for wrong or missing effects in the Maxwell to Vulkan path.
- Speed: the game limits the rate, not the runtime. In every run the game sets a swap interval of 2 (`vi: swap interval
  2`, one change at about 2 s). The setting `fps_limit=game` makes the runtime hold each frame for 2 refreshes of 60 Hz,
  which is 30 frames each second. The GPU needs about 6 ms of the 33 ms, so the hardware is not the limit. A higher
  rate is a game enhancement (later work) and needs the game's own timing to change with it.
- Shaders: by hash, 11 of the 1379 programs that the game ships fail (see `docs/shader-analysis.md`). The old figure of
  80 counted the same program once for each archive. Six wait for their first draw, because their translation reads a
  constant buffer. Four hit the data of a jump table that follows the program (checked in the files). The last one has the instruction `RED`, and the
  count of skipped draws grew by one in each frame. Patch `0002` adds the form, and a run of 157 s then showed 0
  skipped draws. The one texture of
  format 0 is an empty header that the game binds to the slot of a compute program. The slot reads zeros through a
  null view and no draw is dropped. The user sees no visible fault from these in the tested scenes. A run of 681 s with the bag, the box, the clothes,
  the battle introduction and a large part of the open areas had no shader that failed at its first draw, no skipped
  draw and 0 JIT misses.
- Stutter: the only visible problem so far is a stutter when a pipeline is made for the first time. A run of 420 s
  made 100 pipelines (3.8 s making, 2.5 s of waiting). The runtime keeps its pipelines in `shader_cache/` and makes
  them again in the background at the next start. A run of 794 s made 67 new pipelines (2.1 s of waiting). The
  analysis is in `docs/stutter-analysis.md`: ScarletNX already prepares the shaders ahead of time, and the pipelines
  for new draw states cannot be made before the game uses them. Most slow frames are not from pipelines. The setting
  `async_pipelines=1` removed the wait for pipelines (0 ms in 375 s, with an empty cache), and the user reports clearly
  less stutter. The stop of about 1.2 s to 1.4 s at the start is the game's own loading loop: its main thread starts 39
  pooled objects one after the other and waits for each with a sleep of 16 ms, with no other thread busy and no slow file
  read (see `docs/stutter-analysis.md`, the hitch runs). The runtime's sleeps are exact, so no fix is planned. The cause of
  the other slow frames is not known.
- Audio, input and save data.
- Services of the console that Arceus needs and ScarletNX does not have yet (`runtime/hle`).

Exit condition: the covered scenes play at the speed and the quality that the user accepts.

## Phase 6: Launcher

A launcher made with Tauri (`launcher/`).

- The user chooses the game dump, the update and the keys. The launcher never copies or uploads them.
- It starts the recompile, shows its progress and starts the game.
- It lists the settings that matter, for example the language and the resolution. It sets `async_pipelines` to 1 by
  default (see `docs/stutter-analysis.md`).
- It ships with the runtime DLLs, so that no other tool must be installed.

Exit condition: a user with a dump and keys goes from a fresh install to a running game without a terminal.

## Phase 7: Release

- Choose and write down the license (GPL-3.0-or-later is the plan, because ScarletNX is GPL-3.0).
- Add the credits for the projects whose code or research NXAevora uses.
- Build the release in CI from a pinned ScarletNX commit.
- Write the README for users, with the legal notes: the game files are not part of NXAevora and the user must dump
  them from their own console.

## Candidates for native replacement

The CPU code of the game is native after the recompile. The other layers of the ScarletNX runtime still re-create
the console: they decode its GPU commands, mix its audio, read its input, decode its video, rebuild its memory and
read its files. A native replacement moves one of these layers to code made for the PC.

These are candidates and not decisions. The current runtime works (30 frames each second and no visible fault in the
tested scenes), so each replacement must earn its place. Measure first. Every replacement also moves away from the
upstream project, so a patch must be kept up to date or be proposed upstream.

### Graphics

- Now: the runtime decodes the GPU command stream that the game writes (methods and macros of the console GPU) and
  draws with Vulkan. A shader is translated from its console binary when the game first uses it. A pipeline is made
  at the first draw, which causes the stutter that we see.
- Native: handle the graphics API calls of the game (NVN) directly on Vulkan, and make the shaders and pipelines
  before the play.
- Benefits: no command stream to decode, less CPU work. The pipelines can be ready before play, so the stutter goes.
  Room for PC options such as the resolution.
- Drawbacks: the API is large, and the game uses it in its own ways, so each call must be written and tested. The
  stream emulation works now, and a new path can add visual faults. The translation of the shader binaries stays. It
  is a large patch to keep against upstream.
- Step 1, small: ScarletNX makes the shaders ahead of time and the pipelines of earlier runs at the start. A pipeline
  for a new draw state cannot be made before the game uses it. What is left is the setting `async_pipelines` and the
  other causes of slow frames (see `docs/stutter-analysis.md`).

### Audio

- Now: the runtime re-creates the audio renderer of the console (voices, mixing, effects) and sends the result to
  the PC through SDL (WASAPI). Music and voices are decoded with FFmpeg.
- Native: give the voices of the game to a PC mixer.
- Benefits: possibly less CPU work and latency, and options such as volume for each kind of sound.
- Drawbacks: the effects of the game (for example reverb) must sound the same, and the result is hard to check
  without listening. The present path works, and no fault is recorded. Low priority.

### Input

- Now: the pad state of the console is made from the SDL gamepad (an XInput pad is found in the log). The keyboard and
  mouse are mapped to pad buttons and sticks, so the mouse look acts like a stick and feels poor.
- Native: read the keyboard and mouse directly. The mouse moves the camera by its own delta and does not pass through a
  stick. The launcher offers rebinding and profiles.
- Benefits: a camera that is usable with the mouse, and rebinding.
- Drawbacks: the game reads its camera from the stick values, so a mouse delta must be turned into a stick value (or
  into a change that the game accepts). Functions that the console has and the PC may not have (motion, touch) need a
  plan. The work is in `docs/input-analysis.md`.

### Memory

- Now: the runtime re-creates the address space of the game. The generated code uses the addresses of the game, the
  memory calls of the console are emulated, and the pages that the GPU uses are tracked.
- Native: change the addresses of the game to native pointers and use the PC allocator.
- Benefits: possibly faster memory access and simpler debugging.
- Drawbacks: this is the largest and riskiest change. The game keeps addresses in its data. The mapping calls (alias,
  permissions) must act the same. It reaches into the core of the recompiler. The game already runs at 30 frames
  each second with no JIT misses, so the gain is not proven. Do not start without a measured problem.

### Files

- Now: the runtime reads the ExeFS and the RomFS in place from the dump (6 GB, 19095 files) and decrypts them at each
  read. Saves are in the data folder of the game.
- Native: extract the files to the disk ahead of time (ScarletNX has an extract option) and read them as normal
  files.
- Benefits: possibly faster loading, because nothing is decrypted at a read. The dump is not needed to play.
  The launcher can check the files, and mods are easier.
- Drawbacks: 6 GB or more of disk space for a second copy of the data. The extraction takes time and must be made
  again after an update. The copy is for the own use of the user only and must never be shared. The loading time is
  not measured yet, so the gain is not known.

### Decision

A layer is replaced only when a measurement shows a problem. The measurements so far show none for the layers below:
the CPU waits for most of the time, the GPU uses about 6 ms of a frame of 33 ms, and the user sees no stutter while
walking (`docs/stutter-analysis.md`).

- Video: no replacement. The movies of the game are cutscenes that the game engine renders itself.
- Files: no replacement for now. A run with the RomFS extracted still had the stop at the start, so reading the dump in
  place is not the cause.
- Graphics (NVN), memory and audio: not started. Open them again only with a measured problem. Review of this
  decision: the measurements still show none. The 30 frames each second come from the game's swap interval, so the
  layers are not the limit. Open graphics and memory again if a higher frame rate is tried and the CPU cost of the GPU
  command stream decode becomes the limit.
- Open measurement: the stop of about 1.3 s at the start (a wait, not work, see run L). Find the lock or the read that
  it waits on.
- Input: the one layer that is replaced. The keyboard and mouse must work as native input, because the mouse look of
  an emulated pad is poor. See `docs/input-analysis.md`.
- Game enhancements such as the frame rate are later work and are not part of this list.

## Open questions

- Which Arceus versions are supported? The work so far uses 1.1.1.
