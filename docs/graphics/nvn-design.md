<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# NVN design: the choice of the way (phase 6, stage 2)

This file compares the two ways of `docs/graphics/nvn-plan.md` and holds the design of the chosen way. The input is
`docs/graphics/nvn-survey.md` (section 1-5). Nothing here is built yet.

**Decided by the user (2026-11-02):**

1. Way A is used.
2. The output of drawing is choice 1 (section 2-4): the host handlers write engine methods on a private channel, and the
   existing `Renderer` and its state update code are reused.
3. The old path stays as a comparison target and is selected for each launch.
4. The 242 functions that the played scenes called are not the limit of the work. The goal is all 523 functions that the
   game resolves, with a plan to check the ones that no scene has called (`docs/graphics/nvn-unobserved.md`).

## The two ways

- **Way A, replace NVN completely.** The host implements every NVN function that the game resolves (523 slots). The game's
  NVN library (the `sdk` module, d82xxxx to d85xxxx) is not run. No library pushbuffer and no GPFIFO entry exist. The host
  handlers and the replay of recorded command buffers write the Maxwell 3D, compute and copy methods on a private channel
  (section 2-4). The existing engine code and the `Renderer` turn them into Vulkan work, so the state update code, the
  write tracking and the pipeline cache are reused. The command stream that the library builds is gone. The method
  writes on the private channel stay, until choice 2 (a native draw interface) is judged after stage 4.
- **Way B, keep the library, replace the Maxwell layer in part.** The library still builds the pushbuffers and the
  GPFIFO entries. The runtime decodes them as now, but turns chosen command groups (for example draws and clears) into
  Vulkan calls at a higher level than register writes.

## What is replaced and what stays

| | Way A | Way B |
| --- | --- | --- |
| Entry points | All 523 resolved functions get a host handler. The 242 that scenes called are done first. The other 281 follow the plan in `nvn-unobserved.md`. | None. The table stays as it is. |
| Library code in `sdk` | Not run. | Runs in full. |
| Command stream (pushbuffer, GPFIFO, macros) | The library's stream is gone. The host writes engine methods on a private channel, and no GPU command buffer is parsed. | Stays. It is decoded, and a part of it is grouped. |
| Drawing output | The existing `Renderer` and the state update code of the engine (choice 1). | The same, fed by the decoder. |
| Recorded command buffers | New host object that can be called again (4.34 million calls against 1.33 million recordings). | The library does it. |
| Frame prologue and epilogue (engine reset, report semaphore, window image as color target 0) | The host does the equivalent by itself. | The library writes them. |
| Object memory in the guest | The host fills it. The game may read fields (open). | The library fills it. |
| GPU addresses (`BufferGetAddress`, texture handle) | The host chooses and must match what the game and the shaders expect (open). | Unchanged. |
| Shaders, pipeline cache, `gmem_*` write tracking | Stay. | Stay. |

## Size of the work

- **A.** The functions fall in few groups: state objects (pure data, a Set and a Get for each field, about 100 functions),
  texture, sampler and pool objects, buffer and memory pool, the queue and window (the frame cycle), and the command buffer
  (57 called functions, the hardest part because of replay). The game calls NVN from few places (a layer around
  a9b82e0 to ac8714c), so the surface is known: the loader resolves 523 slots, and that count is the bound of the work, not
  the 242 calls that one run saw. It is the larger work.
- **B.** The grouping needs a model of the engine state that the library leaves in registers, and a rule for each group
  when the state is complete. The work is smaller at first, but it grows with each kind of command, and the decode of
  macros and library-made entries stays.

## Risk of visual faults

- **A.** The risk is in what the library produces that the game or the shaders read back: raw field reads of objects,
  the GPU address values, the texture handle format, the exact object sizes. A mistake shows at once and in a clear way
  (wrong value in the first scene), as stage 3 expects. A missing library side effect (a state reset, a fence) shows as a
  fault that depends on the scene, which is harder to find.
- **B.** The risk is lower for the objects, because the library keeps them. It is higher for the grouping: a group closed
  at the wrong time gives a missing or wrong draw, and these faults depend on the order of the stream.

## Patch size against upstream

Both ways are code in `third_party/ScarletNX`, which is vendored, so upstream distance is not a hard limit.

- **A.** Mostly new files (a host NVN layer next to `nvn_trace.c`, which already wraps the table), plus a switch in the
  place that loads the table. Small changes to old files. The old path stays and a setting selects it.
- **B.** Changes inside `gpu/maxwell/` and the renderer interface, which are old files that upstream also changes.

## What each way gives

- **A** removes the command stream for the work it covers. This is the goal of the native plan (no GPU command stream).
- **B** does not remove it. It changes what the decoder emits. The gain in CPU time is not measured: the GPU uses about
  6 ms of 33 ms, and the CPU time of the decode is first measured in stage 5. B has no clear goal that A does not reach
  better.

## Recommendation

Way A, built in this order so that the old path stays:

1. The host layer replaces entries of the dispatch table (the same place where `nvn_trace.c` wraps them). A setting
   (for example `nvn_native`) selects the old path or the new one, as stage 3 requires.
2. Start with the objects that have no stream effect: device, state objects, memory pool, buffer, texture, sampler and
   pools. Object memory is the open point, so stage 3 begins with the raw-read question. The first scene shows a
   wrong field at once.
3. Then the queue, the window and the command buffer with replay, with the frame prologue and epilogue done by the host.
4. The 281 functions that the played scenes did not reach are implemented in stage 4 and checked as in `nvn-unobserved.md`
   says. Until then they use the missing-function handler of section 2-6.

Mixing the two paths inside one frame (some objects native, some from the library) is not possible for command buffers,
because a library command buffer holds a Maxwell stream and a host command buffer holds a recorded list of host ops. The
switch is therefore per launch (the setting `nvn_native` is read once at start), not per function and not during a run.

Why not B: it keeps the library, the macros and the GPFIFO decode, so it ends the phase with the same command stream. It
is the smaller step only at the start.

## Decisions for the user

1. Way A or way B. Decided: A.
2. The switch is per launch (`nvn_native` setting, old path kept as a comparison target until stage 5 decides). Decided.
3. The place of the code (`runtime/gfx` or inside ScarletNX) is decided after this choice. The first files go into
   `third_party/ScarletNX/src/nx/gpu/` next to `nvn_trace.c`.

## Open points that this choice does not close

- Raw reads of object fields by the game (state objects: not found; buffer, texture and command buffer: open).
- The values of `nvnBufferGetAddress` and `nvnMemoryPoolGetBufferAddress`, and the 32-bit texture handle format.
- The exact object sizes.
- Words 20 to 55 of the last library entry, and the contents of the other library entries.

Stage 3 answers the first three by running one scene. The fourth is a read of the log with more words logged.

## 2-1 Raw reads of NVN objects (started 2026-11-02, not finished)

Method: the module image of `main` (from the cached `--dump-modules` output). The loader at aa93e30 stores the result of each
`nvnDeviceGetProcAddress` call in a global slot (page c2f5000, 523 slots). A scan for `adrp` + `ldr` of these slots found the
game's call sites. `main` has no symbols, so a site cannot be named by its function. The scripts are in the scratch folder only.

Result so far:

- The hot functions match the earlier check (`DrawArraysInstanced` aaaf970, `QueueSubmitCommands` aac7d78,
  `QueuePresentTexture` aa83738, `DeviceGetTextureHandle` aaa1fcc, `VertexAttribStateGetFormat` aa859fc).
- The sites outside the Event slots are about 440. They lie in one block, aa81d54 to aac7de4, with `BufferMap` sites in the
  range a9b82e0 to ab279bc. The block is grouped by object type: state objects at aa85e84 to aa8cca4, pools and memory pool
  at aa92194 to aa9a7d0, buffer, texture and sampler at aa9c2c0 to aaa0a38, queue at aaa1130, command buffer calls at
  aaa1da4 to aac3458. So the game wraps NVN in one layer with one small function for each NVN call. This is the shape of
  `nn::gfx` (NVN backend) that is linked into `main`.
- The wrappers keep the NVN object inside a larger object of the game. Example (buffer init near aa9c7b4): the wrapper has the
  device at +0x20, copies fields to +0x28, +0x30, +0x38 and stores the GPU address (result of the call in slot +0x478) at
  +0x40. The game therefore keeps its own copy of the address, and it takes it from `nvnBufferGetAddress`. This supports the
  rule "the host chooses the address and the game follows the returned value".
- 14309 hits on the Event slots (page c2f6000) and a name repeated at 35 sites (`DeviceRegisterFastClearColori`) are not
  calls of those functions. The name table at the end of the page is not reliable in the scan. The sites above are not
  affected (checked on the five hot functions).

Not done: a data-flow check of each wrapper for loads and stores through the NVN object pointer, and the exact object sizes.
A scan of the whole code for such accesses is not possible without symbols, because every function of `main` can hold an
object pointer. Two ways remain: (1) read the wrapper classes one by one (about 440 small functions, the largest group are
the state objects, which were already shown to be read through calls), (2) let stage 3 show it. The recommendation is (1) for
buffer, texture, memory pool, command buffer and window only, and (2) for the rest.

### 2-1 second part: the wrappers of the five types (2026-11-02)

The game's wrapper allocates the memory of an NVN object itself (a virtual call of its allocator with a size and an
alignment), keeps the pointer in its own object, and gives only that pointer to the NVN function. In the `Initialize`
wrappers read so far, the game does not load or store a field of the NVN object.

| Type | What was read | Result |
| --- | --- | --- |
| Buffer | wrapper aa9c2a0, `Initialize` at aa9c7b4 | The NVN object is a separate allocation of 0x30 bytes (the library zeroes 0x30). The wrapper keeps the pointer at +0x20, the size at +0x30 and the result of `nvnBufferGetAddress` at +0x40. |
| Texture | aa9ca34 to aa9cbbc | A separate allocation of 0xC0 bytes (the library writes at least 0xA8). The pointer is at +0x48 of the wrapper. |
| Command buffer | aaa3ce4 | The wrapper object is 0x158 bytes. The NVN object starts at offset 0, and the game keeps its own fields at +0xE0, +0xF0, +0x128 to +0x150. So the NVN part is at most 0xE0 bytes. |
| Sampler | allocation sizes 0x70 and 0x60 near aaa0a38 | The NVN object is likely 0x60 (the library zeroes 0x60). Not read to the end. |
| Memory pool, window, queue, sync | allocation sizes 0x280, none found, 0x2028 (the queue memory, not the object), 0x48 | Not decided. These sizes are the sizes of the game's own objects or of memory, and the NVN object is not shown. |

What this means for the host layer:

- A host layer that keeps its state in a side table, keyed by the address of the object, does not need the exact object
  sizes. The game never relies on bytes that the host layer would write into the object, because it never reads them in the
  wrappers read so far. The object memory can stay as the game left it. This is the recommended design.
- The only fields of the game that sit next to an NVN object (the command buffer example) belong to the game. The host layer
  must not write beyond the start of the object if it writes into it at all.
- Still open: a data-flow check for loads from the object in code outside the wrappers (memory pool, window, queue, sync,
  and the command buffer replay path). Stage 3 shows it at once if it exists.

## 2-2 GPU addresses and texture handles (2026-11-02)

Method: the same scan as 2-1, then the disassembly of the wrappers that use the results (aaa1d60 to aaa2130).

Texture handle (`nvnDeviceGetTextureHandle`, one call site, aaa1fcc):

- The wrapper is the "set texture and sampler" function of the game's command buffer layer. It reads a texture index from a
  game object (+0x80 of the texture view) and a sampler index (+0x68 of the sampler), calls `nvnDeviceGetTextureHandle`
  once, and passes the result at once to `nvnCommandBufferBindTexture` (slot +0xc48) for each shader stage. It does not store
  the handle in memory. `nvnDeviceGetImageHandle` (one site, aaa20c8) feeds `BindImage` the same way.
- The indices are the numbers that the game itself gave to `TexturePoolRegisterTexture` and `SamplerPoolRegisterSampler`.
  So the handle is an opaque value between two NVN calls. The earlier assumption in the survey (the game writes handles
  into uniform buffers) is not supported by the code: no such site was found. Rule for the host: any 32-bit value that the
  host can turn back into (texture index, sampler index) is correct. The format of real NVN (texture index in the low
  bits, sampler index above) is the natural choice. Not verified against the SDK.

Buffer address (`nvnBufferGetAddress`, two sites, aa9c7c4 and aa9d3e8):

- The wrapper calls it once after `nvnBufferInitialize` and stores the result in its own object (+0x40).
- `BindUniformBuffer` and `BindStorageBuffer` (aaa1d60 to aaa1f4c) read the stored address and the live size
  (`nvnBufferGetSize`, called at each bind, 1.06 million times) and pass `(command buffer, stage, index, address, size)`.
  So the address returns to the NVN layer as an argument, and the game does not interpret it there.
- Not checked: other uses of the stored address (vertex and index buffer binds, copies, counters), and arithmetic on it
  (offsets added by the game). The stored field is at +0x40 of the wrapper, so loads from +0x40 would show them.
- Rule for the host: the host chooses the value. It must be unique for the buffer, stable until `Finalize`, and
  `pool base + offset` for a buffer in a pool (the game may add its own offsets). It can be a host-made virtual range
  with one entry for each pool.

Memory pool calls: `nvnMemoryPoolMap` (3485 calls) and `nvnMemoryPoolGetBufferAddress` (411) have no call site in `main` by this
scan. The callers are somewhere else (another module, or code that reaches the slot in a way the scan does not see). Open.
`BufferMap` has 33 sites in a9b82e0 to ac8714c. The mapped pointer is a CPU pointer into the guest memory of the pool, so
the host must return a pointer into the same guest memory that the renderer reads.

What is left for stage 3: the format of the handle and the address base are free choices of the host layer, so they are not
a risk. The risk that remains is a hidden use of the stored address or of the pool functions.

## 2-3 Where the host layer attaches (2026-11-02)

Method: `third_party/ScarletNX/src/nx/gpu/nvn_trace.c`, `runtime/main.c`, `runtime/loader.h`, the settings file, and the
disassembly of the resolver stubs in the `sdk` module.

How `nvn_trace` works, and what the host layer reuses:

- The game calls `nvnBootstrapLoader(name)`. It gives the address of `nvnDeviceGetProcAddress` and `nvnDeviceInitialize`.
  `nvn_trace_init` (called in `main.c` after `kernel_load_symbols`, before `jit_init` and before any guest thread) replaces the
  dispatch slot `g_disp[(addr - g_code_lo) / 4]` of `nvnBootstrapLoader` with a wrapper. The wrapper then replaces the slot of
  `nvnDeviceGetProcAddress`. Each name that the game resolves gets a wrapper in the slot of its address.
- With `nvn_native=1` the host handlers replace the game's NVN library for every call through the table. The loader hooks are the
  ones of `gpu/nvn_trace.c`, one level below it: `nvn_native_init` runs first, and the trace wraps what the slots then hold, so it
  counts the host calls too. A function without a handler gets the missing-function handler, so one run lists every missing function.
- A handler is a `GuestFn(CPU* c, u64 entry)`. The arguments are `c->x[0..7]` (and `c->v[]` for floats), the result is
  `c->x[0]`, and guest memory is read and written with `GP()`, `gr64()` and the like. The game calls NVN through pointers, and the
  generated code reads `g_disp` for such calls, so a handler in the slot sees every call. A recompile with `--hook-direct-calls`
  is not needed (step 1-2).
- The 522 resolved addresses are 522 different addresses. No two NVN functions share code in this game, so one handler
  per name is enough (`nvn_trace` logs a message when two names share an address; the log has none).
- The slots are written once. The JIT clears a slot only when guest code memory is rewritten (`jit_code_invalidate`), which does
  not happen for the code of the library.

Design of the attachment:

1. A new file `gpu/nvn_native.c` with `nvn_native_init()`, called in `main.c` next to `nvn_trace_init()`, and a startup setting
   `nvn_native` (0 or 1, default 0) in `settings.txt`.
2. The resolver hooks (bootstrap and `GetProcAddress`) are shared with `nvn_trace.c`. Today they are private to it. The change
   is to move them into one small piece that calls a list of "on resolve" functions: the native layer first (it installs its
   handler in the slot), then the trace (it wraps whatever the slot holds, so it counts the host calls too).
3. On resolve of a name, the native layer looks the name up in its table. A found name gets its handler. A name that is not in
   the table gets a "not implemented" handler. A library function must never run on a host object, because the library would
   read and write memory that the host never prepared.
4. The "not implemented" handler logs the name on its first call (`LOG_LIMIT`), counts the calls, and returns 0. It does not stop
   the game. A run then lists every function that is missing, and not only the first one. A setting value or a flag can make it
   fatal later.
5. `nvn_native` and the old path: with `nvn_native=0` nothing changes. With `nvn_native=1` the library is not entered by the
   game, so the GPFIFO submissions stop. The old path in `gpu/maxwell` stays in the code for the comparison of stage 3 and 4.
6. Also needed: the `nvnDeviceGetProcAddress` of the library must not run on a device that the library never initialized.
   The resolver stub is a trampoline (`adrp`, `ldr`, `ldr`, `br`: it jumps to a pointer that is filled at load). Open: if the real
   function reads the device object. The safe design is to answer `nvnDeviceGetProcAddress` in the host from the table of
   names that `nvnBootstrapLoader` gave, and to give back the same addresses as now.
   Done in 4-u4 (2026-11-13): the real function is d832940. It binary searches 523 sorted names (without the prefix `nvn`,
   offsets at sdk+0xABE798, strings at sdk+0xABAFD5) and returns entry n of a relocated `.data` table (sdk+0xC63E58). On
   each call until it is set up it also runs d82cd80, which calls d861ff0 with the device. `nvnBootstrapLoader` calls the
   same function with device 0. Now the host reads the two tables at the first lookup (after `rtld` relocated them),
   checks the sdk build ID and the table (`nvn_proc_table_check`), and answers both functions. The library lookup never
   runs. An unknown build, a NULL name or a bad table stops the game. A name that is not in the list (the library would
   try its 30 NVX extension names next) follows `nvn_native_missing`.
   The skipped setup changes no state that other code reads. `nvnDeviceInitialize` still runs d82cd80 (d831e74) and copies
   the table 0xe0f2988 to the thunk table 0xe0f03e0 (d831e8c). 0xe0f2988 stays zero, because d861ff0 and d859ba0 run only
   when two flags are set (d861f14, d864698) whose setters (d861f20, d861880) have no caller. So the library lookup also
   copied zeros. No code branches to the 523 thunks d82d1b0..d82f250, and `main` and `subsdk0` import only
   `nvnBootstrapLoader`. The thunk path d82f260 needs `[0xe0f1470]` >= 1, which the game does not set.

Facts found on the way (from the disassembly of the `sdk` module):

- The handle functions of the device are small and pure. `nvnDeviceGetTextureHandle` (d82d0d0) returns
  `sign_extend(texture | sampler << 20) | 1 << 32`. `nvnDeviceGetImageHandle` (d82d050) returns `sign_extend(index)` with no tag (checked 2026-11-04). The sampler handle
  functions (d82d0a0, d82d0b0, d82d0c0) are not checked. This settles the handle format of step 2-2: the host layer uses this formula, and
  the pool registration takes the texture index from the low 20 bits.
- Correction to the survey: d82d110 (`nvnDeviceInitialize` as the loader returns it) is a trampoline, and not the function that
  writes at +0x19d8. The access at +0x19d8 belongs to the small getter at d82d100. The table "Object memory that Initialize
  writes" is read from the real function behind the trampoline, which this check did not locate. The lower bound for the
  device is not changed by this, but the address in the table is the target of the trampoline and not the stub.

Open for 2-4 and 2-5: what the host handlers call (the `Renderer` interface or Vulkan directly), and the design of the three
hard parts.

## 2-4 What the host handlers call (2026-11-02)

Method: `gpu/renderer.h`, the fields of `GpuChannel` that `gpu/vk/` reads, `gpu/gpu.h` (guest memory through the GPU address
space), and the size of the files in `gpu/vk/` and `gpu/maxwell/`.

What the `Renderer` interface is:

- It is the seam between the Maxwell front end and Vulkan, but it is not free of Maxwell. `draw(GpuChannel* ch, ...)` and
  `clear(ch, ...)` take the whole channel, and the Vulkan side reads the register file `ch->r3d[]` (render targets, viewports,
  blend, depth, vertex format, shader stage setup) and the constant buffer bindings `ch->cbind` itself. `dispatch` takes the
  QMD (the compute launch description). Textures and samplers are read from the pools in guest memory in the Maxwell
  descriptor format (TIC and TSC).
- Memory goes through the GPU address space (`gmem_read`, `gmem_ptr`, the mapping hooks `g_gpu_mapping_begin/end`). The
  nvdrv code (`hle/services/nvdrv.c`) maps GPU addresses to guest pages. The renderer reads shader code, vertex data,
  uniforms and textures by GPU address.
- Syncpoints, semaphores and releases (`stream_incr`, `release_write`, `before_syncwait`) are tied to the GPFIFO stream.
- Size: `vk/` is about 20000 lines (`render.c` 3305, `pipeline.c` 1938, `surface.c` 1760, `compute.c` 1522, `texture.c` 1391,
  `buffer.c` 2362). `maxwell/` has the macro handling (`mme_hle.c` names the macros that NVN uses and what registers each sets).

Two choices for the handlers:

| | Choice 1: write the channel state, call the existing `Renderer` | Choice 2: a native draw interface |
| --- | --- | --- |
| Idea | Handlers keep the NVN state in the host objects. At a draw, they write the registers of a private `GpuChannel` (`r3d`, `cbind`) directly and call `vk_renderer.draw`. No pushbuffer, no GPFIFO entry, no macro, no decode. | A new `draw_native(DrawState*)` with native state (formats, blend, targets). `render.c`, `pipeline.c`, `record.c` read this state. |
| Reuse | All of `vk/` stays: shaders, pipeline cache, surfaces, texture cache, compute, write tracking. | Needs a rewrite of the parts that read `r3d`. |
| Compare with the old path | The same renderer gets the same registers, so frame dumps must match. A fault is in the encoding. | Two renderers: a fault can be in either. |
| Work | The register encoding of NVN state (the macros in `mme_hle.c` show each one), the TIC/TSC descriptor encoding, and the memory pool mapping in the GPU address space. | All of choice 1's state work, and the rewrite. |
| Remaining Maxwell | Inside the renderer only (registers as a state format). | None at the draw. |

Recommendation: choice 1 for stages 3 and 4, choice 2 later, as part of phase 8 (shaders and pipelines), when the new path
matches the old one. The command stream and the decode (the goal of phase 6) are gone with choice 1. The state format that the
renderer reads is not changed by this phase.

Calls and services that the new path needs and the current code does not offer:

1. A private `GpuChannel` made without nvdrv (`gpu_channel_create` or the like), with no puller and no pushbuffer.
2. A way for the host `MemoryPoolInitialize` to map the pool in the GPU address space, with the call that nvdrv uses for
   `AS_MAP_BUFFER_EX` (`hle/services/nvdrv.c`, near line 144), and to remove it at `Finalize`.
3. Fences and queries without syncpoints: `nvnQueueFenceSync`, `nvnSyncWait`, `ReportCounter` need a host replacement for
   `stream_incr`, `release_write` and the samples-passed counter (`samples_*`). A first version can wait for the renderer
   (`stream_end`, `idle`) at the fence.
4. Present: the window texture is a render target of the host. `present_buffer` takes a guest framebuffer
   (address, pitch, layout). A call that shows a render target by its GPU address is needed (2-5 decides it).
5. Nothing new is needed for the draw, clear, copy and dispatch calls themselves: the registers and the QMD are written by
   the handlers.

Open for 2-5: the encoding of the state is the largest part of the work and 2-5 designs the recorded command buffer around
it (a recording stores NVN calls, and a replay writes the registers at the time of the call).

## 2-4 Decision and rules for the register path (2026-11-02)

Decision of the user: choice 1. Choice 2 (a native draw interface) is a candidate for a later clean-up. Whether to move to it
is judged after choice 1 is implemented, with the criteria at the end of this section.

"Write the registers directly" means one thing here: the host handlers feed the same engine entry points that the pushbuffer
decoder feeds (`maxwell3d_method`, `compute_method`, `kepler_i2m_method`, `fermi2d_method`, `dma_method`, all in `gpu/gpu.h`),
on a private `GpuChannel`. A handler never stores into `ch->r3d[]` itself. The reason is in the list of side effects below.

### Side effects that a direct store into `r3d[]` would bypass

`maxwell3d_method` (`maxwell/engines.c`) does more than store a value. Each item is something a store would skip:

| Side effect | What the engine does | Rule for the host layer |
| --- | --- | --- |
| Dirty groups | A changed register sets its group bit in `r3d_dirty` (`R3D_SHADER`, `VATTR`, `VFORMAT`, `RTFORMAT`, `BLEND`, `PIPEMISC`, `XFB`). The renderer builds pipeline keys again only for dirty groups. | Always write through the method entry. A missed bit gives a stale pipeline with no error. |
| Shadow RAM | `SET_MME_SHADOW_RAM_CONTROL` modes (track, passthrough, replay) change what a write stores. | The private channel runs in passthrough. The host never sets replay. |
| Held draws | Draws that differ only in their instance are merged and held (`g_engines.deferred`). Any other method, channel or the end of the stream sends them first (`r3d_pending_flush`). | The host never reads renderer state in between. A tap (below) runs before the draw is queued, not after. |
| Constant buffer data | A write to the constant buffer data registers is held and written to memory in one go (`r3d_const_upload`, `r3d_const_flush`), and it records the write for the caches (`gpu_mem_written`). | Uniform data that the game wrote through `BufferMap` is already in guest memory. The host binds the buffer with the bind group method (`BIND_GROUP_CONFIG`, `r3d_cb_bind`) and does not upload through the registers. |
| Constant buffer binding | `BIND_GROUP_CONFIG` fills `ch->cbind` and `cbind_size`. | `BindUniformBuffer` writes the same method. |
| Acting methods | Draw begin and end, index and vertex draws (`queue_draw`), `CLEAR_SURFACE` (`r3d_clear`), `DRAW_TEXTURE`, barriers (`barrier()`), `SYNC_INFO` (`stream_incr`), report semaphores (`query_get`), samples-passed enable and reset, render enable (`render_enable_update`), transform feedback offsets. | Draws, clears, barriers and reports are made only by writing these methods. |
| Macros | The library draws with MME macros (`macro_call`, `mme_hle.c` names them). A macro call also sets registers and runs the draw. | The host does not run macros. For each macro that NVN uses, `mme_hle.c` is the list of the registers the host writes and of the draw it ends with. |
| Inline data | `LAUNCH_DMA` and `INLINE_DATA` write memory (`i2m_launch`, `i2m_data`). | The host writes guest memory itself and calls `gpu_mem_written` where the engine would. It does not need the inline path. |
| Compute and copy | `compute_method` takes the QMD. The 2D and copy engines have their own methods. | Same rule: write the methods, do not call the renderer interface past them. |

### State update notifications

- `r3d_dirty` is set by the method entry (see above). A fresh private channel starts with `r3d_dirty = ~0u`, as `gpu.c` does at
  channel creation.
- The renderer is told that guest memory may have changed (`mem_may_have_changed`) by `gpu_submit` today. The host replay calls
  it once before it starts a replay on the GPU thread. The memory write hooks stay as they are (`gpu_mem_written`,
  `gpu_mem_changed`). `stream_run(1)` and `stream_run(0)` surround a replay job, as they surround a job now.
- `stream_end` is called at the end of a replay job, because the guest may look at memory after it.
- Texture and sampler pool changes (`RegisterTexture`, `RegisterSampler`) write descriptors into guest memory. They are
  guest writes, so the descriptor cache is invalidated with the barrier `BARRIER_DESCRIPTOR_CACHE`. The replay writes the
  method at the point where the library would write it (the start of a command buffer replay and after a pool change).

### Threads, recording and replay

- The renderer is used by the GPU thread only. Guest threads must not call it. The host handlers therefore record, and a
  replay job runs on the GPU thread. `gpu_call` (`gpu/gpu.c`: calls run on the GPU thread between jobs, in order) is the
  existing way to put a function on that thread. Open for 2-5: if it fits a job that lasts a frame.
- A command buffer records host calls, not registers. A replay (`CallCommands`: 4.34 million against 1.33 million recordings)
  must use the state of the time of the replay for the parts that depend on it (bound pools, render targets set by the
  caller). `nvnQueueSubmitCommands` stores the list. `nvnQueuePresentTexture` sends the frame as one job, as the library does
  now with one GPFIFO submission (step 1-4).

### Resource lifetime and synchronization

- Guest memory stays the truth. Pools are mapped in the GPU address space at `Initialize` and unmapped at `Finalize`
  (`g_gpu_mapping_begin/end`). A `Finalize` that runs while a replay job still uses the pool must wait for the job. The game
  waits with its own fence (`nvnSyncWait`, 17838 calls) before it frees memory, so the host `SyncWait` returns only when the
  GPU thread has run every job before the fence and the results are landed in guest memory.
- The host objects (buffer, texture, sampler, program, state objects) are in a side table keyed by the object address (2-1). A
  recorded command buffer holds the object address and a generation number, not a pointer into the table. A `Finalize`
  or a reuse of the address changes the generation. A replay that finds a stale generation logs the call and skips it
  (undefined on the real console, so a game bug).
- Fences: `nvnQueueFenceSync` puts a marker in the GPU thread queue. When the marker runs, it lands the pending results (what
  `before_syncwait` and `release_retire` do now) and signals the sync object. `SyncWait` waits for the marker. Semaphores and
  reports that the game polls (`ReportCounter`, the semaphore methods) go through `release_write`, not through a direct memory
  store, because they must come after the work before them.
- The window: `nvnWindowAcquireTexture` returns the next image and a sync. The present job draws, then shows the render
  target (2-5 designs the call that shows a render target by its GPU address).

### Taps for per-draw information (TAA and other post effects)

The host layer sees the draw at the NVN level, before it becomes registers. That is the place for the information that a
temporal anti-aliasing pass or another effect needs, and the register path must not lose it. The plan reserves the place
now and fills it when an effect needs it.

- Where: in the replay on the GPU thread, right before the host writes the draw or clear method (before `queue_draw` can
  hold it), at the frame start (`WindowAcquireTexture`) and at the frame end (before the last method of the present job).
- What a tap gets (a read-only view of the host state, `NvnDrawInfo`): the kind (draw, indexed draw, clear, dispatch, copy),
  topology and counts, the render targets (color and depth: GPU address, format, size, the texture object), viewports and
  scissors, the bound program, the bound uniform buffers (GPU address, size), the bound textures and samplers by handle, and the
  frame number. These are the sources for the depth buffer, a velocity buffer, the matrices in the camera uniform buffer, the
  previous frame target and the sub-pixel jitter.
- What a tap may do: read, and for an effect that changes a draw, set an override that the replay applies when it writes the
  methods (for example a viewport offset for jitter). The override is part of the draw, so the `r3d_dirty` rules above hold
  for it.
- The old path has no such place: there only registers are visible. This is one reason to build choice 1 on host objects and
  not as a patch of the register decode.
- Not decided now: which draws are the scene pass, and how a velocity buffer is made (the game may not write one). The tap
  carries the information for that later analysis.

### When to evaluate choice 2

After choice 1 runs the target scenes (end of stage 4), list three things: the CPU time of the register encoding and the engine
entry (stage 5 measures it), the faults that came from the register format and not from the encoding rules, and the effects
(TAA and others) that the register state makes hard. Move to choice 2 only if these show a gain that is larger than the
rewrite of the `r3d` readers in `vk/`. Until then choice 2 is a candidate for clean-up, and nothing in choice 1 depends on it.

## 2-5 Design of the three hard parts (2026-11-02)

Method: the numbers of the survey (`docs/graphics/nvn-calls.md`), `gpu/gpu.c` (`gpu_call`, `gpu_submit`), `runtime/present.c` and
`hle/services/vi.c` (the present path now), and the rules of section 2-4. Nothing here ran. Each assumption that needs a run is
listed at the end, as the first work of stage 3.

### (a) The recorded command buffer and its replay

What the game does (counts from the run of 2026-11-02): 1.33 million `BeginRecording` and `EndRecording`, 4.34 million
`CallCommands`, about 150 recordings and 490 calls in each frame. One frame submits 76 or 77 command buffers. The game gives
command memory in pieces (`AddCommandMemory` 1.77 million, `AddControlMemory` 1.47 million) and reuses it.

Design:

- A command buffer object has a recorder. `BeginRecording` starts a new list in an arena. Each call is one op: an opcode and its
  arguments, 32 bytes at most, in a plain array. `EndRecording` closes the list and returns its handle (a 64-bit id, with a
  generation, that the host makes). `CallCommands(handles, n)` records an op that points to those lists (a nested call).
- Values are copied at record time. This is the NVN rule: `BindBlendState`, `BindVertexAttribState`, the polygon, depth
  stencil, color, multisample and channel mask states copy the contents of the state object into the command memory when
  they are called. The game may change or finalize the state object afterwards. So the op holds a snapshot of the state, not
  the object address. The same for `Set...` arguments, viewports, scissors and clear colors.
- Things that the GPU reads when it executes are not copied: buffer contents (uniforms, vertices), shader code, texture and
  sampler descriptors, texture storage. The list holds the GPU address, the size, or the handle. The replay reads guest
  memory at its own time, as the GPU does.
- Objects that a list names (program, texture, buffer) are held as address plus generation (2-4). A stale one is skipped
  and logged.
- Replay: a function walks the list and writes the methods through the engine entries (`maxwell3d_method` and the others) on
  the private channel. A nested call walks the other list at that point. Depth is limited (a named constant), and a cycle is a
  fatal error with the handle in the message.
- Life of a list: a list lives while its command memory range is valid. The host keeps the range from `AddCommandMemory`
  (pool, offset, size). A new recording that is given an overlapping range frees the older lists there. The command buffer's
  `Finalize` frees its lists. A submitted frame holds a reference to each list it uses until the replay ends, so a
  list is never freed in the middle of its replay. Reusing memory that a list still uses and has not run is a game bug,
  because the game waits with a fence before it reuses command memory.
- Memory callbacks (`SetMemoryCallback`, `SetMemoryCallbackData`): the host does not need command memory, and it never calls
  the callback. This is checked in stage 3: if the game depends on a callback to give more memory, the first scene shows it.
- Counters (`ReportCounter` 37762, `ResetCounter` 9962) and `Barrier` are ops that become the matching methods. Debug ops
  (`InsertCheckpointDynamic`, debug groups) are dropped.

### (b) The frame cycle and the work of the library

The cycle in all 8919 frames: `WindowAcquireTexture`, `QueueSubmitCommands`, `QueueFenceSync`, `QueuePresentTexture`.

- `WindowAcquireTexture(window, sync, &index)`: calls `present_pace(interval)` (`runtime/present.c`, what vi does at
  `DequeueBuffer` now), takes the next index of the textures that the game gave in `WindowBuilderSetTextures`, signals
  `sync` at once (the image is free: the present job has copied the frame out), and returns the index. The interval is the one
  of `nvnWindowSetPresentInterval`.
- `QueueSubmitCommands(queue, n, handles)`: copies the handle array at once (the game alternates two arrays and reuses them)
  and adds the lists to the queue's pending frame. It queues nothing on the GPU thread, as the library does not.
- `QueueFenceSync(queue, sync, condition, flags)`: attaches a token to the pending frame for this sync. The token is
  signaled when the GPU thread has run the frame job. If nothing is pending, it is signaled at once.
- `QueuePresentTexture(queue, window, index)`: closes the pending frame and queues one `gpu_call` job for it
  (`gpu_call(fn, frame, NULL, 0)`: it runs on the GPU thread in order with the calls and jobs of other users, and a call is not
  parked by a wait, which this job never makes). One job a frame is what the library does with one GPFIFO submission.
- The frame job: `stream_run(1)`, `mem_may_have_changed`, the frame start (the prologue below), the replay of every list in
  order, the taps (2-4), the last method, `stream_end`, `stream_run(0)`, then the show of the image, then the signals of the
  tokens, then `cheats_frame` and `g_frames` as `vi_show_buffer` does.
- The show of the image: `present_buffer(cpu_addr, w, h, pitch, layout, bhl, fmt, crop, transform)` takes the guest address of
  the image. The window textures are the game's texture objects with their storage in a pool, so the host takes the
  address from the texture's storage and the layout from its builder fields. No new renderer call is needed. This replaces the
  note in 2-4 (item 4), which asked for a call that shows a render target by its GPU address. Stage 3 checks that the renderer
  reads the render target of the last draw with `rt_flush` before it shows the memory.
- What the library adds to every submission (step 1-4): three small entries before the game's lists (19, 1 and 20 words) and
  one after (56 words). Of the last one, 20 words are decoded: two inline writes of `0x451`, a write of `0xB2`
  (`SYNC_INFO`), a report semaphore write (`0x6C0` to `0x6C3`), `SET_MME_SHADOW_RAM_CONTROL` with value 2, and the setup of color
  target 0 as the window image. The host does the equivalent as follows, and each line is an assumption until stage 3:
  - The shadow RAM mode of the private channel is passthrough (value 2), set once.
  - Color target 0 is set to the acquired window image at the frame start. The game sets its own targets in its command buffers,
    so this only matters for a draw that relies on the default.
  - The report semaphore and `SYNC_INFO` serve the library's own fence and queue accounting (its `SyncWait` and command memory
    flush). The host has its own fences (tokens), so it does not write them.
  - Words 20 to 55 of the last entry, and the three first entries, are not decoded. They may hold a state restore that the
    game's lists rely on. This is the first thing stage 3 reads (see below).

### (c) Memory and synchronization

- The truth is guest memory. A pool is a range of guest memory. At `MemoryPoolInitialize` the host maps it in the GPU address
  space (the same mapping call as `AS_MAP_BUFFER_EX` in `hle/services/nvdrv.c`) with a GPU address that the host chooses
  (2-2). At `Finalize` it unmaps it, after the frames that use it have run (the game's fence guarantees this).
- `BufferMap` and `MemoryPoolMap` return the guest pointer of the range. The CPU writes are seen by the renderer with the
  write tracking it has (`gpu_mem_written` is called by the existing guest write hooks). No host call is needed for a write.
  `FlushMappedRange` and `InvalidateMappedRange` (not called in the run) call `gpu_mem_written` over the range.
- Textures: the storage is in a pool. `TextureBuilderGetStorageSize` and `GetStorageAlignment` must give the sizes of the
  Maxwell block linear layout, because the game carves its pools with them and fills the memory with data that is already in
  that layout (`SetPackagedTextureLayout`, 5584 calls). The host copies the algorithm of the library's function (read from the
  disassembly), and does not invent one. A wrong size breaks the pool layout of the game at once.
- Descriptors: `TexturePoolRegisterTexture` and `SamplerPoolRegisterSampler` write TIC and TSC entries into the pool memory
  in the Maxwell format that `vk/texture.c` reads. The host encodes them from the texture and sampler objects (the reverse of
  what the decoder reads). The handle is `texture | sampler << 20 | 1 << 32` (2-3).
- Fences and waits: the tokens of the frame job (above). `nvnSyncWait(sync, timeout)` waits on the token with a time limit
  and returns the NVN result codes (signaled or timeout). It must not hold the GPU thread: a guest thread waits, the GPU
  thread signals. `nvnSyncInitialize` and `Finalize` make and free the token.
- Results that the CPU reads: occlusion counts and compute or render target results that the game reads after a fence. The
  frame job lands them before it signals the token (`stream_end`, then the release functions of the renderer), as
  `before_syncwait` does for the console's syncpoint wait.
- Pause and save states (`gpu_hold`, `reset`, `flush_targets`): the host frame job is a normal GPU thread call, so `gpu_hold`
  waits for it. The save state does not record the host recorder state. A state is only taken between frames, when the lists
  are empty. This is checked when the new path reaches the save state code (stage 4).

### Order of the work of stage 3 and the checks it needs

1. Diagnostic run (before the code): extend the `gpu-submit:` trace to show all words of the library's entries (the small
   ones and the 56 word one), for the first 3 submissions only, then the user runs the game to the title once. The agent reads
   the words and decodes them with `tools/maxwell_label.py`-like names. This closes the open point of 1-4.
2. The object layer without drawing: device, memory pool (with the mapping), buffer, texture and sampler objects, state
   objects (setters and getters), pools. The "not implemented" handler for everything else (2-3).
3. The queue, window and command buffer recorder with a replay that writes only the frame start, a clear of the window image and
   the present. This is the smallest path of stage 3.
4. One draw: the title scene's first draw, compared with the old path by frame dumps.

Assumptions to confirm in stage 3 (each is a plain yes or no from the first runs):

- The game does not need a memory callback of the command buffer.
- The default color target is not relied on, or the frame start sets it.
- No game command buffer relies on a state restore from the library's entries.
- `present_buffer` shows the last drawn render target correctly (`rt_flush`).
- One `gpu_call` job a frame does not hold back the other GPU users (video, the pause).

## 2-6 Order of the work of stage 3, files and test plan (2026-11-02)

Source: the 242 functions of `docs/graphics/nvn-calls.md` and the design of 2-1 to 2-5. The call counts do not show the order of the
calls, so the groups below follow the dependencies. The user accepts this section before stage 3 starts.

### Rule for the unknown

The target is all 523 resolved functions, not the 242 of the run. Every function that the table can return and that has no
host handler yet goes to the missing-function handler (2-3). It logs the name and the arguments once and returns zero. A
command buffer op that the replay does not know yet is recorded, counted by opcode, and skipped. The counts print at the end
of each frame for the first 10 frames and then once a second. So a run shows what is still missing, and the agent reads it from
the log. This lenient mode is for stages 3 and 4 only. When the plan of `nvn-unobserved.md` is done, the setting
`nvn_native_missing` changes the default to stop (fatal error with the function name), because a silent zero can hide a fault.
Done in 4-u4 (2026-11-13): `nvn_native_missing` (0 Log, 1 Stop, default 1). The command buffer ops above are not changed.

### Groups of functions (the numbers are the counts of functions of `nvn-calls.md`)

| Group | Functions | Step |
| --- | --- | --- |
| G1 Device and queue | `DeviceBuilderSetDefaults`, `DeviceBuilderSetFlags`, `DeviceInitialize`, `DeviceGetInteger`, `DeviceSetWindowOriginMode`, `DeviceSetDepthMode`, `DeviceGetTimestampInNanoseconds`, nine `QueueBuilder*`, `QueueInitialize` | 3b |
| G2 Memory pool | `MemoryPoolBuilder{SetDefaults,SetDevice,SetFlags,SetStorage}`, `MemoryPool{Initialize,Finalize,Map,GetFlags,GetBufferAddress,SetDebugLabel}` | 3b |
| G3 Texture objects | `TextureBuilder*` (21), `TextureInitialize`, `TextureFinalize`, `TextureGet{Width,Height,Depth,Target,Stride}`, `TextureBuilderGetStorageSize`, `TextureBuilderGetStorageAlignment` | 3b |
| G4 Window and sync | `WindowBuilder*` (5), `WindowInitialize`, `WindowSetPresentInterval`, `WindowSetCrop`, `SyncInitialize`, `SyncWait` | 3b |
| G5 Pools | `TexturePoolInitialize`, `TexturePoolGetSize`, `SamplerPoolInitialize`, `SamplerPoolGetSize` | 3b |
| G6 Command buffer | `CommandBuffer{Initialize,Finalize,AddCommandMemory,AddControlMemory,SetMemoryCallback,SetMemoryCallbackData,BeginRecording,EndRecording,CallCommands,SetTexturePool,SetSamplerPool,SetRenderTargets,SetViewports,SetScissors,ClearColor,Barrier}` | 3c |
| G7 Frame cycle | `WindowAcquireTexture`, `QueueSubmitCommands`, `QueueFenceSync`, `QueuePresentTexture` | 3c |
| G8 One draw (title scene) | `Buffer*` (9), `Program{Initialize,SetShadersExt,Finalize}`, the state setters and getters (blend, color, polygon, depth stencil, multisample, channel mask, vertex attrib, vertex stream), `CommandBuffer{Bind*,Set*,Draw*}` | 3d |
| G9 Later (stage 4) | `Texture{Pool,View}*`, `Sampler*`, `TextureHandle`, `ClearTexture`, `ClearDepthStencil`, `CopyTextureToTexture`, `DispatchCompute`, transform feedback, `ReportCounter`, `ResetCounter`, `Image` functions, `TextureWriteTexels`, `TextureFlushTexels` | stage 4 |

| G10 Not observed | The 281 resolved functions that no scene called: transform feedback extras, tessellation, conservative raster, `Event*`, `FenceSync`, `WaitSync`, `QueueFlush`, `QueueWaitSync`, buffer copies, `BindImages`, mapped range flushes, and the others | stage 4, `nvn-unobserved.md` |

G1 to G5 hold about 70 of the 242 observed functions. G6 and G7 add 20. The sets are not final: step 3a (the diagnostic run) and the
first runs of 3b and 3c can move a function from one group to another.

### Steps, files and checks

Files (new unless it says changed). The names are a proposal, the split follows the groups.

- `third_party/ScarletNX/src/nx/gpu/nvn_native.h`: the shared types (object table, the side table of 2-1, the recorder types).
- `nvn_native.c`: the table switch (2-3), "not implemented" handler, G1, G4 and the frame job (2-5 b).
- `nvn_native_mem.c`: G2 and the GPU address rule of 2-2.
- `nvn_native_tex.c`: G3, G5, the storage size function, the TIC and TSC encoders (stage 4 uses them with the pools).
- `nvn_native_cmd.c`: G6, the recorder and the replay (2-5 a).
- `nvn_native_state.c`: the state objects of G8 (step 3d).
- `nvn_trace.c` (changed): the diagnostic of 3a, and the switch to the host table.
- `nvn_selftest.c` (new): the selftest and the execution test (`nvn-unobserved.md`, "The selftest"), with the recording `Renderer` backend and
  the comparer. The comparer is a separate pure part, so that host tests can use it.
- `tests/nvn_native.c` (new) and `CMakeLists.txt` (changed): the tests below.
- `docs/build/runtime-changes.md` (changed): one row for each commit that touches the runtime.

Steps. Each step is one commit. The game is not started by the agent.

1. 3a Diagnostic run (no host code). Extend the `gpu-submit:` trace as 2-5 says (all words of the library entries, the first 3
   submissions). Also log the result of `TextureBuilderGetStorageSize` and `GetStorageAlignment` with the builder fields (width,
   height, depth, format, target, levels, flags, stride), for the first 40 calls. Run: the user starts the game to the title
   once. The agent reads: the words (it decodes them and closes the assumptions 2 and 3 of the list above), and the size
   table (the test data of step 3b).
2. 3b Object layer without drawing. G1 to G5 with the switch `nvn_native=1`. The old path stays when the setting is 0. Tests:
   the pure parts on the host without the game (storage size against the table of 3a, the address rule, the side table and
   the generation check, the recorder and the replay order of nested calls, the TIC and TSC encoders against the decoder in
   `vk/texture.c`). Run: the user starts the game with `nvn_native=1`. The expected result is that the game reaches
   `QueueInitialize` and `WindowInitialize` and then stops at the first function of G6 or G7 that is missing. The agent reads
   the "not implemented" lines and the opcode counts. This step also adds the harness of the selftest (the comparer with its
   host tests, the isolated memory, the recording backend) and its first functions (table in "Introduction in stage 3"). A
   second launch with `nvn_native=0` and `nvn_selftest=1` gives the result of kinds `R` and `M`.
3. 3c Command buffer and frame cycle with the smallest replay (frame start, clear of the window image, present). G6 and G7.
   Tests: the recorder with the recorded sequence of a real frame shape (counts of 2-5 a), the token of `QueueFenceSync`,
   `SyncWait` with timeout. Run: the user starts the game to the title with `nvn_native=1`. The agent reads: frames shown,
   the unknown opcode counts, the present interval, and any fatal error with its stage name. The user says what the screen shows
   (the clear color or a black screen). The game cannot draw yet, so this is the check of the cycle only. This step adds the
   selftest cases of kinds `S` and `Y`, the mutation check (`nvn_selftest_mutate`), and the first execution test (fence after a
   clear, and the read of the clear by the CPU).
4. 3d One draw. G8 for the first draw of the title scene. The check is a frame dump of the title with the old path and with the
   new path (`nvn_native=0` and `=1`), compared by the agent with a script (local only, never in git). Run: two runs of the
   same scene. Stage 3 ends when both give the same result for this scene. This step adds the selftest case of one draw.
   A pass of the selftest and the match of the frame dumps are two records, and neither replaces the other.

Each assumption of the list above has a step that shows it: 1 and 2 in 3a and 3c, 3 in 3a, 4 in 3d, 5 in 3c (the frames shown
per second, and the video and the pause are checked in stage 4).

### What the agent reads after a run

Log lines that stage 3 adds (the prefix is `nvn-native:`): the start line with the setting and the table size, one line for
each "not implemented" function (once), the per frame summary (frame number, lists replayed, ops replayed, ops skipped by
opcode), and the fatal errors with the stage name (object layer, recorder, replay, present). The agent reads them with the
skill `nx-log-read` and reports the result. The user is only asked for what is on the screen.

## Unobserved functions

The plan for the functions and argument values that no scene called (coverage file, selftest, execution test, steps
4-u1 to 4-u4 and the U3 design) is in `docs/graphics/nvn-unobserved.md`.

## 3a Result of the diagnostic run (2026-11-02)

Run: update program, 43.4 s, 1149 frames at 30 frames a second, 0 JIT misses, no crash. The user ran to the title scene once. The
log has 40 `gpu-submit:` blocks and 40 `nvn-size:` queries. The window had 1 call of each builder function (the same as the run of
`docs/graphics/nvn-calls.md`), 1149 calls of the four frame functions, and 432 calls of `SetMemoryCallback`.

### The library entries of a submission

Names below come from `maxwell_3d_regs.h` where it has one. A register without a name there is written as its number and not
named.

- Submission 1 (3 entries, 2301, 1 and 10 words) is the setup at boot, not a frame. Its small entries: a zero word, and the tail
  `0x451`, `SYNC_INFO`, `0x451`, `REPORT_SEMAPHORE`, `0x1000f010`, `0x3e0`.
- Every frame submission (168 to 172 entries, 77 lists) has three entries with the sync flag before the game's lists (19, 1 and
  20 words), and one after (56 words, sync). The game's own lists have no sync flag.
- Entry 0 (19 words): writes `0xC0B` twice (`0x80000000`, `0x70000000`), then the pairs `0x910e0002 ... 0x91000002` around the
  writes of `0x4A2`, `0x369`, `0x50A`, `0x509`. Not decoded further. The same pair of words surrounds other small entries of the
  game's lists too.
- Entry 1 (1 word): zero, a no-op.
- Entry 2 (20 words): writes `0x55D` (3 words: `5`, `0x12bf0000`, `0x1fff`) on subchannel 0 and on subchannel 1, `0xE20` with `0x40`,
  `0x8E4` (`CONST_BUFFER_BUFFER`) with `0x12bf0000`, `5`, and `0x557` (`5`, `0x12bec000`, `0x1ff`) on both subchannels. By the
  register numbers (Eden's names, not checked in this repository) these are the texture header pool (address
  `0x5_12bf0000`, limit `0x1fff`) and the sampler pool (address `0x5_12bec000`, limit `0x1ff`). So a library entry, not the
  game's lists, sets the two pools in these frames.
- Tail (56 words), decoded:
  1. `0x451` (immediate 0), `SYNC_INFO` = `0x00100014`, `0x451`, `REPORT_SEMAPHORE` (address `5:0`, payload 2, 4 or 5 in
     the three submissions read, flags `0x1000f010`), `0x3E0` (immediate 0).
  2. `SHADOW_RAM_CONTROL` = 2 (passthrough). `RT` 0 (`0x200`, 8 words): address `5:0x53dc0000` (frame 2) or `5:0x53550000` (frame
     3), width 1920 (`0x780`), height 1080 (`0x438`), format `0xD5`, tile mode `0x40`, layers 1, layer stride `0x0021c000`.
     Then `0x574` = 0, `0x3FD` = `0x07800000`, `0x04380000`, `WINDOW_ORIGIN` = `0x10`, `0x3ED` = 0, `0xB8` = 0.
  3. `SHADOW_RAM_CONTROL` = 3 (replay). The same writes of `RT` 0, `0x574`, `0x3FD`, `WINDOW_ORIGIN`, `0x3ED` and `0xB8` again.
     In replay mode the value that the engine takes is the one in the shadow RAM (`maxwell_3d_regs.h`, line 158), so the
     second group puts the registers back to what the game's lists had set.
  4. `SHADOW_RAM_CONTROL` = 1 (track with filter). The frame ends in this mode.
  5. `0x451`, `SYNC_INFO` = `0x00110014` twice, `0x451`, `REPORT_SEMAPHORE` (payload one more than in item 1), `0x3E0`.
- The window image address changes from frame to frame (the acquired texture). Its size, format and layout are the same.

### What changes in the design

- Section 2-5 (b) said: "the shadow RAM mode of the private channel is passthrough, set once". This is wrong. The library moves the mode
  through 2, 3 and 1 in the tail of each frame, and the frame ends in mode 1. The host frame start must set the mode, then set
  `RT` 0 to the window image in passthrough, restore in replay mode, and end in mode 1, or do the same effect in another way
  that the stage 3 tests prove. The assumption stays open until step 3c.
- Assumption 3 ("no list relies on a state restore from the library's entries") is partly answered: the entries before the game's
  lists set the texture and sampler pools, and the game's lists use texture handles. The host must set both pools at the frame
  start from the last `SetTexturePool` and `SetSamplerPool` calls. Whether the library takes them from these calls is not shown by
  the log, and the pools of the entry are the same in all frames read.
- Assumption 2 (the default color target) is not answered: the library sets `RT` 0 to the window image after the game's lists.
- The game gives a memory callback to each command buffer (432 calls). The log does not show whether the library calls it. This
  stays an assumption for 3c.

### Storage size queries

The log has 40 queries with 32 words of the texture builder each (the size of the builder is not known, so the words are raw).
The words at 8 to 14 hold, for the 1920 x 1080 case: `9 1 0x780 0x438 1 1 0x25`, and for 960 x 540: `8 1 0x3c0 0x21c 1 1 0x3f`. So
width and height are at words 10 and 11, and the format very likely at word 14. The results seen (alignment, size): 1920 x 1080
`0x10000`, `0x870000` (which is 1920 x 1152 x 4: the height rounded up to a multiple of 128 rows); 960 x 540 `0x10000`, `0x260000`;
a 1-pixel-high case `0x20`, `0x20`; and others (`0x200` with `0x21c000`, `0x4b0000`, `0xc0000`, `0x400`, `0x12c000`).
These are the test data for the storage size function of step 3b. The builder words 0 to 7 and 15 to 31 are not decoded yet, and
the fields that decide the result (target, levels, flags, packaged layout) must be found by comparing the queries one by one in
3b.

Open: the log lines are many (about 10000 `gpu-submit:` lines, because the small entries of the game's lists are also printed
in full in the first 3 submissions). The trace is for this diagnostic only.

## 3b Progress (2026-11-02)

Step 3b is split in parts, because the answers of the device queries and the sizes must come from the library and not from a
guess (rule: mechanism first). Each part is one commit.

| Part | Content | State |
| --- | --- | --- |
| 3b-1 | The setting `nvn_native`, `gpu/nvn_native.c` (loader hooks below the trace, missing-function handler, exit report), `gpu/nvn_objtab.c` (the record table, test `nvn_objtab`), and the trace of the queries (`nvn-value:` lines for `nvnDeviceGetInteger` and six other queries). | Written and built. The unit tests pass, except `tracked_material`, which fails on the two SMAA `.dds` files that were added before this work (not caused by it). Not run in the game. |
| 3b-2 | A diagnostic run with `nvn_trace=1` to the title: the agent reads the `nvn-value:` lines. | Done (2026-11-02). The values are in "3b-2 Result" below. |
| 3b-3 | G1: device and queue builders, `DeviceInitialize`, `DeviceGetInteger` (the table of 3b-2), `GetTimestampInNanoseconds`, `QueueInitialize`. | Written and built (30 handlers, `gpu/nvn_params.c`, test `nvn_params`). Not run with `nvn_native=1`. See "3b-3 Result". |
| 3b-4 | G2: memory pool and the GPU address rule (2-2). | Written and built (13 handlers, test `nvn_params`, row 0029). Run of 2026-11-02: all 13 ran without a failure. The GPU page table mapping of a pool is not connected. |
| 3b-5 | G3 and G5: texture objects, the storage size function (read from the library's disassembly, tested against the 3a table), the pools. | Written and built (29 handlers, row 0030, test `nvn_params`; 40 of 40 size queries of the archived run agree). Not run with `nvn_native=1`. |
| 3b-6a | G4: window builder and initialization, interval and crop setters, and sync initialization. | Written. |
| 3b-6b | G4: `WindowAcquireTexture` with host image state and `SyncWait` for the two states known so far. Finalizers remain for later parts. | Written. |
| 3b-7 | The selftest harness (comparer with tests, isolated memory, recording backend) and its first cases of kinds `R` and `M`. | Done (2026-11-03, row 0034). Run with `nvn_native=0`, `nvn_trace=1`, `nvn_selftest=1` to the title: 51 cases, 0 failed (`DeviceGetInteger` 27 values, M; texture builder storage queries 24 sets, R). Scope: kinds R and M, this build of the `sdk` module. Not yet shown on the live run: that the selftest sees a wrong handler (the mutation check comes in 3c). |

The code review of the Orca "code reviewer" (decided for 3b) comes after 3b-7, over the code of 3b-1 to 3b-7.

Open risk of 3b-1 (first run with `nvn_native=1` shows it): the library's `nvnDeviceGetProcAddress` runs for the lookups on a
device that the library never initialized. If it reads the device object, the run stops there, and the host must answer the
lookups itself from a table of names.

### 3b-6a Host window and sync records

With `nvn_native=1`, the host owns window builder, window, and sync state in the object table. The game’s object memory is
not written, and the host does not create compatibility words for the game’s NVN library. The builder defaults to device 0,
native window 0, no textures, a null texture array, and interval 1. `WindowInitialize` reads the texture array when it runs,
so the game may fill the array after `WindowBuilderSetTextures`. It accepts 1 to 4 entries and requires each address to name
known host texture. An invalid count, unreadable array, or unknown texture gives one failure log and leaves an existing
window record unchanged. The native-window pointer is stored only; the host does not call VI or a wrapper from this part.
The window record keeps the device, textures, the builder interval, the full first-texture crop, and index -1. Crop
width or height zero resets all crop fields to the first texture size. `WindowSetPresentInterval` always stores its 32-bit
value because the host has no wrapper callback. The builder interval setter stores the supplied value (the library stores it minus 1 and adds 1 at initialization, which gives the same result).
`SyncInitialize` records its device and the unsignaled state. Acquire, wait, finalize, active-texture count, and getters stay
on the missing-function path, so this part does not complete the frame cycle. The disassembly corrections are: the alleged
`+0x50` call and `d94ec20` belong outside `WindowInitialize`.

### 3b-6b Acquire and wait

Each host window image starts FREE. `WindowAcquireTexture` uses `nvn_window_pick_image` to select the first FREE image after
the last acquired index, with wraparound. It calls `present_pace(interval)` outside `objs_lock`, then checks the records
again. On success it marks the image ACQUIRED, records its index, signals the supplied sync and writes the index to the
guest. A FREE image means its previous frame job has finished. No frame job in this part releases an image, so when every
image is ACQUIRED the handler logs `no free window image`, writes -1 and returns failure. This is the intended stop until
3c implements present and image release.

`SyncWait` ignored its timeout in this part. Since the fix of 2026-11-07 it waits on a FENCED sync until the frame job signals
it or the timeout ends (result 0 or 2; `0xffffffffffffffff` is no limit). A sync that was never fenced and an unknown sync give 3
at once. `WindowAcquireTexture` waits the same way when no image is FREE and one is PRESENTED. `frame_finish` wakes both
(`frame_done_cv`, `objs_lock`). Cause: the first frame job needs 0.5 s on the GPU thread, and the game waits on its fence meanwhile.

### 3b-2 Result (run of 2026-11-02, update program, title screen, `nvn_trace=1`, `nvn_native=0`)

The run ended without a crash. JIT misses: 0. Frame rate: about 30. `nvn: 522 resolved addresses, 231 called`.

`nvnDeviceGetInteger` (27 distinct queries, what = value). The queries 0 to 39 were not made.

| Value | what |
| --- | --- |
| 1 | 40, 41, 42, 50, 59, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76, 87 |
| 0 | 90 |
| 256 | 4, 17, 18 |
| 32 | 15, 16 |
| 4 | 19 |
| 8 | 20 |
| 65536 | 82 |
| 262144 | 85 |
| 16384 | 92 |

The six other query functions:

- `nvnTexturePoolGetSize`: returns the size argument (0x2000). `nvnSamplerPoolGetSize`: the same (0x200).
- `nvnQueueBuilderGetQueueMemorySize`: 0x8a000.
- `nvnBufferBuilderGetSize`: returns the size argument in almost all calls. Two calls have an x3 that looks like a stack address
  (0x13000ffef8 gives 0x40, 0x13000ffed8 gives 0x4000). The trace may read the wrong argument there. Check it in the
  disassembly in 3b-3.
- `nvnMemoryPoolGetFlags`: 0x22 in most calls, 0x24 for small pools in CPU memory, 0x12 once (a pool of 0x3000000 bytes).
- `nvnDeviceGetTimestampInNanoseconds`: 0 in the first two calls, a real value after that.

The Vulkan translation reported `79 failed` of 1787 shader programs. It was not compared with an earlier run.
### 3b-3 Result (run of 2026-11-02, update program, `nvn_trace=1`, `nvn_native=0`)

Library functions read (update 1.1.1 `sdk` module):

- `nvnDeviceGetTimestampInNanoseconds` (d82d0e0): `ticks = counter[+8]`, then `ticks * w58 / w5c` in two steps (whole and rest) with the
  device words at +0x58 and +0x5c. The run shows w58 = 0x271 and w5c = 0x180, so the ratio is 625 / 384. Device +0x19d8 and
  +0x19dc (the window origin and depth mode) were 1 and 1 at that time.
- `nvnQueueBuilderGetQueueMemorySize` (d8383f0): reads the builder (flags at +0x10, set bytes at +0x14 to +0x17, sizes at +0x18,
  +0x1c, +0x20) and six device words (+0x8c, +0x94, +0x98, +0xe0, +0xe4, +0x1af4). The run shows 0x80, 2, 1, 1, 0x1000 and 0. The
  game's builder had flags 0, command 0x20000, compute 0x40000, control 0x20000. The port in `gpu/nvn_params.c` gives 0x8a000 for
  it, the library's answer (hand-checked step by step, and in the test `nvn_params`).
- `nvnDeviceGetInteger` (d82d060): calls d831f40, a switch over `what` (0 to 0x62) that returns a device field or a constant. The
  host uses the table of 3b-2. A `what` that no run asked for gets 0 and one log line.
- The queue builder, device builder and the window origin and depth mode functions are plain field accesses. `SetDefaults` of the
  queue builder zeroes all fields except the device and sets the priority to 100.

Not verified: the names of the queue flag bits 0 and 1 in `nvn_params.c`, the default of the origin and depth modes after
`DeviceInitialize` (the host uses 0, no run read them before the game set them), and the queue memory size for flags other than 0.
`nvnDeviceGetCurrentTimestampInNanoseconds` and the setters with no call in the run still go to the missing-function handler,
except the ones in the table of `native_procs`.
### 3b-3 First run with `nvn_native=1` (2026-11-02, update program, 16:57)

The run did not draw (0 frames) and did not crash. The log shows:

- The lookups worked: 522 addresses resolved through `nvnBootstrapLoader` and `nvnDeviceGetProcAddress`. So the open risk (the
  library's `nvnDeviceGetProcAddress` reads the device object) did not happen for the lookups.
- The only NVN function that was called is `nvnDeviceBuilderSetDefaults`, 1121063378 times. `SetFlags` and `DeviceInitialize` were
  not called, but the game's code calls them right after `SetDefaults` (function aa912a8 of `main`, the calls at aa91310,
  aa91338 and aa91350). This code has no loop. So the repeat is in the host. The main thread stays at lr=aa91314 (the return
  site of the `SetDefaults` call) in `threads.txt`.
- Cause: the host handlers did not set `c->pc`. A guest function ends with RET, which sets `c->pc` to x30, and after each call the
  generated code compares `c->pc` with its return address (`check_return`, `ir_codegen.c`). The handler left `c->pc` at the last block
  entry of the caller (aa91304), so the caller ran that block again. The trace hypothesis was wrong: the trace wrapper calls the
  real function, and its RET set `c->pc`, so `nvn_native=0` worked. Fix: runtime change 0028 (`native_call`).

### 3b-3 Second run with `nvn_native=1` (2026-11-02, update program, 17:14)

- The loop is gone. The game resolved the functions, called the 30 handlers and then 37 functions that have no handler: the memory
  pool builder and `MemoryPoolInitialize`, the texture and sampler pools, the sampler builder and `SamplerInitialize`, the texture
  builder, `TextureBuilderGetStorageAlignment` and `GetStorageSize`, and `TextureInitialize`. Each returned 0.
- Then the game crashed in function aa926ec (null pointer plus 0x30). It reads x21, the result of the pool allocator aa969bc, which
  returns 0 because the pool and texture initialization failed. This follows from the missing handlers, not from a host fault.
- Next: 3b-4 (memory pool) and 3b-5 (texture, sampler, pools).


### 3b-5 Host sampler and texture functions

The 29 functions of the accepted 3b-5 table have host handlers. Builders and initialized objects use host records.
Sampler registration writes eight words at the memory pool CPU base plus pool offset plus signed index times 32.
An unknown pool or sampler, or a zero CPU base, gives one error line and no descriptor write.
Texture initialization rejects imports, unknown or null pools, physical pools, unsupported formats, and a texture
with flag 8 whose pool has no flag in 0x280. Successful initialization logs its parameters only with `nvn_trace=1`.

The sampler LOD snap formula is `T[i]=(8-d)/(2*d)`, with `d=(8+i%8)*2^(i/8)`. All 32 f32 results match the module
at de87870. The encoder uses this formula rather than a copied table. Border RGB uses the library's sRGB conversion.
Device initialization disassembly verifies +0x1a80=0 and +0x1a84=0x111. The sampler descriptor now
includes bit 0x100 in word 1 with these inputs.

Texture device inputs +0xac=0x10000, +0x19f8=0, +0x19fc=0 and +0x1b18=0 are verified device values.
Only +0x1b1c=1 stays provisional: the 40 archived size queries require R to be true for flags 9 and format 0x25.
The only store found for this range is a 4 byte clear of +0x1b18 at d831c0c, which does not reach +0x1b1c, so the
earlier claim that device initialization clears this byte was wrong. The selftest logs the byte of the game's device. The code implements classification R
for size and alignment. Chip kind selection and NvRmGpuMappingModify are not ported. The texture kind byte stays 0,
and the address is always the memory pool host GPU base plus offset. GPU page-table mapping remains open from 3b-4.

The standalone `nvn_params` tests check sampler defaults and encoding, snap fractions, texture defaults, the three
known storage cases, buffer, mip chain, cube, 1D array, 3D, samples, compressed blocks, sparse tiles, minimal tiles,
adaptive tiles, imports, invalid formats and compression inputs. All 40 storage queries agree with the archived
library run `logs/2026-11-02_16-50-49.log`. The current update log is the later native run and has zero results
from missing handlers. Give the archived log path to the standalone test to repeat the comparison. No game file
is part of the source change. Runtime integration has not been built or run for this part.

### 3b-5b Texture views and descriptor registration

The 15 new handlers add builder dimensions and packaged layout, seven view setters, texture width/height/stride
getters and pool registration. Views use host records with set flags and 8-bit level or 16-bit layer fields.
The pure encoder follows d858c70, d8593f0 and registration d83d99c..d83da34. It reads a supplied format record.
The handler reads the relocated pointer at guest e03fd40 on each call. Records have 0x30 bytes; the seed is the
u32 at byte offset 0x24 (d858d28). No game format table is in the source. New verified device words are
+8=0x4000, +0x1a48=2, +0x1a4c=1, +0x1a50=0, +0x1a54=0xffffffff and +0x1a88=2. Format indices above 0xa0
and targets above 10 fail; device rows above 5 use row 5. Both descriptor types share the CPU destination lookup.
Unknown objects, a zero CPU base or unreadable input give one error line and no write. A null view is valid.
Synthetic tests check hand-computed descriptor words, view overrides, depth/stencil, samples, mips, invalid indices,
device rows and packaged tiles. No real descriptor was logged. Other view getters are outside this part.
The texture initialization log is unchanged. Runtime integration has not been built or run for this part.

## 3c Host design decisions (2026-11-03)

Sources: the reports `analyst-3c-A-1.md`, `analyst-3c-B-1.md`, `analyst-3c-C-1.md` and `explorer-3c-1.md` (scratchpad). Checked
against the disassembly: BufferInitialize, BufferMap, BufferGetAddress, BufferBuilderGetSize, CommandBufferInitialize, the
QueueFenceSync thunk, EndRecording (start), SetTexturePool. Not checked: ProgramSetShadersExt, the tables of the explorer. The
rule of the user for window and sync work applies: the host owns the objects, no compatibility words go into game memory, and an
unknown part stops with a log line.

### Facts that decide the design

- The library command buffer is a list of records plus command memory. `SetTexturePool` and `SetSamplerPool` store a 16 byte
  control record (op 0x13 and 0x14 with the pool pointer) and write no method at call time. The interpreter (d839d20) expands
  them at submission. `SetViewports`, `SetScissors`, `SetRenderTargets`, `ClearColor` and `Barrier` write their methods into
  command memory at call time. The host records all of them as ops, so this difference does not reach the host.
- The `EndRecording` handle is a control memory address with a 16 bit tag. `QueueSubmitCommands` and `CallCommands` read the
  memory behind it. The host replaces both, so its handle can be any value. `nvnDeviceFinalizeCommandHandle` is not in the
  call list of `nvn-calls.md`: it follows the rule for unobserved functions (no handler, the missing-function line).
- `QueueSubmitCommands` makes no GPFIFO submission and no entry. `QueuePresentTexture` flushes the queue and builds the entries
  (explorer: entry 0 caches, entry 1 empty, entry 2 pools, entry 3 fence and window image). `QueueFenceSync` makes the 56 word tail
  entry (fence and report semaphore).
- `BufferMap` returns pool map pointer plus offset, and 0 only for a pool with the CPU no-access flag. The crash after 3b came
  from the missing handler that returned 0.

### Decisions

1. **Objects.** New kinds `NVN_OBJ_BUFFER_BUILDER`, `NVN_OBJ_BUFFER`, `NVN_OBJ_PROGRAM`, `NVN_OBJ_COMMAND_BUFFER`. A buffer record holds
   pool address, pool generation, offset and size. `BufferInitialize` fails with a log line when the pool is unknown, has the
   physical flag (0x100, as the library) or when offset plus size is outside the pool (the library has no range check, the host
   adds it because the next step would read outside the pool). `BufferFinalize` removes the record. `BufferSetDebugLabel` does
   nothing. `BufferGetAddress` is pool GPU address plus offset. `BufferMap` is `nvn_memory_pool_map` plus offset.
2. **Program (G8, not 3c).** `ProgramInitialize`, `SetShadersExt` and `Finalize` stay out of 3c. 3c draws nothing, and the program
   needs the shader path of 3d. They keep the missing-function line.
3. **Command buffer record.** One record for each command buffer: the memory callback and data (stored, never called), the added
   command and control memory ranges (stored for the log, not used), the recording flag, and the list in progress. `AddCommandMemory`
   and `AddControlMemory` store the range and do not check it. `BeginRecording` during a recording logs a line and drops the open
   list (the library does not check). `EndRecording` outside a recording returns 0 (as the library).
4. **Handle.** A host id: generation in the upper 32 bits, (list index + 1) in bits 1 to 31, bit 0 clear, never 0. A published list is
   immutable and has a reference count: the command buffer owns one reference, each `Submit` and each `CallCommands` takes one
   (a `CALL` op holds its target). The owner reference ends at `Finalize`, and when the game gives control memory that overlaps
   the control memory the list had at `EndRecording` (the library's handle is a control memory address, so the handle ends when
   the game reuses that memory). Lists of one command buffer only: a chunk reused by another command buffer is not seen, and the
   frame line must show the number of live lists so that a growth is found. A cycle of `CALL` ops cannot exist (a list names only
   published lists). `QueueSubmitCommands`
   and `CallCommands` take ids only. An id that is not valid is skipped with a log line (counted).
5. **Lists and ops.** An op is an opcode with a payload of at most `NVN_OP_MAX_PAYLOAD` (512) bytes, padded to 8 bytes (changed
   in the work: `SET_RENDER_TARGETS` holds 8 targets with copied views, so 32 bytes do not fit; viewport and scissor are one op
   for each rectangle). The list lives in an arena of the command buffer (`gpu/nvn_cmdlist.c`). Values are copied at
   record time (viewport floats, scissor integers, clear color, barrier bits, render target arguments). Objects (texture, pool) are
   held as address plus generation and read at replay. Ops of 3c: `SET_TEXTURE_POOL`, `SET_SAMPLER_POOL`, `SET_RENDER_TARGETS`,
   `SET_VIEWPORTS`, `SET_SCISSORS`, `CLEAR_COLOR`, `BARRIER`, `CALL`. Any other command buffer function is a missing function:
   its first call is logged and nothing is recorded. A frame that has such a call is counted in the frame line (3c cannot draw).
6. **Replay.** On the GPU thread in the frame job, through `maxwell3d_method` on the private channel. Nested `CALL` depth is limited by
   a named constant. A cycle stops with a log line that has the handle.
   - Texture pool op: `SET_TEX_HEADER_POOL_A/B/C` (0x55D) with base high, base low and limit (count minus 1).
   - Sampler pool op: `SET_TEX_SAMPLER_POOL_A/B/C` (0x557) the same way.
   - Subchannel 1 (compute) and the macro 16 constant buffer words are not written. Compute is stage 4. The constant buffer is open
     for 3d (the shader use is a guess of the explorer).
   - Viewport: scale `w/2` and `+-h/2` (the sign is `device.window_origin == 1`), translate `x + w/2`, `y + h/2`, and the clip words
     (floor and ceil clamp rules of the analyst, written in the table). The depth range comes from `SetDepthRanges` (3d).
   - Scissor: the clamp rules of the analyst. The enable method 0x380 is not written here.
   - Render targets: `RT_CONTROL` (0x487), one nine word block for each color target, the format-0 disable for unused slots, the
     depth block (0x3F8, 0x48A) and `SURFACE_CLIP` (0x3FD). The parts for depth compression and the sample table are left out until a
     scene needs them. The format word needs a host table of the library format rows (`[0xe03fd40] + format*0x30 + 0x1c`): the
     table is the first thing to read before this op (a new analyst task).
   - Clear color: write 0x360 to 0x363 and run the clear of one target with the channel mask. The macro 1 body is not read. The host
     calls the renderer clear for the bound target instead of the macro, and the clear covers the whole target.
   - Barrier (changed 2026-11-09, task (b)): the replay sends the words of `nvnCommandBufferBarrier` (sdk d856590, read in the
     disassembly). The old rule (no method, one counter for each bit) gave the transform feedback write of the skinning shader no
     Vulkan barrier before the vertex read, because `WAIT_FOR_IDLE` did not reach `vkr_method_barrier`. The words, in this order:
     1. Always first: `0x910E0002` (puller NOP, data 0x110E).
     2. One of these, the first set bit wins: bit 3 `0x80000014` (puller `REF_CNT` 0), then the range record of type 1, then
        `0x80000040` (3D method 0x40). Else bit 0 `0x80000044` (`WAIT_FOR_IDLE`). Else bit 1 `0x80000378` (`FRAGMENT_BARRIER`).
        Else bit 2 `0x800003DF` (`TILED_CACHE_BARRIER`). Bits 0 to 2 are not sent when bit 3 is set.
     3. Bit 4: `0x800003DD` (`INVALIDATE_TEXTURE_DATA_CACHE`) when step 2 sent bit 1 or bit 2, else `0x800004A2` (method 0x4A2,
        no name in `maxwell_3d_regs.h`, no action in `maxwell3d_method`).
     4. Bit 5: `0x90110369` (`INVALIDATE_SHADER_CACHE_NO_WFI`, data 0x1011). Bit 6: `0x8000050A` then `0x80000509` (texture
        header cache, then sampler cache, both `NO_WFI`). Bit 7: `0x801904B2` (method 0x4B2, data 0x19).
     5. Bit 8: the words of sdk d859710 (cb, 1, 1): `0x8000C014` (subchannel 6, puller `REF_CNT` 0). Then, when the device byte
        +0x60 is not 0, `0x2001C00B 0x80000000` (`MEM_OP_B`, operation 16). Then `0x2001C00B 0x70000000` (`MEM_OP_B`, operation
        14). These are puller register writes only: `puller_method` keeps them and does nothing more.
     6. Bit 3 again: the range record of type 7. Always last: `0x91000002` (puller NOP, data 0x1100).

     The builder is a pure function in `nvn_params.c` (bits in, words out) with a test. The host sends a word with a method below
     0x40 to the puller (`gpu_channel_method`), on any subchannel, and a 3D method to `maxwell3d_method`. The range records of bit 3
     are not words: they split the command memory of the library into GPFIFO entries. The host has no GPFIFO on this path, so it
     does not make them. The bit counters for the log stay. The device byte +0x60 is always 1: device init calls d835630 at d831164,
     which fills a stack record (d8358c4 writes 1 at +0x60 at d835980, with no branch), and copies record +0x0 to +0x6F to the
     device (`stp q3, q2, [x19, #0x50]` at d8311b8). So bit 8 always sends the two `MEM_OP_B` pairs. The host uses a named
     constant 1.
7. **Pool mapping.** The renderer reads render targets and textures by GPU address, so every pool needs a page table entry from the
   first use. `gmmu_map` is public in `nvdrv.h`. The unmap (`gmmu_unmap` plus `gmap_cut` and the allocator) is not. Decision: add one
   small function pair to `nvdrv` (`nvn_gpu_map_range`, `nvn_gpu_unmap_range`) with a row in `docs/build/runtime-changes.md`. The host
   keeps the bump allocator from 0x400000000 and maps at `MemoryPoolInitialize`, unmaps at `Finalize`. Overlap with a game mapping
   fails the initialize with a log line. This is the first work of 3c, because the clear needs it.
8. **Queue and frame.** `QueueRec` gets the pending frame: the lists of `Submit` (the handle array is copied at once), the tokens of
   `FenceSync`, and the last pool objects. `QueuePresentTexture(queue, window, index)` closes the pending frame and calls
   `gpu_call` once. A bad index (not ACQUIRED) fails with a log line and shows nothing. Changed in 4-u3-4: `QueueFlush` and
   `QueueFinish` also post the pending frame, as a job without a window ("Queue fences and flush jobs (4-u3-4)" below).
9. **Frame job.** `stream_run(1)`, `mem_may_have_changed`, set the window image as color target 0 and the clip (the tail entry of the
   library does this, the game sets its own targets later), replay the lists, the last method, `stream_end`, `stream_run(0)`, show
   the image with `present_buffer` (address from the texture storage, layout from the builder fields), set the image FREE, signal
   the tokens, then the frame counters as `vi_show_buffer` does. Skipped on purpose, each with a reason: the L2 operations (no L2),
   the cache invalidations of entry 0 (the renderer tracks writes), the sync point and report semaphore writes (the host has
   tokens), the shadow RAM modes (set once at channel setup, passthrough), the decompress of the window image (the renderer reads
   plain memory). The assumption that none of these changes a result is checked by the first execution test and the first scene.
10. **Sync.** `NVN_SYNC_UNSIGNALED`, `FENCED` (a frame will signal it) and `SIGNALED`. `FenceSync` with nothing pending signals at
    once, otherwise it attaches a token and sets `FENCED`. `SyncWait` waits on a condition variable with the timeout in
    nanoseconds (-1 no limit), outside `objs_lock`, never on the GPU thread. Results: 0 already signaled, 1 signaled after a wait,
    2 timeout, 3 failed (a sync never fenced, as now). `Acquire` still sets the sync to `SIGNALED`. Replaced in 4-u3-4 by the
    syncpoint fence of the queue ("Queue fences and flush jobs (4-u3-4)" below).
11. **Window images.** The states are FREE, ACQUIRED and PRESENTED. `Present` sets PRESENTED, the frame job sets FREE. The third
    `Acquire` then finds a FREE image.
12. **Log.** One line for each frame job: frame number, lists, ops replayed, ops of each kind skipped, missing-function calls in the
    frame. The success path of `Acquire` is counted in this line.
13. **Selftest.** Kinds S and Y: the first S case records `SetViewports` and `SetScissors` in the library path and in the host path and
    compares the recorded method words (the snapshot hook of `nvn_selftest_rec.c`). The first Y case is a fence after a clear and the
    CPU read of the cleared window image. Mutation: `nvn_selftest_mutate` flips one viewport word and the run must show a fail.
    The S case compares the host replay with the words that the library writes in command memory (not with the old path).

### Open for the user

- None blocks the start. Decision 7 adds a small public interface to `nvdrv`. If the user does not want a change in `nvdrv`, the
  other way is a private page table copy in the host (more code, and the renderer does not read it). The recommendation is
  decision 7.

### Work order of 3c

1. Pool mapping (decision 7) and the buffer handlers (decision 1), with host tests. Selftest: none. Needs no run.
2. The analyst task for the format word table, and the read of the `ClearColor` macro 1 (explorer), in parallel.
3. Command buffer record, handles and the recorder with host tests (decisions 3 to 5).
4. Replay and frame job (decisions 6 to 12). Run: the user starts the title scene with `nvn_native=1`.
5. Selftest cases S and Y, the mutation, the first execution test (decision 13). Run: `nvn_selftest=1`.

## Queue fences and flush jobs (4-u3-4, 2026-11-13)

The analysis for the execution test found three faults in decisions 8 and 10:

- The frame job signals its tokens in `frame_finish`, on the GPU thread. `ReportValue` and `ReportCounter` go through the release
  queue (`vk/release.c`), which performs them when the GPU has finished the submit. So `SyncWait` can return before the report
  word is in memory. On the console the fence is a release after the reports in the same stream, so the CPU sees them first.
- `QueueFenceSync` with nothing pending signals at once, also when a posted frame job has not run yet.
- `SyncWait` returns 0 after a real wait. NVN gives 1 (`CONDITION_SATISFIED`) when the sync was signaled during the wait.

Not a fault: a GPU copy result is an owned range (`vk/dma.c`, `vk_own_publish`). A CPU access to an owned page traps and lands the
bytes first (`memory/own.c`). Without traps the result lands at publish. So the CPU sees copy results without a host action in the
mapped-range functions, and those stay no-ops.

The new design uses the syncpoint of the queue, as the console does. Each `QueueRec` already has its own syncpoint (`channel_id`,
from `syncpt_alloc`), and the replay skips the library's own syncpoint methods (decision 9), so the host is the only user.

1. **Post.** One helper posts the pending frame of a queue: Present (with the window), `QueueFlush`, `QueueFinish` and
   `QueueFenceSync` with the flag `FLUSH_FOR_CPU` (bit 0) (without a window). It takes the pending frame, carries the init words
   once, promises one increment (`syncpt_promise(id, 1)`), keeps the value as the last posted value of the queue, gives this value
   to every sync whose newest token is in the frame, and calls `gpu_call` before it releases `objs_lock`. So no other thread can
   see the fence and queue a job that waits on it before this job is in the GPU queue (lock order `objs_lock`, then `g_qlk`;
   `gpu_call` only appends, and the GPU thread runs calls without `g_qlk`). Present always posts. A flush, a finish or a fence with
   an empty pending frame posts nothing: the last posted value already covers all the work of the queue.
2. **Job end.** After the replay and the flush of the 3D state, and before `stream_end`, the job increments the syncpoint through
   the stream (`stream_incr`, the order of `job_end` in `gpu.c`). The renderer performs this increment after the releases of the
   job, so the reports are in memory before the fence is reached.
3. **Job without a window.** It does not set the window target, does not show an image, does not count a frame (`g_frames`,
   cheats) and does not free a window image. The 3D state of the channel stays as the lists left it, as the console keeps the
   channel state between submissions. Its log line says `flush`, and a missing image is not abnormal for it.
4. **Sync.** States: `UNSIGNALED`, `PENDING` (a token in a pending frame, no value yet), `FENCED` (syncpoint id and value) and
   `SIGNALED` (window acquire). A new fence of a sync replaces the old one (a serial in the token). `QueueFenceSync` with an empty
   pending frame uses the last posted value of the queue, or the current syncpoint value when the queue never posted, and wakes
   the waiters on `frame_done_cv` (a waiter of an older token of the sync waits there).
5. **SyncWait.** `UNSIGNALED` gives 3. `SIGNALED`, or `FENCED` with the value reached at the call, gives 0. Otherwise the caller
   waits outside `objs_lock`: first on `frame_done_cv` while the sync is `PENDING` (a post broadcasts), then `syncpt_wait` with
   the time that is left. Reached after a wait gives 1, the end of the time gives 2.
6. **QueueFinish.** It posts the pending frame and waits with `syncpt_wait` for the last posted value. It waits for the GPU work
   and its releases, not only for the end of the frame jobs.
7. **QueueWaitSync.** The one GPU thread runs the jobs in the order of the posts, so a fenced sync and a `PENDING` sync of the same
   queue are in order. A `PENDING` sync of another queue is not posted yet, and the host cannot hold a job back until it is: the
   call returns 0 with a log line, not a false success (review of 4-u3-4).
8. **Command buffer fence.** `CommandBufferFenceSync` keeps its flag word in the op. When a submitted list (or a list that it
   calls) has a fence with `FLUSH_FOR_CPU`, `QueueSubmitCommands` posts the pending frame after the scan, as a flush.
9. **Queue initialized again.** A `QueueInitialize` on a queue that has a record drops its pending frame. A sync whose newest token
   is in that frame can never be fenced by it, so it goes back to `UNSIGNALED` (`SyncWait` gives 3) with a log line, and the host
   wakes the waiters on `frame_done_cv`.

Answered by the title run (2026-11-13, update 1.1.1, `nvn_native=1`): Arceus calls no `QueueFlush`, `QueueFinish` or
`QueueWaitSync` up to the title. It submits, fences and presents (469 each in a run of 15 s) and waits with `SyncWait` (938). The
flush jobs of that run all come from the execution test. Seen once in each run: the game calls `SyncWait` with no limit on a sync that
it never fenced (caller lr `aac7d20`). The host returns 3 (failed) and the game goes on. The SDK does the same for a sync of kind
0: `SyncInitialize` sets kind 0 (`d83b80c`) and the `SyncWait` table entry of kind 0 (`d83be68`) gives 3 without a wait. Open:
what the game expects there.

Open after the review of 4-u3-4 (the game does not reach these paths up to the title; decision of the user: record only):

- A flush job replays its lists with a new replay host (`frame_replay_lists`), so a bind that no draw used before the end of the
  job is lost for the next job of the queue. A frame job had the same limit before the flush jobs. Fix: keep the replay state of
  each queue across jobs, owned by the GPU thread, reset only with the queue.
- A cross-queue wait on a `PENDING` fence (item 7, and the command buffer `WaitSync` op, which only logs) does not hold the
  consumer back. Fix: keep the dependency and park the dependent job until the producer job ends.

## Stage 4 compare: the draw record (2026-11-14)

The exit condition of stage 4 asks for a comparison of `nvn_native=0` and `nvn_native=1` in the target scenes. The user chose
the scenes (Obsidian Fieldlands and a battle, one run with each path) and the method: the runtime records its own draw state,
and a script compares the two records. Screenshots are not used.

### Facts that decide the design

Checked in the code on 2026-11-14 (analyser report, five claims checked again by the caller):

- Both paths call the same `Renderer` through `g_renderer` (`renderer.h:152`). It is set once at static init
  (`vk/renderer.c:924`). Only the selftest swaps it for one job (`nvn_selftest_cmd.c:369`, restored at :421). No code calls
  the Vulkan backend directly for drawing.
- Both paths put the 3D state into `GpuChannel.r3d[]` through `maxwell3d_method`. The old path decodes the game's stream.
  The native replay writes the methods on the private channel of the queue (`nvn_native.c:5089`).
- The GPU addresses are not equal. The native path takes its own pool addresses from `nvn_gpu_va_alloc` in another order,
  and its shader base is the host constant `NVN_SHADER_REGION_BASE` (`nvn_native.c:327`, written at :5579). So no address,
  host pointer, channel id or predicate goes into a record.
- A frame ends at `present_buffer`. The old path calls it in `vi_show_buffer` through `present.c:25`. The native path calls
  it in `frame_show` (`nvn_native.c:6599`). Both run on the GPU thread as a `gpu_call` item and hold no runtime lock. The
  old path can drop a present whose key changed (`vi.c:227`), so the two runs are keyed on presents, not on game frames.
- The draw, clear, dispatch and copy calls run on the GPU thread. The backend takes only its own `vk_lock`.
- `shader_hash` (`shader/shader.h:175`) is backend independent and ends a program at its EXIT.
- `nvn_selftest_rec.c` is a test double (no forward, no lock, no frame end). Only its idea of a snapshot is reused.

### Decisions

1. **Setting.** `nvn_drawrec` (int 0 1, default 0, startup) in `settings.txt`, next to `nvn_native`. It works with
   `nvn_native=0` and `=1`. It does not need `nvn_trace`.
2. **Module.** A new file pair `src/nx/gpu/draw_record.{h,c}`. `draw_record_init()` is called in `snx_main` after
   `nvn_native_init()` (`main.c:956`) and before `video_init` (`main.c:981`), when no other thread uses `g_renderer`. It
   copies `*g_renderer` into a static `Renderer`, replaces the eight recorded fields with wrappers and sets `g_renderer` to the
   copy. A field that is NULL in the backend stays NULL. All other fields stay the backend pointers.
3. **Recorded calls.** `clear`, `draw`, `draw_texture`, `blit`, `dma_copy`, `blit_mem`, `dispatch` and `present_buffer`. Each
   wrapper records first, then forwards with the same arguments. The record step only reads its arguments, `ch->r3d[]`,
   `ch->rcomp[]` and guest memory (for the shader hash). It does not change them.
4. **Lock and threads.** One mutex of the module (`rec_lock`), a leaf lock. The wrapper holds it only in the record step and
   never while it forwards. The file writes happen outside the lock, from a copy made under it.
5. **Signature of a call.** A 64-bit hash of path-independent values only:
   - draw: `indexed`, `topology`, `count`, `instances`; for each enabled program of `MAXWELL3D_PIPELINES` (6 programs of 16
     words) the `shader_hash` of its code at the shader base plus its offset; for each color target that `RT_CONTROL` enables,
     the format, width, height and layer words (not the address words); the depth target format and size when depth is on.
     `first`, `base_vertex` and `base_instance` are left out: they can depend on the buffer layout of the path.
   - clear: the argument `v`, the clear color or depth value that `v` selects, and the format and size of that target.
   - draw_texture: the rectangle and step words (0x420 to 0x427) and the format of color target 0.
   - dispatch: the `shader_hash` of `code`, the grid and block sizes and the shared memory size of the QMD.
   - blit: format, linear, block height, width and height of both surfaces, and the rectangle arguments.
   - dma_copy: the flags (multi, remap, fill, ...), width, lines, line bytes, and the block linear flag and pitch of each side.
     `snap` is left out (only the old path takes a snapshot, `gpu.c:1150`).
   - blit_mem: bpp, the rectangle and `fmt`.
   - present_buffer: w, h, pitch, layout and fmt.
6. **Shader hash cache.** The hash of a program is kept in a fixed table keyed by its GPU address, so a draw does not walk the
   code again. A full table hashes without a cache and counts the misses. Changed 2026-11-14 (`4b9404d`): the game writes
   other programs at the same addresses, so the table checks an entry once in each epoch of the backend with its write
   tracking (`code_same`, `vk/pipeline.c:399`) and hashes it again when it may have changed. An epoch ends at each
   `mem_may_have_changed` of the renderer (a submitted job, the end of a wait, a frame start), not only at a present. A
   shader instruction invalidate (`method_barrier` kind 5) and a renderer `reset` clear the table, as the backend drops its
   address shortcuts there. The
   log line `draw record: N shader programs hashed again with a new hash` counts the hashes that the check changed.
7. **Per frame.** Between two presents the module counts the calls of each kind and adds the signatures into two frame hashes:
   one that depends on the order and one that does not (a sum). At `present_buffer` it appends one line to the frame file
   and resets the counts.
8. **Per run.** A fixed open-addressing table maps each signature to its count and to a short text of its first call (kind,
   topology, count, shader hashes, target formats and sizes). A full table stops new entries, counts them, and logs one line.
   The table is written to the signature file every `DRAWREC_TABLE_WRITE_FRAMES` presents and at a clean exit, through a
   temporary file and a rename, so a killed game keeps the last copy.
9. **Files.** In the game folder, beside the log: `<exe>.drawrec<N>.frames.txt` and `<exe>.drawrec<N>.sigs.txt`, where `N`
   is the value of `nvn_native`, so the two runs do not overwrite each other. A new run of the same path replaces them.
   An open or write error logs one line with the path and turns the record off. The game goes on.
10. **Compare.** A script `tools/drawrec_compare.py` reads the files of the two runs. The two runs are separate plays, so
    frames do not align. The script reports: the signatures found in one run only, ordered by count per frame; for the shared
    signatures, the ratio of their counts per frame; and the median, minimum and maximum of each call count per frame. It writes
    no file into the repository.

### Tests

- A host test `tests/draw_record.c` for the pure parts: the signature of each kind from a register array and a fake memory
  reader, the leaving out of the address words (a change of an address does not change the signature), the frame hashes
  (order dependent and order independent), the table (insert, count, full table), and the shader hash cache (graphics and
  compute keys, probe collisions, full cache, clear, the check once for each present).

### Work order (one session each)

1. Code writer: the module, the setting, the call in `main.c`, the test. Review, build, `ctest`, commit.
2. Code writer: `tools/drawrec_compare.py`. The caller checks it on two record files of one short run with each path.
3. The user runs Obsidian Fieldlands and a battle with `nvn_drawrec=1`, once with `nvn_native=0` and once with `=1`. The caller
   runs the script and reports the differences.

### Limits

- If the native path changes the shader code bytes (for example a patch of a program), each draw signature differs. The
  compare then shows it in the shader hashes of the signature text.
- The scene is played by hand, so a signature found in one run only can come from a different action of the player. Rare
  signatures are judged by their text, not by their presence alone.

### Result (2026-11-14)

Runs of the update program with `nvn_drawrec=1`. The compare output stays local.

- **Title screen, first run.** On the native path every compute dispatch had the block size 0x0x0, so `vk_dispatch` skipped
  it. Cause: `program_read_compute_in` read the compute words 5 to 12, 14, 15 and the flag bytes at wrong header offsets.
  The library reads words 5 to 12 at h+0x748 to h+0x764, words 14 and 15 as the u64 at h+0x778, and the flags as bit 0 of
  the bytes h+0x830 to h+0x832 for layout 13 to 16 (else 0). It stores the nine fragment bytes as 0 or 1 (d837c64). Fixed in
  `b2a302c` with the pure function `nvn_program_compute_in_from_header` and host tests. Run again: the dispatches have the
  block size 32x1x1 on both paths, and no signature is found in the native run only.
- **Obsidian Fieldlands and a battle, first pair.** Some draws had the same count and fragment shader in both runs but another
  vertex shader hash. Cause: the record's shader hash cache (decision 6) kept the hash of the first program at an address.
  Fixed in `4b9404d`.
- **Obsidian Fieldlands and a battle, second pair** (`nvn_native=0`: 3744 presents, `=1`: 3776 presents). Both runs ended
  normally, with no fault, drop or refusal. No vertex or compute shader hash is found in one run only. The draw that had two
  stale hashes in the first pair has the same hash in both runs. The dispatches match. The median calls per frame: draw 544
  and 514, clear 58, dispatch 5, blit 4 in both. The signatures in one run only are draws with the same shaders and another
  instance or vertex count (grass, particles, the camera place and the time of the play), and six clears found once each in
  the native run, of the same kind as the rare clears of the old run. Coverage: 99.5% of the calls of the old run and 99.9% of
  the native run are in shared signatures.

The compare finds no difference between the two paths in the target scenes, in the values that a signature records
(decision 5). The blend state, the viewports and the constant buffer bindings are not in a signature, so this result
does not cover them.
