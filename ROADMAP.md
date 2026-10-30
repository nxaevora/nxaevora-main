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

## Phase 1: Understand the misses (in progress)

The update program had 17 different misses in a run of 84 seconds, and the base program had 19 in the boot. Most
are inside functions. A few are PLT stubs.

- Done: read how ScarletNX decides where a miss can happen (`docs/jit-analysis.md`).
- Done: verify one miss as a jump-table target. The recompiler does not read the entries of a jump table.
- Next: scan the jump tables of the main module and of sdk, and compare their case targets with the misses.
- Next: find the instruction that causes each miss that is not a jump-table target. The runtime does not log it.

Exit condition: every miss of the update program in a boot run has a known cause.

## Phase 2: Remove the misses of the boot (the first goal is reached)

Result: with the targets of all jump tables as hints (45728 pcs from `tools/jump_table_scan.py`), the opening scene
at sea ran for 252 seconds with 0 misses. Before, the same scene had 114 misses. Only this scene is tested.

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

## Phase 3: Cover the game

The user plays a scene. Claude reads the log and `<id>.hints` of the run. Each round adds the new pcs to the hints
and recompiles again.

- Scenes to cover: the title screen, the start of the story, the village, the wild areas, battles, menus, the save
  and load flow, and the cutscenes with video.
- Covered so far, with 0 misses: the opening scene at sea, the first menus with the software keyboard, the write of
  a save, the load of that save, a play of 7 minutes to the entry of the village with a capture, and a play of
  13 minutes to the point before the Obsidian Fieldlands with a battle against an NPC. Not tested yet: the
  Obsidian Fieldlands and the other wild areas, battles against wild Pokemon of the later areas, and video.
- The number of misses for each scene is the measure. It is written down after each round.
- Findings about the game (crashes, wrong rendering, missing services) go to `docs/`.

Exit condition: a long play session in each scene ends with 0 misses.

## Phase 4: Remove the JIT

ScarletNX always keeps the JIT. A miss is compiled and run, and the game goes on. A build without the JIT would stop
at the first miss, so the list of misses must be empty first.

- Decide how to build without the JIT: a setting in ScarletNX, a patch, or a check that fails when the JIT is used.
- Check that the game still runs when a miss is a hard error.

Exit condition: the game plays through the covered scenes with the JIT turned off.

## Phase 5: Quality of the port

- Rendering: look for wrong or missing effects in the Maxwell to Vulkan path.
- Speed: the boot run shows about 30 frames each second. Find out whether the game limits it or the runtime does.
- Shaders: the boot run reports 80 of 1787 shader programs as failed. The cause is not known. Every run also
  reports one shader that is not translated because of the instruction `RED`, and one texture of format 0 that is not
  drawn. The user sees no visible fault from these in the tested scenes.
- Stutter: the only visible problem so far is a stutter when a pipeline is made for the first time. A run of 420 s
  made 100 pipelines (3.8 s making, 2.5 s of waiting). The runtime keeps its pipelines in `shader_cache/` and makes
  them again in the background at the next start. A run of 794 s made 67 new pipelines (2.1 s of waiting). Look for
  a way to make the pipelines ahead of time.
- Audio, video, input and save data.
- Services of the console that Arceus needs and ScarletNX does not have yet (`runtime/hle`).

Exit condition: the covered scenes play at the speed and the quality that the user accepts.

## Phase 6: Launcher

A launcher made with Tauri (`launcher/`).

- The user chooses the game dump, the update and the keys. The launcher never copies or uploads them.
- It starts the recompile, shows its progress and starts the game.
- It lists the settings that matter, for example the language and the resolution.
- It ships with the runtime DLLs, so that no other tool must be installed.

Exit condition: a user with a dump and keys goes from a fresh install to a running game without a terminal.

## Phase 7: Release

- Choose and write down the license (GPL-3.0-or-later is the plan, because ScarletNX is GPL-3.0).
- Add the credits for the projects whose code or research NXAevora uses.
- Build the release in CI from a pinned ScarletNX commit.
- Write the README for users, with the legal notes: the game files are not part of NXAevora and the user must dump
  them from their own console.

## Open questions

- Is a build without the JIT possible with a setting, or does it need a change in ScarletNX?
- Which Arceus versions are supported? The work so far uses 1.1.1.
