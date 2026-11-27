<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Arceus timing above 30 FPS

Pokémon Legends: Arceus 1.1.1 runs its world at 30 updates per second. Most of its clocks add a fixed 1/30 second
on each rendered frame, so above 30 FPS the game runs fast. `src/nx/runtime/arceus_clock.c` in the vendored
ScarletNX changes those clocks to the real elapsed time. The timing fixes are on by default and can be disabled with the master or individual switches below. Setting
the master to `0` also suppresses the opt-in traces and research probes (the fixed resolution and Discord
presence without the timing layer, `SNX_ARCEUS_DISCORD_BARE`, still run). The layer does
nothing for another game or another build. Each change first checks the build id of `main` and the instruction
words it replaces, and it logs a refusal and changes nothing when they differ.

## Switches

Unset timing switches use the defaults below. `SNX_ARCEUS_TIMING_FIXES=0` turns off the entire timing layer. For every individual fix, setting its switch to `0` turns off that fix; probes and graphics settings remain opt-in.

The `SNX_ARCEUS_*` switches are environment variables, read with `getenv`. No `.ini` setting holds them.

| Switch | Default | What `=0` does |
| --- | --- | --- |
| `SNX_ARCEUS_TIMING_FIXES` | on | Installs nothing and suppresses the traces and research probes too. The throw owner guard, the fixed resolution (`SNX_ARCEUS_FIXED_RESOLUTION`) and Discord presence without the layer (`SNX_ARCEUS_DISCORD_BARE`) are not part of the timing layer and stay on. |
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
| `SNX_ARCEUS_EVENT_TICK` | on | Keeps event scripts on every frame. On, they run 30 times a second. |
| `SNX_ARCEUS_DEMO_TICK` | on | Keeps the demo clock at 2 a frame. On, it steps by elapsed time. |
| `SNX_ARCEUS_TEXT_TICK` | on | Keeps the message text reveal on every frame. On, it reveals 30 frames a second. |
| `SNX_ARCEUS_TEXT_TICK_END` | on | `0` runs a waiting text's step on its tick only, so above 30 FPS a press can fall on a frame that does not see it and a save box can stay open. On, a press is seen on any frame. |
| `SNX_ARCEUS_THROW_OWNER_GUARD` | on | Lets the game make a throw whose owner is already gone, which crashes. Not a timing fix: it installs at every target rate and with `SNX_ARCEUS_TIMING_FIXES=0`. |
| `SNX_ARCEUS_CLOCK_TRACE` | off | `=0` keeps the trace off; set to `1` to log clock calls. |
| `SNX_ARCEUS_PERF_TIMES` | off | `1` logs a `perf-times:` line every 600 presents on native: the frame job's cycle (post to start, start to end, end to the next post, the period), the GPU thread's idle and blocked time by site, the release thread's `vk_lock` holds, the game's `nvnSyncWait` and `nvnWindowAcquireTexture`, and the storage bytes landed a frame. It also times a present job's tail, each from its syncpoint increment queued at the job's end: to the recorder thread handing the submission it waits for to the driver (`tail_submit`), to the release thread's wait for it returning (`tail_done`), to the increment performed after the landings (`tail_incr`), to `vk_lock` released (`tail_free`) and to the game's next `nvnSyncWait` returning (`tail_wake`), with the submissions from one present's increment to the next (`tail_submits`), those not complete when it was queued (`tail_after`), and the tails without a submission to wait for, closed with a time missing, or lost. The tail times are printed as the mean over the tails that have them, the largest and how many, and the three misses as totals. They are CPU times on the threads that reach each point, and diagnostic: `tail_free` is the end of the `release_through` that performed the increment, which can sit inside an outer hold of `vk_lock`, `tail_wake` is the next return of any `nvnSyncWait`, and a slot reused before it closed can mix two tails. And a frame's render passes: game passes begun (`passes`), open passes ended by the reason their caller gives (`end_gpu_src` for the barrier before a draw that reads an earlier draw's result, `end_copy`, `end_image`, `end_target`, `end_query` for a samples-passed report, `end_other`), `gfx_gpu_src`'s barriers (`src_barriers`), the ones whose result no draw bound for writing since the barrier before (`src_clean`) or whose last writer was not kept (`src_unknown`), and the GPU thread's time ending passes (`pass_end`) and beginning them (`pass_begin`). `src_clean` judges a barrier by the first earlier result its draw reads: another result the same draw reads may still need it, so it can count a needed barrier, and barriers other than `gfx_gpu_src`'s are not counted. `pass_begin` starts at the end of the pass before, after the draw's own copies and binds. And a frame's graphics submissions by the site that made them (`sub_slice`, `sub_release`, `sub_finish`, `sub_frame`, `sub_present`, `sub_image`, `sub_compute`, `sub_readback`, `sub_other`), each as all of them, then those queued in the last 1, 2 and 4 ms before the present's `frame_end`, and per submission the time from the recorder's `vkQueueSubmit2` returning to its completion first seen, for all (`sub_done`) and for the last five a present queued before its `frame_end` (`sub_last5`). A completion is seen where the runtime already reads the timeline (a wait returning, the counter read at each submission), so these times are upper bounds. `sub_lost` counts submissions whose completion was never seen before their slot was needed, and `sub_unhanded` those seen complete before the recorder's handing time was published, which have no `sub_done` sample. Both are totals. A reused slot can still mix two submissions' times. And a tail's window, from the increment queued at `frame_end` to `tail_done`, split by what the release thread did in it: blocked in its waits for the GPU (`win_gpu`), waiting to take `vk_lock` (`win_lock`), holding it to land and release (`win_hold`), and the rest (`win_other`), each the mean over the tails, with the storage outputs landed in the window (`win_outputs`), the releases queued and not yet performed when the increment was queued (`rel_depth`) and the release thread's wait for `vk_lock` over the whole frame (`rel_lock_all`). And the GPU thread's CPU time in buffer.c (PERF-18): `gfx_gpu_src` past its page test, its walks over the pending results up to a landing or barrier (`src_walk`, with the calls `src_walks` and the results the first walk passed `src_results`), PERF-17's class walk (`class_walk`, the line's own cost), and the compositions for a read (`compose_empty`, `compose_copy`) and for a writable binding after `gfx_gpu_src` (`compose_w_empty`, `compose_w_copy`), split by whether a byte was copied. These are clock reads around the code, so they include the reads' own cost. A second line, `perf-copies:`, counts a frame's buffer copies by call site (`compose` and `compose_w` for pending results copied over guest bytes for a read or a writable binding, `atlas`, `owned_base`, `patch`, `own`, `owned_read`, `dma`), each as copies, the ones that ended an open pass and the KB they moved. It counts what `gfx_gpu_src` did for bindings over pending results, by the caller (`in` for inputs, `rd` for storage reads, `wr` for writable bindings): bound in place, composed by a copy because several results cover the range, one covers it in part, its offset is not accepted or a writable binding's result is from an earlier submission, or landed first because the result is of an earlier frame or the guest changed its bytes. The same counts are kept by the first target of the pass or draw (color slot 0, else depth, as the GPU profile names a pass), with the target's draws, passes begun, passes ended by reason and copy pass ends by site. The four targets with the most copy pass ends and reads not bound in place are printed. A draw set up again counts again, and calls off the GPU thread are only counted (`elsewhere`). A third line before it, `perf-inplace:`, is from the same window (PERF-17). It sorts each input binding that `gfx_gpu_src` composes, by the draw's first target, by the pending results that share a byte with its range: none (page neighbours only), one (the newest covers it), one at the vertex stream's exact length, a run (several at one address each, tiling the range in one buffer with the guest's spacing), several, or part. It counts the same outcomes as `perf-copies:` by what is bound (vertex stream, index, constant buffer, storage, transform feedback, dispatch), the writable results handed out by producer (ordinary, over an owned baseline, composed, transform feedback, dispatch, atlas page, rewritten in place, a repeated binding), compositions that copied no byte with the bytes they staged, vertex streams whose last vertex was unknown because the indices share a page with a pending result, streams looked up at their exact length, the reuse barriers recorded and skipped, the writable compositions that entered `gfx_overlay` and those that skipped it at `SNX_ARCEUS_PERF_INPLACE=4` (`overlay_w_entered`, `skipped`), the samples-passed reports queued (`query_reports`) and of them those that left the pass open (`kept`) or ended it because half the query ring was not copied yet (`guard`), and the atlas pages made since the start as an integer (`atlas_total`), which a window does not reset. A fourth line, `perf-writable:` (PERF-20), splits `vk_buf_copy_writable`'s new results by path (plain, composed with no byte copied, composed with bytes, over an owned baseline), each as calls and KB a frame and the milliseconds of each step (setup, the record, the original bytes, `out_map`, the staging allocation, the copy into staging, the overlay, the rest). It counts the transform feedback bindings by whether the draw provably writes all of the bound range (`whole`: the captured vertices times the stride, from a guest topology drawn as itself), writes less (`short`), or cannot be told (`stage`: a geometry or tessellation stage, `resume`: written from its counter, `topology`: line loops, quads, quad strips, polygons, adjacency, restart in an indexed strip), with the KB bound and those provably captured. Results that fail partway are not counted. A fifth line, `perf-own:` (PERF-22), counts `buf_get`'s slow path past its checks by what it did (`staged` into this frame's ring, `own_made`, `own_same` for an own buffer hashed and unchanged, `owned_read`, `patch` for an own buffer's dirty runs) and why (a new entry, no buffer, a copy of an earlier submission, a GPU-side write, a CPU write, the check epoch), each with its KB, how many lie inside a graphics result that landed this frame or the one before (a table of landed ranges by guest page, the first 16 pages of a result and 8 results a page, so a census), and its milliseconds split into the guest bytes with their hash and copy, the buffer creation, and the rest. It counts the draws on each own buffer when it is dropped (0, 1, 2 to 3, 4 to 15, 16 to 63, 64 or more), and the same outcomes by the draw's first target, each also counted inside a landed result. A patch counts its range's bytes, not the runs it copied. A range of more than 16 pages meets a result on another address by page (`wide`). Log only. A seventh, `perf-same:` (PERF-23), follows it from the same window. Every GPU-side write is kept by GPU page, to the byte, in two records: as stamped (`gpu=`, what the dirty test reads, where a landed result stamps its whole binding once any byte of it changed) and, only with `SNX_ARCEUS_PERF_TIMES=2`, as written (`real=`, where a result counts only its runs that changed, as landed, and with `SNX_ARCEUS_PERF_LANDING=3`'s 64-byte masked stores each step from its lowest changed byte to its highest). An own buffer found unchanged after a GPU-side write is asked of each: `out` (no byte of its range written since its last check, and the range outside every byte ever written on its pages, in 16 byte steps), `span` (none written but inside that span), `span_unknown`, `in` (a byte in range written) or `unknown` (not kept: a page that made room or was made again since, too many wide writes, or every page written at once). For the first three it counts who wrote on the same pages (a result's binding, another GPU-side write, an ownership publication through `gpu_mem_written_cpu`, a result's changed run). `cleared=` counts them by the tests that clear one: no byte of its range in a binding as stamped (the cheap test), its range also outside its pages' spans as stamped, no byte in a changed run as written (what an exact test would clear), the first and third together, and the second and third (all three, since the span clears only what the bytes clear), of all of them. Each record keeps six runs a page, the two closest merged when a write makes more, so `in` may count a few that are really clean. One found unchanged after a CPU write is counted by whether the write tracking saw it, at or past the patch size, its KB and the KB on the host pages written since its check. Those pages are counted in the window: distinct, written in every frame, in half the frames or more. The slow path's time is split by step (a patch that failed before it, the written check, this probe's reason test, the owned-range check, getting the bytes, the hash, the sampled hash, `use`, the rest) with the calls each ran in, by outcome. For the patch outcome the first step is the patch itself. Also the written checks that found nothing, the full checks that took no sampled hash (`sample_skipped=`, with `SNX_ARCEUS_PERF_SAMPLE=1`), this probe's own asks and page walks after a slow path's end (outside every step), each record's writes, and what one clock read costs. The records take about 32 MB once the switch is on. The record as written took about half the frame rate in T23 (some 105,700 writes a frame), so `1` keeps only the one as stamped: `perf-same:` then leaves out `real=`, `real_writers=` and `written=`, and prints the cleared counts that need it as `-`. A `1` line from a build before cd60794e also held the record as written, so its times are not one series with later ones. `2` keeps both. An eighth, `perf-landed:` (PERF-25), counts the `buf_get` lookups that end staged or as a new own buffer inside a graphics result landed in this frame or the one before (the PERF-22 table, a result's first 16 pages): the whole result, a part of it or two adjoining results, the submissions since it landed, whether the staging that held the result's GPU buffer was reused and how many submissions before (the KB of those results, each once), and whether anything wrote the range since its landing: a later result over a byte, a GPU-side write by page and by byte (the record as stamped), a CPU write by page. Cleared by page tests means none of the page tests found a write, cleared with the byte record that only the byte test cleared the GPU-side one. Also their milliseconds at the site and the passes they ended, by target. To see the CPU write after a landing, the probe asks the write tracking about the result's pages as it lands, which takes in the landing's own writes, and asks again at the lookup. Log only. A ninth, `perf-captures:` (PERF-27), counts the transform feedback captures a frame (one range bound twice in a frame once): how many, how many did not fit the probe's 4096 a frame, the distinct ones, the next one in address overlapping, abutting, on the same 4 KB page or apart, those at the same address and size as the frame before, those with another GPU-side write or an ownership publication on their pages since the frame before began (the record as stamped, so a neighbouring result's landing is not one), their KB and their sizes. It splits the landing of every graphics result, on the release thread and elsewhere, into invalidating, comparing and writing the changed bytes, unmarking its pages, finding its changed runs, walking the later results for the rebase, and the rest (which holds PERF-25's own ask), with the walk's results passed, composed ones asked and met, the landed results' changed runs (the runs a result divided by the results landed) and the landed results whose own write found them (`SNX_ARCEUS_PERF_COMPARE=2`). And it splits the release thread's hold into the compute landing, the graphics landing and the releases performed. Log only. With `SNX_ARCEUS_PERF_PROBES=1` as well, a tenth, `perf-p29:` (PERF-29), splits three costs by their parts, at a cost in the code it times, so runs that compare times leave it unset. `vk_hash` and `vk_hash_copy` by caller: each caller's return address less `vk_hash`'s own (printed once), to look up in the build's symbol list, with its calls, KB and the milliseconds of its calls of 1 KB or more, the 24 first callers in slots of their own and the rest together. The replay's draws, one in 16 of them timed whole (its binds' words built and sent, and the draw's) and by their sends, each send after a pass that only decodes the same words, so that building, decoding and the methods sent can be told apart, with the words and methods of a timed draw. And the release thread's releases performed, by kind (syncpoint increment, payload, samples report): how many, their own milliseconds, and the milliseconds of the queue's bookkeeping around them under its lock. Log only. With it an eleventh, `perf-mem:` (PERF-31), says where kernel time goes: the process's page faults a frame, the kernel and user milliseconds a frame of the GPU, release and recorder threads and of the guest thread that posts presents (each thread reads its own times at most every 50 ms, so a value can be that old, and a slot keeps the first thread that stores to it, with the others' stores counted), the blocks of 4 KB or more made a frame by site (a result's block, with those under 4 KB apart, a page map, a list, an own buffer), and the results' blocks freed on the release thread and elsewhere. Log only. A twelfth, `perf-waits:` (PERF-32), counts a frame's waits and wakes by thread (GPU, release, recorder, the presenting guest thread, the write scanner, other) and kind: a contended lock, a condition wait, a signal or broadcast call, a yield, a sleep, a wait for the graphics timeline, a write watch scan, the recorder's submits and command buffer ends, and a payload release's write in its three steps (the before-write hooks, the copy, the written notice) and a samples report's sum, each with its ms, then the 12 callers with most ms at their offset from `pm_wait_note`. Log only. A thirteenth, `perf-handoff:` (PERF-33), describes the frame hand-off at each frame's increment: the frame's submissions, those a result for the release thread was queued in (`vk_release_result`, the one place every producer calls) by kind, where the newest of those sits before the last submission, the ms from frame_end to its last landing, to the GPU's end of that submission and of the last, and to the increment, compute landings after frame_end, landings from a submission never registered, the same position with the storage images left out, and the calls and ms of `nvnQueueSubmitCommands` (its lock wait, list add and scan apart, with the ops walked whole and the ops read at their marks under `SNX_ARCEUS_PERF_SCAN`), `nvnQueueFenceSync` and `nvnQueuePresentTexture`, and the storage images written a frame with their draws' places after frame_end and the last one's place, its frame's draws, guest address, size and format. Then the frame's last submission's results by kind (how many, and in what share of frames), each kind's last registration's mean place among the frame's draws, and the frame's mean draws. A fourteenth, `perf-land:` (PERF-35), counts a frame's landed storage results and their bytes by page: all, those on pages with no CPU write since the result's original was read, no other GPU-side write over them since, no composed result there, and all three, with the bytes whose page could not be told, and gives one bracket around the landing's loop and one around a plain copy of the same bytes after it, and the masked pass's 64-byte steps by their final mask (none stored, all 64, some). Log only. A fifteenth, `perf-vklock:` (PERF-36G), counts a frame's contended takes of `vk_lock` by thread and by what the holder was doing as the wait began (the release thread's compute landings, graphics landings or releases, or another hold), with the GPU thread's 8 callers of `vk_lock` with most ms at their offset from `pm_wait_note`, each with its ms by holder. Then the render targets made pending over pages, ownership armed, the release thread's holds and those that found an owned range, the result lookups made with no `vk_lock` held by entry, and the composed captures and those whose bytes meet a result of a submitted frame. Log only. |
| `SNX_ARCEUS_PERF_LANDING` | 3 | Part of the FPS play set, on by default, and `0` or any value that is not one of its levels turns it off. `3` lands storage shader results with 64-byte stores under a byte mask (AVX-512BW) instead of byte loops (`out_changed_runs`, `vk_write_changed`): the same runs and the same bytes written to guest memory as with it `0`, with a check-to-write step of one 64-byte step. On a CPU or OS without AVX-512BW, for a result outside one contiguous mapping and for the tail under 64 bytes, it uses word compares, with the same bytes. Its start line says which. In the village bench it took the release thread's hold of `vk_lock` from 3.6 to 1.56 ms. `1` (word compares alone, no gain) and `2` (a whole-block copy, not exact) were removed: they now land as with it `0`, and the start line says so. |
| `SNX_ARCEUS_PERF_REPLAY` | 3 | Part of the FPS play set, on by default, and `0` or any value that is not one of its levels turns it off. `1` stops each of the native replay's bind walks at a draw (uniforms, storages, textures, vertex buffers per stage) once its count of set dirty flags reaches 0, instead of testing every flag. The same binds are sent in the same order. `2` also sends a draw's bind and draw words to the 3D engine as the builders write them (PERF-30): each word goes through the same decoder at once, not into an array decoded after, so the engine gets the same methods in the same order and a damaged group is refused as before. Not for the draws PERF_TIMES times, and not for a draw's own words while register 0xD18 is 0, which the draw's check refuses after its words are built. `3` also lets the header helpers set the decoder's state themselves when it is idle and their arguments fit a header's fields (a 12-bit method, a count of 1 to 8191), an inline value make its one call, and a run's data words take the decoder's branch for an open run inline. Anything else is written and decoded as at `2`. |
| `SNX_ARCEUS_VK_VALIDATION` | off | `1` or `2` turns on the Vulkan validation layer (`vk_validation`). With the layer on, the runtime also asks for its synchronization checks and turns off its limit of repeated messages, each where the layer lists the extension for it, and logs what the run got. `2` asked for the synchronization checks when `1` did not; it now does the same as `1`. It is not written to the settings. It finds hazards only in the commands a run records. |
| `SNX_ARCEUS_PERF_QUERY` | on | Part of the FPS play set (PERF-19), on by default, and `0` or any value that is not one of its levels turns it off. `1`: a samples-passed report (an occlusion test's result) ends its counter's open query inside the pass and leaves the pass open, and its counts are copied at the end of the submission it waits for, as every submission's end already copies them, instead of at the report with a pass end, a copy and a barrier each. The predicate batch copies the counts it reads before its barrier. When half the query ring is not copied yet, they are copied as a report did (`vk_query_room`), checked at each report, reset, enable and before each draw, so no count is made up because of the switch. A count is the same either way: the draws between a segment's begin and end. |
| `SNX_ARCEUS_PERF_SAMPLE` | on | Part of the FPS play set (PERF-24), on by default, and `0` or any value that is not one of its levels turns it off. `1`: with write tracking on, a buffer's full check takes no sampled hash. `buf_get` sampled every own buffer that had been the same 63 times and is 4 KB or more (64 reads of guest memory, 461 a frame and 1.3 ms in T23), and only its check without write tracking reads the sample. A full check that skips the sample flags the entry, a sample taken clears the flag, and that check does not take a flagged entry, so write tracking lost later sends it to a full check instead of comparing an older sample. With it `0` the flag is never set. |
| `SNX_ARCEUS_PERF_COMPARE` | 2 | Part of the FPS play set, on by default, and `0` or any value that is not one of its levels turns it off. `2` (PERF-28 1b and 3): a landing that goes through `SNX_ARCEUS_PERF_LANDING=3`'s 64-byte masked pass over its whole result takes its changed 8-byte words from that pass's own compare of the result with its original bytes, and the rebase uses them in place of a second pass over the result. Any other landing (no AVX-512BW, no host pointer, a result that changed nothing) finds them as before. And the results not landed yet are kept by their mapped guest pages (and under one key when a result has an unmapped page), so that a landing's rebase walk visits the composed ones on its own pages, and the GPU thread's walks over pending results for a binding (`gfx_gpu_src`) visit the ones on the range's pages, each in the order they were made, not every pending result. A range of more than 16 pages, or no memory for those lists, walks every pending result as before. |
| `SNX_ARCEUS_PERF_SCAN` | on | Part of the FPS play set (PERF-33 part 2), on by default, and `0` or any value that is not one of its levels turns it off. `1`: every command list keeps the offsets of its fence, wait and call ops as they are appended, and `nvnQueueSubmitCommands` reads those ops at their offsets, in list order, instead of walking every op of every list it submits. A list that could not keep them all (no memory) is walked whole as before, and so is a call's target without them. At `0`, no offsets are kept and every list is walked. |
| `SNX_ARCEUS_PERF_STORAGE` | on | Part of the FPS play set, on by default, and `0` or any value that is not one of its levels turns it off. `1`: the release queue walks that look for a storage range translate it once, not once for each queued release, and the match stops at the latest one. They find the same releases (`release.c`). Its start line is `vulkan: SNX_ARCEUS_PERF_STORAGE: ...`. |
| `SNX_ARCEUS_PATCH_HASH` | off | A fix, off by default. `1`: an own buffer patched in place (`patch_own` for CPU writes, `patch_dirty` for GPU-side ones) is marked, since its hash and sample are still those of the bytes last hashed. A marked buffer skips the three checks without write tracking, and its next full check takes it as changed whatever its hash: hashed, uploaded again, the mark cleared. Without the switch, bytes that change and change back after a patch (A, B patched in, A again) match the old hash, and the draw reads B until the bytes change again. A failed patch, or one with nothing to copy, does not mark. |
| `SNX_ARCEUS_FRAME_LOG` | off | Log only (DAILY-LT). `1`: the time between the game's present posts on native, where `perf-times:` takes its period, in microseconds, every one. Each 600 presents give four `arceus-frame-log: presents A-B us: ...` lines of 150 intervals, written on the GPU thread after the present that filled them. A buffer that fills while the last one is still being written is dropped, `lost=` on the last line counts those. It costs one clock read a present and does not need `SNX_ARCEUS_PERF_TIMES`. The intervals are the game's submission pacing, not the display's. |
| `SNX_ARCEUS_LATENCY_PROBE` | off | Log only (DAILY-LT). `1`: a scripted press (`script`, the unattended presses) is stamped at the HID update whose pushed state first carries a newly pressed button, the first state the guest can read with it: `arceus-latency: t0 at_us= mask= sample=`. The host's controller path and the 60 Hz poll before that update are not in it. Each game frame's picture is blitted to 64x36 RGBA8 and copied for the host, read when its submission is done (never waited for), and compared with the frame before: the mean absolute difference over red, green and blue. The 30 presents before a press and the 90 from it on get an `arceus-latency: present N at_us= diff= jobs= acquire_us= shown=` line each, every present once, with its post time (the clock of `t0`), the guest frame jobs already in flight at its post, the game's acquire call for its image, and whether the swapchain had an image for it. Swapchain images presented and not acquired again do not count the frames queued for the display, since the runtime acquires one image a frame without waiting: once each image was used, all of them count. |
| `SNX_ARCEUS_PERF_INPLACE` | 4 | Part of the FPS play set (PERF-17), on by default, and `0` or any value that is not one of its levels turns it off. Each level adds to the ones below. `1`: a read binding (vertex, index, constant or storage) binds in place the newest pending result that shares a byte with its range when that result covers the range at its address, where the newest one by page (a neighbour on the same 4 KB page) made it compose. The checks that follow (the guest's bytes unchanged, the result's frame, the offset) are the same. `2`: a vertex stream with pending results on its pages is looked up at the bytes it reads, not rounded up to a power of two, so one result covering them can be bound in place. `3`: a read skips the reuse path's barrier, and the pass end with it, when that barrier was recorded in this submission and no draw or dispatch was handed a writable result since: the barrier is a global memory barrier from the stages and accesses that write results to all graphics and compute accesses. Every producer of a writable result marks the work index (vk_buf_copy_writable's paths, the atlas's pages, a rewrite in place), and vk_buf_writable_commit marks it again when it publishes. It does not cover attachment writes, image layouts or queries, which keep their own paths. `4` (PERF-21): a writable binding over pending results skips `gfx_overlay` when no pending result shares a byte with its range (`out_meets`, a page in common counting when no page map can be made). `gfx_overlay` copies only such bytes, so with none it records nothing: the binding still takes the composed path, with the same record, original bytes, staging and landing. Writable bindings are otherwise unchanged at every level. |
| `SNX_ARCEUS_DIALOGUE_TRACE` | off | `=0` keeps the read-only trace off. |
| `SNX_ARCEUS_CAMERA_TRACE` | off | `=0` keeps the read-only trace off. |
| `SNX_ARCEUS_TIMING_RESEARCH` | off | `=0` keeps read-only timing counters off. |
| `SNX_ARCEUS_CADENCE_CENSUS` | off | `=0` keeps the read-only census off. |
| `SNX_ARCEUS_GRAVITY_PROBE` | off | `=0` keeps the read-only probe off. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | on | It is on unless set to a value other than `1`; then it keeps the game's automatic 3D resolution governor, which lowers the scene size after a slow frame. It applies at every frame rate, the game's own 30 FPS and `SNX_ARCEUS_TIMING_FIXES=0` included, since it needs nothing of the timing layer. The game's own scripted size changes stay either way. |
| `SNX_ARCEUS_DISCORD` | on | It is on unless set to a value other than `1`; then it is off. With the player's `SNX_ARCEUS_DISCORD_APP` it shows Discord status (D2, `arceus-discord:`): the title screen, the region from the pack the game reads, the battle's kind from the files its start reads (D3: a trainer, "Battling <name>" once the battle's line "<name> sent out <species>!" gives it (D5, English only, in another language "Battling a trainer"), "Fighting noble <species>", an alpha or a wild opponent's species, "Fighting" for a legendary, else "In battle"), a noble's whole fight from its sound bank to its win scene or its loss (D3c, D3d), in a battle the player's Pokémon as the second line (D3d), and the party in the image's hover text. Discord's replies are logged on change, a refusal with its code and its message, every digit masked. Species and levels come from copies of the game's records deciphered by our own code. A battle ends 3 s after its effect sequences stop, which needs `SNX_ARCEUS_BATTLE_SEQUENCE_FIX`, and the title screen ends at the camera's first input, which needs `SNX_ARCEUS_CAMERA_INPUT_FIX`. Without the first a battle ends only at an area name, without the second the title screen stays until the first battle. A warning names each that is off. With the timing layer (target 60, 120 or unlocked, with `SNX_ARCEUS_TIMING_FIXES` on) its tick runs in the layer's clock reader. At the game's own 30 FPS or with the master at `0` it runs without the layer (`SNX_ARCEUS_DISCORD_BARE`). With that switch off too it installs nothing, and when it was asked for (the switch or an application id set) one line says so. |
| `SNX_ARCEUS_DISCORD_BARE` | on | It is on unless set to a value other than `1`; without the timing layer Discord presence is then off. On: Discord presence without the timing layer (the game's own 30 FPS, another refused target, or `SNX_ARCEUS_TIMING_FIXES=0`). Its tick runs on the clock read's own dispatch entry, with no word of the game changed, and two counters stand in for the two fixes' counts, each on that fix's own JIT body with the fix's guards. The readers and the host half are the same. One `arceus-discord:` line names the three sites. |
| `SNX_ARCEUS_DISCORD_APP` | none | The user's Discord application id, digits only. Never in the repository; only its last four digits are logged. |
| `SNX_ARCEUS_EVENT_PROBE` | off | `=0` keeps the read-only event script probe off. |
| `SNX_ARCEUS_DEMO_PROBE` | off | `=0` keeps the read-only demo clock probe off. |
| `SNX_ARCEUS_TEXT_PROBE` | off | `=0` keeps the read-only text reveal probe off. |
| `SNX_ARCEUS_SCENE_SCALE` | off | Not a boolean: a number from 0.5 to below 1.0, or `p/q` such as `2/3` (also written `2of3`), the game's own 3D buffer scale. Unset, or 1.0 and above, changes nothing. Needs the fixed resolution, which is on unless `SNX_ARCEUS_FIXED_RESOLUTION` is set to a value other than `1`. See "Scene scale". |
| `SNX_ARCEUS_WSI_LOCK` | on | Not a timing fix. It is on unless set to a value other than `1`, and then the acquire and the present run without it, as upstream. On, the renderer holds one lock across each swapchain present and each image acquire, which upstream runs on two threads at once. Its start line is `wsi-lock: serializing swapchain acquire and present`. |

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

## Cutscenes and message text

Three parts of a cutscene count calls instead of time, and each is called once per rendered frame. At 60 FPS the
opening cutscene moved on between its texts about 1.7 times too fast and ended in half the Switch's time, and the
text in its windows printed about twice as fast, while animation, camera and sound kept their speed. Each part has a
switch of its own, on by default, and set to any value other than `1` it is off. None of them installs while the
timing layer is off, which it is with `SNX_ARCEUS_TIMING_FIXES=0` and, at the default frame-rate selection, at game
pace or 30 FPS and below. `SNX_ARCEUS_TARGET_FPS=game` keeps the layer on at the runtime's own pace, so there an
enabled switch also installs at a 30 FPS cap.

Event scripts. The event script job `main+15BA460` runs its Lua script once a call and resumes it through
`main+F7EC80`. The scripts read no time, so their waits are counts of calls. With `SNX_ARCEUS_EVENT_TICK` on, each job,
keyed by its address and its script context, runs on its own budget of the measured frame step: a run when the
budget comes within half a step of 1/30 s, at most one a frame, the carry capped at two runs. A skipped frame answers
0, as a script that waits does, and writes nothing. The pad natives `IsPadTrigger`, `IsPadRelease` and `IsPadRepeat`
see the presses of the job's skipped frames in its next run, for their own call only.

The demo clock. A demo's controller step `main+1458300` adds 2 to its clock at `C+7EC` once a call while it plays,
and the demo ends at a clock value. With `SNX_ARCEUS_DEMO_TICK` on, a call that added exactly 2 sets the clock to the
elapsed time in 1/60 s units instead, at most 2 a call, the carry capped at 4. A hold clears the carry, and every
other change of the clock is left as the game made it.

Message text. The text processor's reveal `main+FBFBD0` makes up to a quota of glyph steps (`main+2B315E8`) each call.
With `SNX_ARCEUS_TEXT_TICK` on, each text is decided once a frame by the event tick's rule. The reveal still runs every
call, so page input, the held button and the end of a text work as before. In a frame that is not the text's tick,
its glyph steps answer as steps that made no glyph. A fast finish and a whole page still show at once. A text not seen
in the previous frame frees its state.

Waiting text. The same glyph step holds the text's press tests: `main+FC10E4` in the wait at the end of a text (the
save's "You saved your progress!" box) and `main+FC1078` at a page break. A press is an edge the pad keeps for one
frame, so with the tick alone a press on a frame that is not the text's tick is never seen: at 120 FPS three presses in
four, and a box could stay through several presses. With `SNX_ARCEUS_TEXT_TICK_END` on (the default), a step the tick
would skip runs the game's step when the text's last real step answered other than 1, which is every wait (0 at the end,
2 at a page break, 4 once closed). A text being revealed answers 1 and is skipped as before. In the end wait of modes 2
and 3 (`+90`) the step counts the byte `+A3` toward `+A2` (`main+2B31D44`) and leaves when it passes. An extra step
that would leave is skipped, and one that counted gets the byte back, so the count stays on the tick. Every 5 s an
`arceus-text-tick: end=3` line gives the steps run, the counts held and the steps kept back. With
`SNX_ARCEUS_TEXT_PROBE=1`, a line when a text enters an auto-advance wait and one when it leaves.

The probes `SNX_ARCEUS_EVENT_PROBE`, `SNX_ARCEUS_DEMO_PROBE` and `SNX_ARCEUS_TEXT_PROBE` are read-only. They log the
calls a second of these functions, the demo clock at the end of a demo, and the text's glyph steps, quota and speed
settings, and they install at 30 FPS too. They measured each cause before its fix was built: the job and the demo step
once a frame, the demo ending at clock 3502 after 33.65 s at 60 FPS and 61.00 s at 30, and a text taking 205 ms at 60
against 467 ms at 30 for the same calls and glyph steps. With the three switches the demo ended at 61.40 s at 60 FPS,
and the user found the opening's timing and the text speed the same as on the Switch.

Limits. The battle's message windows wait for a press or for a count of calls (`main+C67F30`, mode 3, up to 40 counts
in the session played). The count is made in the glyph step that `SNX_ARCEUS_TEXT_TICK` paces, and with it on the user
found battle text at 60 FPS the same as on the Switch. The length of a wait was not measured. The state tables hold 8
jobs, 8 demo controllers and 16 texts. A job's state goes when the job finishes or its address holds another script,
a controller's after a second without a step while it plays, and a text's once the text was not seen in the previous
frame. A full table leaves that one as the game runs it, with a count, and the played sessions had none.

## Throw owner guard

`main+16E33EC` makes a throwable in heap 4 of the owner that its weak reference at `+88` names, and never checks
that the owner still lives. The owner's controller (update `main+18756E4`) releases it on its own schedule, and the
throw can come after that. The call then faults reading `+118` of a null owner at `main+16E34FC`. It was seen
entering the arena of the Arceus fight, with every timing fix off, and only with the native NVN backend.

The default-on `SNX_ARCEUS_THROW_OWNER_GUARD` wraps the function. When the reference is empty, its use count is 0 or
its object is cleared, the call returns the empty handle the function starts from, and the throw is not made. Its
only caller, `main+19D2368`, accepts an empty handle, which its own early exits also leave. Any other call, or one
whose memory cannot be read or whose handle cannot be written, runs the game's code. The only caller is a direct
call, so the guard changes `MOV X21,X0` at `main+16E3408` to the equivalent `ADD X21,X0,#0`. That marks the
compiled function stale, and a stale function sends its entry through dispatch to the wrapper. It refuses unless
one function of 0xA70 bytes starts there and three instruction words match. It logs its install, the first call
that reaches it, and up to 16 guarded calls. In a native run at 30 FPS it guarded three calls, all with an empty
reference, and the game did not crash.

## Scene scale

The game can draw its 3D passes into the top left part of its full-size targets. Two scales, width and height,
say how much, and they change viewports and scissors only. Its mode setter (`main+CC3888`) takes the pair for the
default mode from a table in the game's data, where it is 1.0 and 1.0 at `main+3978758`. Its automatic governor
(`main+CC35A4`) lowers the pair under load.

`SNX_ARCEUS_SCENE_SCALE` writes one number over that default pair, both ways the same, before any of the game's
code runs. The game then applies it as its own scale. It is an experiment for an upscaler to come, and off unless
set. On its own it gives a softer picture, since the game stretches the part it drew over the screen.

Rules. The value is one finite number. From 0.5 to below 1.0 it is used as written. It may also be written as
`p/q`, two whole numbers with 1 <= p < q <= 64 and nothing else, which is divided once (`pofq` is the same
thing for a launcher that cannot pass a slash): two thirds has no short
decimal, and the game draws a viewport of the target's size times the scale as it is, so `0.6667` ends inside
a pixel where `2/3` does not. Unset, or 1.0 and above, is
off: nothing is touched and no line is written. Anything else (below 0.5, text, text after the number, not finite)
is refused with a line and nothing is touched: a value is never moved into the range. It needs
the fixed resolution (on unless `SNX_ARCEUS_FIXED_RESOLUTION` is set to a value other than `1`) to have turned the governor off, and refuses otherwise. The pair is written only
when both of its words are exactly 1.0 and the pair after it is exactly 0.5 and 0.5, which is the evidence that
this is the table. A refusal by this guard logs the table's ten words.

The table is in the game's read-only data. That is the guest's permission. The host commits every mapped guest
page readable and writable, which the patches to the game's code rely on as well, so a table that lies wholly in
mapped memory can be written, and one that does not is refused before anything is written. After the write both
words are read back. If they are not what was written, 1.0 and 1.0 are put back and the switch refuses.

What the game makes of it is logged by the renderer, read only (`arceus-scene-scale` lines): the used fraction
the game puts into its scene block (constant bank 3, at +628 and +62C), and the viewport of draws with three
colour targets or more and a depth target, per depth target. One line per change, 96 lines at most. The line of
the write says what was asked. These lines say what the game uses. A written table alone does not show that the
game's mode took the default pair.

Limits. Not played. Whether every pass stays inside the part that is drawn is not known: borders and effects that
sample neighbours (bloom, reflections, ambient occlusion, depth of field) are what to look at. With
`SNX_ARCEUS_TEMPORAL` on, the temporal passes are as they were: their viewport gate refuses a frame whose scene
viewport is not the whole target, so there is no jitter and no resolve under a scale below 1, and their lines
count those frames.
