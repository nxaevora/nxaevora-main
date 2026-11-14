<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# NVN unobserved functions: coverage and checks (phase 6, stage 4)

Decided on 2026-11-02. This part was in `docs/graphics/nvn-design.md` until 2026-11-11. The design of the host layer
and its decisions stay there.

The run of 2026-11-02 called 242 of the 523 resolved functions, and the scenes were the ones that the user played. A function
that no scene called is not a function that the game never calls. The host layer must answer all 523, and the work has two
parts: the 281 functions that no scene called, and the argument values (formats, targets, flags, modes) that the 242 observed
functions did not get in the run.

### Rules

- Mechanism first. A handler follows what the library function does, read from the disassembly of the `sdk` module (which
  object fields it writes and which engine methods it emits) and from the public NVN API behavior. It does not follow a guess
  from the name. The result of the reading goes into the coverage file below.
- An enum or flag argument that the handler does not support goes to the missing-function handler with the function name and
  the value. It is never mapped to a default. This covers the unobserved values of observed functions.
- The trace (`nvn_trace=1`) logs the set of distinct values of the enum arguments of the observed functions (target, format,
  filter, wrap, compare, blend, topology, index type, and similar). The set is the first input for the check of the argument
  values. The agent reads it from the log of any run.

### The coverage file

`docs/graphics/nvn-coverage.md` (new, made by a script `tools/nvn_coverage.py` from the resolved names and the call counts of the trace)
has one row for each of the 523 functions: name, group (G1 to G10), calls in the last run, and the state of the handler. The
states, in order: `none`, `stub-log` (the missing handler), `implemented` (read from the disassembly, written), `unit`
(a host test passes), `selftest` (the comparison below passes), `exec` (the execution test below passes), `seen` (a game run
called it and the result was read). A row can hold more than one state.

Two rules for the record:

- A passed selftest is not a check in the game. `selftest` and `exec` never set `seen`, and `seen` is set only from a game run
  that the agent read (`nx-log-read`). The file keeps the two in separate columns, and the final report names each one. A
  function with `selftest` and without `seen` is written as "not verified in the game".
- The `selftest` state names its scope: the kinds of comparison that passed (return value `R`, memory writes `M`, state at a
  draw, clear or compute `S`, order of the sync operations `Y`), the argument sets that were used, and the build of the
  library (build ID of the `sdk` module). A change of the build or of the handler clears the state.

### Checks that need no scene

1. Unit tests on the host for every pure part: object setters and getters, enum range checks, size and alignment functions,
   the encoders. A getter is tested against what its setter stored.
2. The comparison against the library (`nvn_selftest`, adopted by the user on 2026-11-02, a diagnostic setting, off by
   default). It runs in one launch of the old path (`nvn_native=0`). The user starts the game, and the agent reads the result in
   the log. See "The selftest" below.
3. The agent reads the library disassembly of each function in G10 and writes in the coverage file what it emits. This is the
   source of the handler and the reference of the selftest.

### The selftest

Idea: the same scripted call goes to the library function and to the host handler. The two results are compared. The library is
the reference, because the real game was made for it.

Isolation (rules, not options):

- The selftest has its own objects and its own memory: a memory area that the host reserves (not the heap of the game), its
  own pools, buffers, textures, samplers, programs, command buffers and state objects. It may read the game's device object
  (for example `GetInteger`), and it does not write any game object.
- It never submits to the queue of the game. A comparison that needs a queue uses its own queue with its own queue memory
  (open point: whether the library lets a second queue exist, checked in step 3b). A comparison that only needs the recorded
  words does not submit at all.
- It never changes the state of the game: the private channel and a recording `Renderer` backend (a test double that logs the
  calls instead of drawing) take the output, and not the channels and the renderer of the game. The selftest runs once, after
  the device exists and before the first frame of the game, and it ends before the first game submission. A failure of the
  selftest is logged and does not stop the game, unless `nvn_selftest_fatal=1`.
- The scripted calls use no game input and draw nothing on the screen.

What is compared, for each function in the scope:

1. The return values (`R`): the result of the call and of the getters of the object that the call changed, for example the
   storage size and alignment, addresses, handles, the values of the status queries.
2. The memory writes (`M`): the bytes that the call wrote into the memory that the game can see. These are the object memory,
   the pool memory (TIC and TSC entries, the descriptors) and mapped buffers. The library's private fields are on an ignore
   list in the coverage file, and each entry on it needs the reason that the game never reads it (`nvn-design.md`, 2-1).
3. The state at the point of work (`S`): at a draw, a clear and a compute dispatch, the library's stream is decoded with the
   existing engine into the recording backend, and the host replay goes into the same backend. The two lists of backend calls and
   the state at each call (targets, viewports, blend, depth, stencil, vertex state, bound textures and buffers, shader program,
   the dispatch size) are compared. This is the check of the result at the time of work, and it does not require the same words.
4. The order of the sync operations (`Y`): the sequence of fences, semaphores, report counters, barriers, waits and signals. The
   library side is read from its decoded stream. The host side is the log of its token and barrier calls. The order must be
   equal. The timing is not compared here (see the execution test).
5. The raw method stream (`W`) is compared as a help for the diagnosis only: a different stream with the same `S` is not a fail.
   The words that differ are shown in the report. A difference in `S`, `R`, `M` or `Y` is a fail.

Report: one line for each function and each argument set (`selftest: <name> <args> R:ok M:ok S:ok Y:ok`), the first difference with
both values for each fail, and the totals. The agent copies the passed scope into the coverage file.

Test of the test: the selftest must show that it can see a wrong handler.

- The comparer has host tests (no game) with pairs of recorded results that differ in one return value, one byte, one state
  field, and the order of two sync operations. Each pair must give a fail with the right kind.
- A setting `nvn_selftest_mutate=<name>` makes the host handler of that one function wrong on purpose (an argument changed). The
  live run must fail for this function and pass for the others. The first run in stage 3 includes this mutation.

### Checks that need a running queue (the execution test)

The selftest does not prove that a fence completes or that the CPU sees the result of the GPU. A separate test does
(`nvn_exectest`, a diagnostic setting, off by default, its own launch, with its own queue and memory and the same isolation rules).
It runs real work on the host path and compares the result with a value that the CPU computes:

- A fence after a clear: `QueueFenceSync` and `SyncWait` return in the time limit, and the order of the signal and the wait is as
  the call order says.
- A clear of a texture and a copy to a mapped buffer: the CPU reads the expected bytes after the wait (the visibility from the
  GPU to the CPU).
- A write by the CPU into a mapped buffer and a dispatch or draw that reads it: the result shows the CPU data (the visibility from
  the CPU to the GPU, with `gpu_mem_written`, `FlushMappedRange` and `InvalidateMappedRange`).
- A wait with a time limit that must run out (a fence that never signals) returns the timeout code.
- Events and `WaitSync` between two queues, when the functions exist in G10.

A pass sets `exec` for the functions in the test. It does not set `seen` either.

Design of 4-u3-4 (2026-11-13). The test needs `nvn_native=1` (the host queue records, the frame jobs), so it is a separate launch
from the selftest (which refuses `nvn_native=1`). It uses the queue fences and flush jobs of `nvn-design.md`, "Queue fences and
flush jobs (4-u3-4)". It starts once, on the game thread, after the first `nvnQueueInitialize` of the game made its host record
(the device is that of the game queue, read only), before the first game submission. It makes its own queues, pools, buffers,
textures, events, syncs and command buffers in its own arena, and calls the host handlers directly. Each wait has a time limit, so
a fault fails the case and does not stop the game. Mutation: `nvn_selftest_mutate` set to a case name changes one host argument.

- E1 fence order: two lists (each a clear of an own texture), a `QueueFenceSync` after each, then `QueueFlush`. `SyncWait` on the
  second fence returns 0 or 1 in the limit, then `SyncWait` on the first with limit 0 returns 0.
- E2 GPU to CPU: a clear with a known color, `CopyTextureToBuffer` into a mapped buffer, fence, flush, wait,
  `BufferInvalidateMappedRange`: the CPU reads the bytes that it computes from the color and the format.
- E3 CPU to GPU: the CPU writes a pattern into a mapped buffer, `BufferFlushMappedRange` (and the pool form),
  `CopyBufferToBuffer` to a second buffer, fence, flush, wait, invalidate: the CPU reads the pattern from the second buffer.
- E4 timeout and failure: a fence behind a pending list without a flush, `SyncWait` with a short limit returns 2. Then
  `QueueFlush`, and `SyncWait` returns 0 or 1 (the job can end before the call). A sync that was never fenced returns 3.
- E5 events and reports: command buffer `SignalEvent` (mode write, then mode add), `ReportValue` and `ReportCounter` (the
  timestamp) into a mapped buffer, fence, flush, wait. `EventGetValue` gives the sum, the report words hold the value and a
  timestamp that is not 0. This case checks the order of the reports and the fence. The CPU `EventSignal` and the event getters
  are checked on the side.
- E6 two queues: queue 1 clears and fences, queue 2 does `QueueWaitSync` on that fence, copies the cleared texture into a buffer,
  then fences with `FLUSH_FOR_CPU` and waits with the limit. Only after that wait passes, `QueueFinish` (which has no limit in
  the host) is called. After the finish, the copy is in the buffer without a further `SyncWait`.
- E7 command buffer fence flush: one list clears a texture and fences a sync inside the list with `FLUSH_FOR_CPU`. The list is
  submitted without `QueueFlush`, and `SyncWait` returns 0 or 1 in the limit.
- E8 queue initialized again: a list on queue 2 and a fence without a flush, then `QueueInitialize` of queue 2 again. The fence
  of the dropped frame fails: `SyncWait` returns 3.

A wait that must signal and runs out of time means that a GPU job may still use the test objects. The later cases are then
skipped, and the cleanup keeps the objects and the pools (the arena is never freed). A case can also stop early after a submit
with no wait, so the cleanup first drains both queues: a fence with `FLUSH_FOR_CPU` and a bounded wait on each. A drain that runs
out, that ends with any result other than 0 or 1 (a refused fence leaves the sync unsignaled, so the wait gives 3), or that
cannot run (a missing handler, no sync left), keeps the objects in the same way.

The GL sync rows (`SyncCreateGLSync`, `SyncInitializeFromFencedGLSync`) fail with "no backend" by design: they get a reason, not a
case.

Result of 4-u3-4 (2026-11-13, update 1.1.1, update program to the title, `nvn_native=1`, `nvn_exectest=1`): 6 cases, 0 failed,
0 skipped, and the game reaches the title at 30 frames a second. The first two runs crashed on a null host handler: the test did
not resolve the event functions, `QueueWaitSync` and `QueueFinish` (fixed in `1546797`). Mutation run with `exec_gpu_to_cpu`: E2
fails ("byte 1: expected ff, actual 00", the clear is left out) and the other five pass. The 61 functions that the test calls have
`exec` in `nvn-coverage.md`.

After the review fixes (`a4b5af4`, same run setup): 8 cases, 0 failed, 0 skipped. Mutation `exec_cb_fence_flush`: E7 fails
(the wait gives 2), E8 is skipped because of that stall, the other six pass. Mutation `exec_queue_reinit`: E8 fails ("expected
3, actual 2"), the other seven pass.

### Introduction in stage 3 (a few representative functions)

The selftest starts in stage 3 with a few functions, so that the harness is proven before 523 functions use it. The set covers
each kind of comparison:

| Step | Functions | Kinds |
| --- | --- | --- |
| 3b | `DeviceGetInteger` (the device-integer case) and the texture builder case (`TextureBuilderGetStorageSize`, `TextureBuilderGetStorageAlignment`, `TextureBuilderGetWidth` and `GetHeight`) | `R`, `M` |
|  | `BufferGetAddress`, the vertex-format functions and `BlendStateSetBlendFunc` have no case yet. They follow with the buffer functions in 3c. | |
| 3c | `CommandBufferSetViewports`, `SetScissors`, `ClearColor`, `Barrier`, `QueueFenceSync` | `S`, `Y` |
| 3d | `CommandBufferBindBlendState`, `DrawArrays` | `S` for a draw |

Each of the three steps ends with a selftest run, and the step that has `S` also runs the mutation. The execution test starts in
3c (fence and clear), with the first run on the title scene. The selftest and the execution test are a launch of their own: the
user starts it once for each step, and the agent reads the log. The first launch of the selftest can also settle the open point of
the second queue.

### Checks that need a scene

A function that needs a real workload that a test cannot make (transform feedback with a real shader, tessellation, conservative
raster, the copies of real game textures) gets the state `seen` only from a game run. The first call of each function is logged
once with its arguments (`nvn-native: first call <name>`), so the agent finds out from the logs which function a scene reached.
The scenes are the ones that ROADMAP.md lists as not tested. The full playthrough comes last (decision of the user), so the
functions that no run reached stay `implemented` plus `unit`, `selftest` or `exec`, and they are written in the coverage file as
"not verified in the game".

### Steps in stage 4 for this part

- 4-u1: the tool `tools/nvn_coverage.py` and the first `docs/graphics/nvn-coverage.md`, with the trace of the argument values and the
  separate columns for `selftest`, `exec` and `seen`. Done 2026-11-06 except the trace of the argument values: the tool
  reads the 523 names and addresses from the sdk module (`--sdk`) or a trace log (`--log`). The first file has 274 host
  handlers and 249 missing-function handlers, and every one of the 242 functions of `nvn-calls.md` has a host handler.
  The trace of the argument values is in `gpu/nvn/diag/nvn_enum_trace.c` (2026-11-10): with `nvn_trace=1`, each new value of an
  enum or flag register of 63 observed functions gives one line `nvn-enum: <function> w<N>=<hex>`, at most 64 for each
  function. Run 2 (2026-11-10, update program to the title, DrawElements masks fixed to w1 and w2) logged 277 distinct values
  of all 63 functions, the same set as run 1 except DrawElements. The three DrawElements functions use only mode 4 (triangles)
  and index type 1. Each value of the run is now the observed scope of its function for the stub and refusal decisions of U2 to U5.
- 4-u2: the reading of the library for the 281 functions, in groups (events and fences, copies, transform feedback, tessellation
  and raster extras, the rest), one commit for each group, with the unit tests and the cases that the selftest takes.
  U2 task 1 (2026-11-10): 18 CPU side handlers. Events keep pool and offset in a host record, and the value word is the u32 in
  the pool storage (mode 1 adds, other modes write). The flushes of a mapped range and the two deferred finalizer calls are
  no-ops. GL syncs fail with the library result for "no backend". QueueWaitSync succeeds for a fenced or signaled sync because
  one GPU thread runs the jobs in order. QueueFinish waits for the posted frame jobs. QueueFlush with a pending frame,
  QueueFinalize and DeviceFinalize only log. Task 2 is the command buffer part (SignalEvent, WaitEvent, ReportValue,
  FenceSync, WaitSync).
- 4-u3: the selftest cases for the rest of the functions that a test can make, by group, and the execution test for events and
  `WaitSync`. Each run is reported with the passed scope.
  4-u3-3 (d), the command buffer cases in groups. d1 is the fixed-function state, d2 the binds and resources. The S check
  compares a register diff against a base copy of each channel. Eight host handlers are logged no-ops by design and their
  cases are marked expected no-op. The d2 fixes (2026-11-12): the word stream reads the mode 3 header (BindStorageBuffers),
  the vertex state cases build each element with the state setters of their side, and the decoder reads the pool records
  0x13 and 0x14, which the selftest expands as the library does at submission (d84c120, d84c3b0). Run of 2026-11-12
  (update program to the title): 301 cases, 0 failed, 8 expected no-op, 0 skipped. Mutation run with
  BindVertexAttribState: only that case fails, at r3d.0x458 (the stream index of attrib 0). The rows are in
  `nvn-coverage.md`.
  d3 is the draws, clears, copies, downsamples and the uniform upload (30 cases, no DispatchCompute case). The S check also
  compares the 2D, DMA and compute engines, with per-engine ignore lists for the per-side texture addresses. The d3 fixes
  (2026-11-12): the replay sends the 2D open and close words of subchannel 0 to the puller, the pool binds also write the
  compute engine, a DMA copy writes the pitch only for the pitch side and the surface params only for the block linear
  side (d82b0f0), the DMA block size has the 8-row GOB height (0x1000), the 2D route of CopyBufferToTexture uses the corner
  origin and starts the source half a texel in, and Downsample uses the center origin with the bilinear filter. Run of
  2026-11-12 (update program, exe of `10c6c46`): 329 cases, 0 failed, 11 expected no-op (DiscardColor,
  DiscardDepthStencil and ResolveDepthBuffer are new), 2 skipped (TiledDownsample and its texture view form: the library
  recording has op 21, which the decoder does not read, and the host has no driver program for it and uses the 2D engine).
  Mutation runs: the copy mutation makes the host region one texel narrower, because no side reads the filter bit of a DMA
  copy or of the 2D route of CopyBufferToTexture. CopyBufferToTexture fails its two rows (rdma.0x104, r2d.0x091) and CopyTextureToBuffer its row
  (rdma.0x105), and all other rows match the clean run. Open, with no failing case: the library writes no 0x1CE and a
  constant 0 to 0x1C7 (d82b3c4) while the host writes the layer field of a 3D texture, and the library aligns the
  CopyBufferToTexture buffer address down and adds the remainder in elements to the source x (d82b73c-d82b750) while the
  host passes the address as it is.
  d4 is the CPU side of the command buffer (`nvn_selftest_cbstate.c` for the R cases, `st_d4_cases` for the S cases). The
  host keeps a host record and does not write the game's command buffer object or its control memory, so d4 has no M
  case. The R cases make a new command buffer on each side and compare the getters after Initialize and after each
  setter, IsRecording around BeginRecording and EndRecording (the handle only as nonzero), EndRecording outside a
  recording, the checkpoints and SetDebugLabel. The five refused memory getters are expected refusals. The S cases are
  CallCommands (the source list of CopyCommands), the eight debug marker and group functions, and the two ZCull
  functions, which are expected no-ops (the library writes r3d 0x1FA..0x1FD). SetCommandMemoryCallbackEnabled has no
  case, because no getter reads its flag. Run of 2026-11-13 (update program, the code of `1024810`): 356 cases, 0
  failed, 18 expected no-op, 2 skipped. The debug marker and group calls are 3D macro calls that change no compared
  register. Mutation runs: SetCopyRowStride fails only its case (R GetCopyRowStride, library 0x1400, host 0x1401), and
  CallCommands with count 0 fails only its case (r3d.0x4ec, the line width of the source list). Next: 4-u3-4.
  4-u3-5 (2026-11-13): the rows without a check. 82 getters were already compared by the passed selftest cases (the
  getters are the R or M check of each setter case) and get that scope. Seven debug labels, the two texel flushes and
  ProgramFinalize are only called by the cases, with no compare after them: they get a reason, not a check. The checks
  of six handlers move into pure functions with host tests (`unit`, ten mutants of the helpers fail the tests). The
  other rows get the reason: no pure part (a direct record field, an empty body, a library body of one ret), no
  library window or second device in the selftest, or no shader code for a case. No row is left without a check or
  a reason. Runs of 2026-11-13 (update program to the title, the code of `00d7f62`): with `nvn_native=1`, 600 frames
  with no fault, drop or refusal, and 3840 program sets with 0 refused. The selftest: 356 cases, 0 failed, 18 expected
  no-op, 2 skipped, each case line the same as in the run of d4.
- 4-u4: the change of the default of `nvn_native_missing` to stop, when no row is `none` or `stub-log`.
  4-u4 (2026-11-13): the last `stub-log` row, `nvnDeviceGetProcAddress`, was the library lookup under a host hook. The host
  now answers it and `nvnBootstrapLoader` from the library's name and address tables (`nvn-design.md`, 2-3, item 6), so
  all 523 rows are `implemented`. The setting `nvn_native_missing` exists with the default 1 (stop). Tests: `nvn_params`
  (`nvn_proc_name_index`, `nvn_proc_table_check`; mutants of the sort check, the end bound, the alignment and the search
  fail the test).
  Run of 2026-11-13 (update program to the title, the code of `8237aff`, `nvn_native=1`, `nvn_native_missing=1`): the 523
  name answers are the same as in the run before the change (all `nvn:` lines), 1200 frames with no fault, drop or
  missing call, and the exit report has 522 resolved addresses with 0 without a handler.
  Run of 2026-11-14 (same scene, the code of `a61dc10` with the sdk segment bounds): the table passed the new checks, the
  `nvn:` answers are the same as in the run before, 983 frames at 30 per second, and no `nvn-native` stop.

Exit condition of this part: every one of the 523 rows is `implemented` and has `unit`, `selftest` or `exec` with its scope. The
rows that no game run reached are listed as "not verified in the game". The default of missing functions is stop. A function with a
check that no test can make has the reason in the file.

### U3 design of the open items (2026-11-10)

Decision of the user: no refusal for these 7 items, a sure design for each. Facts: the explorer reports of U3 (record in
`OLD-WORK_PLAN.md`, "U3 open items, step (b1)"). No `main` code calls any of the 7, so each row stays "not verified in the game".
The host replays NVN through the Eden based engines (`maxwell/dma.c`, `fermi2d.c`, `maxwell3d.c`), as
`replay_host_copy_texture` does with `nvn_rt_copy_texture_words`.

1. CopyBufferToTexture and CopyTextureToBuffer, all flags. The handler records an op with the region, the flags and the copy
   strides `cb+0x88`/`cb+0x8C` read at call time (0 means tight). The replay builds the words of the library route:
   - DMA route (B2T without `ENGINE_2D`, every T2B): a new builder of the DMA block `d82b0f0` (methods 0x100 to 0x1CF, then 0xC0
     LaunchDma), with the block math of the library (block height and depth from the format table, bytes per block, round up
     to whole blocks). Linear and block-compressed textures use the same builder.
   - `MIRROR_Y` and `MIRROR_Z`: the library gives a negative pitch, but `dma.c` keeps the pitch as `u32`. The builder sends one
     launch for each row (`MIRROR_Y`) or each layer (`MIRROR_Z`), in the reversed order, with a positive pitch. The bytes in
     memory are equal to the library result. `MIRROR_X` on the DMA route has no effect (library mask 0x18).
   - 2D route (B2T with `ENGINE_2D`): a new builder of the 2D block `d82c5d0` (header `0x91010002`) with the format change and
     `MIRROR_X` (negative `du_dx`). `fermi2d.c` samples a negative step on its CPU table path. A unit test must show that
     `r3d_blit` also takes it, else the 2D copy uses the table path. A format pair that `fermi2d.c` cannot convert stops
     with its log line.
   - The library does no bounds check. The host checks the region against the level size and the buffer range. A region out
     of range stops with a log line (the library would write outside the texture).
   - A multisample texture: the library uses level offset 0 and copies the raw bytes. The host does the same.
2. TiledDownsample and TiledDownsampleTextureView. The library runs a driver shader through macro 0xE1E, with program fields
   `device+0x9AC..+0x9B8` that the host does not have. The host does not replay these words. A new op makes the renderer do a
   `vkCmdBlitImage` with `VK_FILTER_LINEAR` from the source level (view base level and layer, NULL view gives the defaults of
   d83c800..d83c980) to the whole destination level. For an exact 2x reduction this is the 2x2 box (each sample is at the
   corner of 4 texels). Another size ratio gives a log line once and the linear blit result. A format without linear filter
   support stops with a log line. The 2x2 box is an inference from the constants `2*srcWidth`, `2*srcHeight`.
3. ResolveDepthBuffer. Method 0x47F resolves the depth compression of the bound depth target in place. Host depth images have no
   compression, so the contents are already resolved. The handler is a no-op with one log line. The library adds no barrier, so
   the host adds none.
4. MemoryPoolMapVirtual. Today the host refuses a pool with flag 0x200 (`NVN_POOL_UNSUPPORTED_VIRTUAL`). The host makes a virtual
   pool as a reserved GPU address range without storage (size a multiple of 64 KiB, storage NULL, as vtbl[0] d83ff88 checks).
   MapVirtual does what vtbl[0x60] does: batches of at most 0x200 requests of stride 0x28 (+0 physical pool, 0 unmaps,
   +8 physical offset, +0x10 virtual offset, +0x18 size, +0x20 storage class, ignored), in 64 KiB pages with the low bits
   truncated. Host pools have no nvmap handle, so the host does not use `as_remap`: it maps each page with
   `nvn_gpu_map_range` and keeps the mapped CPU address of each page in the pool record, which finalize unmaps. The
   virtual range starts on a 64 KiB page. The whole batch is checked before a page changes. A bad request returns false,
   and the batches before it stay, as in the library. A page outside every request stays unmapped, and an engine access
   to it gets the existing log line of an unmapped address. A physical pool finalized under a mapping is not handled.
5. DiscardColor and DiscardDepthStencil. Method 0x3DE makes the contents of the target undefined. Keeping the contents is one
   allowed result, so the handler is a no-op with one log line. The gate `device+0xA8` is 0, so the library always emits the
   word. The index of DiscardColor is checked (0 to 7) as the library masks it.
6. UpdateUniformBuffer of any size. The library copies the data into the stream at record time, ordered with the draws. The
   handler copies the `(size>>2)` words at call time (1 to 3 tail bytes drop, as in the library). Data over the op payload
   limit go into several ops in order, each one with the selector (0x8E0, size rounded up to 256, minimum 256) and its offset.
   The replay sends 0x8E0..0x8E3, the PipeNop 0x68B (the gate `cb+0x70` bit 4 is 1) and the 0x8E4 data words to
   `maxwell3d.c`. The writer checks what `maxwell3d.c` does with 0x68B before it sends it.
7. CopyCommands. Host lists are immutable and counted (`nvn_cmdlist.h`). The handler copies the op bytes of each source list
   into the destination list. A `NVN_OP_CALL` inside a source stays a reference and takes a new reference. Then the
   destination does not depend on the source list after the call, as in the library, where the source owns the copied
   words. CallCommands keeps its reference model.
