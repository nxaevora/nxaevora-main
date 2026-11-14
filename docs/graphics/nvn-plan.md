<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Phase 6 plan: NVN on the host, drawn with Vulkan

This is the plan for phase 6 of `ROADMAP.md`. It has five stages. A stage ends when its exit condition is true. The next
stage does not start before that. The plan is a proposal until the user accepts it.

## Where we start

- The game calls NVN functions through a dispatch table. The NVN library is inside the game. It writes methods into
  push buffers, and the runtime reads them from the GPFIFO (`src/nx/gpu/maxwell/`, about the console GPU engines and
  the macros in `mme*.c`). The Vulkan side is in `src/nx/gpu/vk/` (`render.c`, `pipeline.c`, `record.c`, `surface.c`).
- `nvn_trace=1` (`src/nx/gpu/nvn/diag/nvn_trace.c`) already lists the NVN functions that the game resolves and counts the calls.
  It wraps the dispatch table entries. It does not see direct branches, so its counts are lower bounds. Calls that the
  game makes without the table are not seen.
- The translation of shader binaries (`src/nx/gpu/shader/`) and the pipeline cache are independent of the command
  stream. They stay.

## Stage 1: Survey the paths of Arceus

Content: the NVN API that Arceus uses, the inline work (data written into memory by the CPU and read by the GPU, for
example uniform and texture descriptors), and the extra commands that the queue adds by itself (fences, flushes,
present, the commands of the library that the game never calls by name).

- Run `nvn_trace=1` in the scenes of the roadmap and write the list of functions with the call counts in `docs/`.
- Find the paths that the hook does not catch. Compare the GPFIFO submissions and the draws that the runtime sees with
  the calls that the hook counts. A gap shows a path that does not pass through the table. Recompile with
  `--hook-direct-calls` if a gap needs it.
- For each object type (device, queue, command buffer, memory pool, texture, sampler, program, sync, window), write which
  state lives in guest memory and which state the game reads back.

Substeps:

- 1-1 List of functions: run `nvn_trace=1` in the boot, title, village, wild area, battle and menu scenes. Write the functions
  and the call counts in a table. The user runs the game, the agent reads the logs.
- 1-2 What the hook misses: compare the GPFIFO submissions and the draws with the hook counts. Decide if a recompile with
  `--hook-direct-calls` is needed (it takes about 11 minutes).
- 1-3 State of the objects: for each object type, where its state lives in guest memory and what the game reads back.
  This is read from the code and the disassembly. The game does not run.
- 1-4 Inline work and extra queue commands: the descriptors that the CPU writes for the GPU, and the commands that the
  library adds by itself (fences, flushes, present). Also read from the code.
- 1-5 Summary: `docs/graphics/nvn-survey.md`, with the parts that are hard to replace, as input for stage 2.

Exit condition: all drawing paths that the hook does not catch are known.

## Stage 2: Choose the way of replacement

Content: compare two ways, and decide.

- A: replace NVN completely. The host implements every NVN function the game calls and the library of the game is not
  used.
- B: replace the existing Maxwell layer in part. The library of the game stays. The runtime turns the commands of some
  parts (for example draws and clears) into Vulkan calls at a higher level than the register writes.

For each way, write what is replaced, what stays, the size of the work, the risk of visual faults and the patch size
against upstream.

The comparison and the recommendation are in `docs/graphics/nvn-design.md`. Decision of the user: way A.

Substeps (the game does not run in 2-1 to 2-5, the results go to `docs/graphics/nvn-design.md`):

- 2-1 Raw reads: find the code of the game that reads or writes the memory of a buffer, texture, command buffer, memory pool
  or window object without a call. Method: the call sites in the layer a9b82e0 to ac8714c, and the stores and loads through
  the registers that hold an object pointer. Result: a list of fields and offsets that the host layer must keep. Also find
  the exact object sizes at the places where the game reserves the memory.
- 2-2 Addresses and handles: read what the game does with the result of `nvnBufferGetAddress`,
  `nvnMemoryPoolGetBufferAddress` and `nvnDeviceGetTextureHandle` (where it stores them, which shaders read them). Result: the
  rule for GPU addresses (keep the addresses of `nvdrv.c`, or new ones) and the format of the 32-bit texture handle.
- 2-3 Where the host layer attaches: how the dispatch table is built (`nvnBootstrapLoader`, `nvnDeviceGetProcAddress`, the
  wrapper in `nvn_trace.c`), how a host function replaces an entry, and where the setting `nvn_native` is read. Result: the
  place of the switch and the order of start (the device must exist before the table answers).
- 2-4 What the host layer calls: decide if it calls the `Renderer` interface (`gpu/renderer.h`) or `gpu/vk/` directly.
  List which `Renderer` calls a host draw, clear, copy, compute dispatch and present would use, and which are missing
  (no register state in the new path). Result: the list of new functions that the Vulkan side needs.
- 2-5 Design of the three hard parts, one short section each: (a) the recorded command buffer that is called again (what a
  recording holds, how `CallCommands` replays it, what state it needs); (b) the frame cycle (acquire, submit, fence,
  present) and the work of the library prologue and epilogue; (c) memory: the guest memory as the truth, write tracking
  (`gpu_mem_written`) and the pending pages, and the syncpoints and semaphores that the game polls.
- 2-6 Order of the work for stage 3: the list of the functions of the smallest drawing path (from the survey: the first
  clear and present), with the files to create. Also the test plan (what the user runs, what the agent reads in the log).
  The user accepts the result before stage 3 starts.

Decision of the user (2026-11-02): the handlers write the engine methods on a private channel and call the existing `Renderer`
(choice 1 of `docs/graphics/nvn-design.md`, section 2-4). The native draw interface (choice 2) is a candidate for a later clean-up,
judged after stage 4. The rules for the side effects of the register path, the update notifications, the lifetime and the
synchronization of resources, and the taps for per-draw information (TAA and other effects) are in that section. Stages 3 and 4
follow them.

Step 2-6 is written in `docs/graphics/nvn-design.md` (section 2-6). The user accepted it on 2026-11-02, so stage 2 is finished and stage
3 starts with step 3a.

Exit condition: the range that is replaced and the work that remains are explained.

## Stage 3: Smallest drawing path

Content: connect initialization, a screen clear, one simple draw and the presentation on the new path.

- Pick the first scene from the survey (the title screen or the first clear and present of the boot).
- Keep the old path in the runtime as a comparison target, selected for each launch by the setting `nvn_native` (read once at
  start), so both can run on the same scene in two launches.
- Compare the result with the old path by frame dumps of the screen (local only, never in git).
- Write registers only through the engine method entries (`maxwell3d_method` and the others), never into `r3d[]`.
- Put a tap call at the draw, clear, dispatch, frame start and frame end of the replay, even when no effect uses it yet.
- Start the selftest (`nvn_selftest`, adopted 2026-11-02) with a few representative functions in 3b, 3c and 3d, and show with a
  deliberate mutation that it finds a wrong handler. The selftest and the execution test use their own objects and memory and
  never touch the queue or the state of the game. A pass is recorded as `selftest` or `exec`, separate from `seen` (a game run).
  The rules are in `docs/graphics/nvn-unobserved.md`, section "The selftest".

- Code review (decided by the user, 2026-11-02): use the "code reviewer" agent of the skill `orca-agents` after step 3b and after
  step 3c, and after 3d when the change is large. Check at 3b: the life of objects, the generation check, the GPU address rule,
  the size functions. Check at 3c: the life of recorded lists, the threads (GPU thread and guest threads), the fence tokens
  (`SyncWait` must not hold the GPU thread), the shadow RAM mode at the frame start. Fix the findings before the next step. A
  commit that used the agent lists it (the `Co-Authored-By` line of CLAUDE.md and one line of the work in the message body).
  Step 3a (the trace) needs no review.

Exit condition: the new path and the old path give the same drawing result for this scene.

## Stage 4: Extend to the real game

Content: textures, compute, synchronization (fences and events) and memory sharing between the CPU and the GPU
(memory pools, write-back, pending pages).

- Add the features in the order that the survey gives, one scene each. After each one, run the scenes of
  `ROADMAP.md` phase 3 and read the logs (`nx-log-read`).
- Shaders and pipelines keep the existing path until phase 8 moves them.
- The 242 functions that the survey run called are not the limit. All 523 resolved functions get a handler, and the argument
  values that no scene used are checked. The plan, the coverage file and the checks are in
  `docs/graphics/nvn-unobserved.md`.

Exit condition: in the target scenes, the drawing and the behavior match the old path in a comparison, and each of the 523
functions is implemented and checked as the coverage file says.

Status (2026-11-14): both parts are true. The comparison of Obsidian Fieldlands and a battle by the draw record found no
difference between the two paths (`nvn-design.md`, "Stage 4 compare: the draw record", "Result"). All 523 rows of the coverage
file are `implemented` with a check or a reason (`nvn-unobserved.md`, 4-u3-5 and 4-u4).

## Stage 5: Measure performance and quality

Content: CPU time, frame time and dropped draws, compared with the old path.

- Use the debug window and the profile output that exist. Measure the CPU time of the command decode that the new path
  removes.
- Check for missing draws, wrong effects and new crashes. Run each fixed crash many times (one good run is no proof).

Exit condition: the effect and the limits of the replacement are confirmed. Then decide if the old path is removed.

## Rules for all stages

- The user runs and tests the game. After a run, the agent reads the files and reports.
- A fix explains the mechanism. No tuning of delays, no retries, no special cases.
- Nothing of the game goes into git (dumps, frame dumps, shader dumps, logs).
- The 30 frames each second and 0 JIT misses are the baseline. A stage must not make them worse.
- A change in `third_party/ScarletNX` goes to `docs/build/runtime-changes.md`.
