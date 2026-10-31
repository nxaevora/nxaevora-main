# Stutter analysis

Subject: the short stops that the user sees in the recompiled game. Data: the logs of the runs F to J (see
`docs/jit-analysis.md`).

## What ScarletNX does already

| Setting | Default | Meaning |
| --- | --- | --- |
| `prepare_shaders` | 2 (before starting) | The shaders of the game are made ahead of time. A loading screen waits for them and for the pipelines of earlier runs. |
| `pipeline_threads` | 0 (automatic) | The number of threads that make new pipelines while the game runs. 0 means all threads but two. |
| `async_pipelines` | 0 (off) | 0: the game waits for a new pipeline. 1: what a new shader draws is skipped until its pipeline is ready. |

The files are in `game/recompiled/<id>/shader_cache/`:

- `<program>.vkshipped` (45 MB): the shaders that the game has in its files.
- `<program>.vkshaders` and `<program>.vkscan`: the shaders that were translated, and the scan.
- `vk_pipelines.bin` (11.7 MB): the pipelines of earlier runs. At the start, the runtime makes them again in the
  background (314 pipelines in 22 s in run I).

## Why not all pipelines can be made ahead of time

A pipeline depends on the draw state that the game sets at run time (formats, blending and so on). The runtime
learns a pipeline only when the game first draws with it. The shaders can be made ahead of time, and the pipelines
of earlier runs can be made again. A pipeline for a state that was never seen cannot be made before it is seen.

The cache is keyed by the GPU, its driver and the build (see `docs/AOT_PACKS.md` of ScarletNX), so a cache cannot be
shipped for all users.

## Measurements

Run J (252 s, into the Obsidian Fieldlands): 21 frames took 50 ms or more. Only 1 of them made pipelines.

| Time | Frame | Pipelines made | Waited |
| --- | --- | --- | --- |
| 179.47 s | 241 ms | 18 (and 1 shader) | 186 ms |
| 180.43 s | 118 ms | 0 | 111 ms |

The other 19 slow frames made no pipeline. Run I (794 s) made 67 new pipelines and waited 2.1 s in total. One
pipeline costs about 30 ms on average.

Every run has a stop of about 1.2 s between 11 s and 15 s after the start (1189, 1235, 1246, 1260 ms and others).
These frames made no pipeline and no shader. The cause is not known.

## Run K: async_pipelines on, with an empty cache

The user deleted the shader cache and set `async_pipelines=1`, then played (375 s, 0 JIT misses, no crash, 30 fps).
The user reports clearly less stutter.

- The cache was empty at the start: 0 pipelines of earlier runs. The preparation before the start took 3.8 s
  (1379 shaders translated from the files of the game).
- The run made 336 pipelines (3.4 s of making) and waited 0 ms for them. Run I had waited 2.1 s for 67 pipelines with
  a full cache.
- The runtime skipped 1001 draws while pipelines were made. That is a very small part of all draws.
- 42 frames took 50 ms or more, at the same rate as before (21 in 252 s in run J). These have other causes. A stop of
  1.35 s at 18 s is the one at the start that every run has.

## Reading

- The stutter from pipelines exists, but it is rare and short: a burst when the game meets many new states at once.
  It gets less as the cache fills.
- Most slow frames have another cause. Some are in groups (for example 176.0 s to 177.7 s in run J), which may be
  loading of data. The cause is not known.
- Making pipelines ahead of time does not remove the burst, because the new states are not known. The remaining
  options are the setting `async_pipelines` and more play to fill the cache.

## Options

- `async_pipelines=1`: no wait for a new pipeline. The cost is that what the new shader draws appears a few frames
  late. Run K shows that the wait for pipelines went to 0 ms, and the user sees clearly less stutter. Use it as the
  default of the launcher.
- Keep the shader cache. A deleted cache makes the first start slower (3.8 s) and the pipelines are made again. With
  `async_pipelines=1` this costs little, but it brings no gain either.
- The launcher can show these settings.

## Run L: profiler and GPU profile (90 s, update program, async_pipelines on)

The user loaded the save and walked around. The user saw no stutter.

- CPU profile (80 s, 35 threads, 1.27 million samples): 86.6% of the samples are in `ZwWaitForAlertByThreadId` and
  8.7% in `ZwWaitForSingleObject`. The threads wait. No game function has more than 0.13% of the samples, and no
  single place on the CPU is hot.
- GPU: 1.0 to 1.4 ms a frame in passes at the start, then 5.6 to 6.6 ms. The last pass ends 16 to 22 ms after the
  first. The GPU is far from the 33 ms of a frame at 30 fps.
- Frame rate: 30 fps in the windows of 5 s, with two exceptions: 25.8 fps at 6.9 s and 22.3 fps at 17 s.
- Slow frames: 11 frames over 50 ms in the first 24 s, the slowest 1340 ms (the stop at the start). Later windows
  have 1, 4 and 2 frames over 50 ms, the slowest 66 ms.
- Pipelines: 5 pipelines made in 90 s, 11 ms in total, 0 ms waited for. Texture uploads: 239 MB in the first 24 s,
  then about 15 to 90 MB for each 20 s.
- JIT: 0 misses.

Reading: the stop at the start is real (1340 ms, around 17 s there is a drop to 22 fps) and it comes with the
uploads of about 240 MB, but the profile shows no busy CPU thread. The stop looks like a wait on a lock or on file
reads, not like work. This is not verified. After the start the game runs smoothly.

## Run M: RomFS extracted (--extract-container), early game, 274 s

The user ran `scarletnx.exe --extract-container <base> --update <update>`. This made a second game folder in
`%APPDATA%\scarletnx\recompiled\01001F5010DFA000` (own recompile, own hints, own cache, no save, 12.3 GB of RomFS
copies). The run began in the opening scene.

- The files came from the extraction: 19095 files in the RomFS, 6 files mapped.
- A stop of 1318 ms was still there in the first 29 s (6 frames over 50 ms). So reading the dump in place is not the
  cause of the stop at the start. Not a full proof: the run is not equal to the earlier ones.
- Differences that spoil the comparison: a cold shader cache (33 shaders translated and 59 pipelines made in 29 s),
  183 JIT misses (this folder has no jump-table hints), no save.
- The run read in place at 12:31 (opening scene, 37 s) had no stop: the slowest frame was 85 ms. So the stop does not
  happen in every run.
- The freeze of 4804 ms at 47 s is the software keyboard (name entry), not a stutter.
- Later windows: 30 fps, with slow frames of 200 ms or less. 9 frames over 50 ms at 154 s while 524 textures were
  uploaded (194 MB).

## The stop at the start: what the logs say and the next measurement

What is known (the logs of 2026-10-31 and the runs before):

- One stop of 1.2 s to 1.4 s in the first 20 s of most runs: 1311 ms at 13.6 s, 1248 ms at 14.8 s (the run of 681 s),
  1340 ms in run L, 1318 ms in run M. The run read at 12:31 had none. So it is not in every run.
- The frontend thread does not stop: the `perf:` lines come every 5.0 s through the stop. Only the frame count stands
  still. 0 pipelines made, 0 shaders translated, 0 render targets made, 0 texture uploads and 0 ms in finishes in the
  frame after the stop. So the stop is not in the GPU, the shader work or the uploads.
- It comes after the first title scene is shown and before the big load: 283 texture uploads (112 MB) follow at about 19 s.
- The CPU profile of run L (80 s) shows 95% waits and no busy thread, so the game's threads wait for something. A wait
  with a time limit of 1 s and the frames around it would give such a number. This is a guess.
- The stop is not the reading of the dump in place: run M read extracted files and had it. That comparison had other
  differences (cold cache, 183 JIT misses, no save).

Next measurement: the patch `recomp/patches/0004-hitch-dump.patch` adds the setting `hitch_dump` (set to 400 in the
game's `.ini`). When the frame count stands still for 400 ms and again at 800 ms, the runtime writes `hitch-1.txt` and
`hitch-2.txt` (and more for later stops) in the game's folder. Each has the state of every game thread, what it waits
for (a service, an event, a mutex, a sleep with its time) and its call chain. The same call chain in both files names
the code that waits, and the wait object names what it waits for. The user plays the first minute of a start and
quits. Then read the `hitch-*.txt` files and the `hitch:` lines of the log.

### First hitch run (patch 0004, `hitch_dump=400`, 2026-10-31 17:48)

Two stops were caught: one at 2.6 s (800 ms or more, `hitch-1.txt` and `hitch-2.txt`) and the known one at 14.3 s to
15.8 s (the frame after it took 1477 ms, `hitch-3.txt` at 400 ms and `hitch-4.txt` at 816 ms).

At 400 ms (`hitch-3.txt`):

- `FileManager` (a game thread, prio 31) is inside `nn::fs::ReadFile`, `RomFsFile::DoRead` and
  `StorageServiceObjectAdapter<IStorage>::Read`, sending the IPC request (`SendSyncRequest`) to the file system
  service. So a RomFS read through the storage service is under way.
- `MainThread` is in a system call (a sleep or an IPC request), called through `main+2762978`, `main+c85270`,
  `main+c18fb4` and `main+c19878`. The code around two of these callers has a loop of load-exclusive and
  store-exclusive instructions (an atomic counter or lock), as a wait loop has.
- All other 22 threads wait on condition variables (jobs, decode, render workers, Havok, Wwise audio). None is busy.

At 816 ms (`hitch-4.txt`):

- `FileManager` has finished the read and waits on its condition variable for the next job.
- `MainThread` is running on core 0 (`pc=8148c18`, `main+143c18`). It has left the system call, and the frame is still
  1477 ms away.

Reading: the game's main thread waits for a file read of its `FileManager` thread. The read takes some hundreds of ms.
Then the main thread runs for about 600 ms more before the next frame. Whether that run is work that depends on the
loaded data (decompress, set up) or more waiting is not known. This is the first evidence that the stop is a file read,
which the earlier comparison with extracted files (run M) could not show.

What is not known yet: which file and how big it is, and how long the read takes. The IPC read is a read of the RomFS
image at an absolute offset, so the log has no file name for it. Patch `0005-slow-romfs-read.patch` logs each such read
that takes 50 ms or more, with its size and its offset, while `hitch_dump` is on. The offset gives the file, and the
time and size give the rate of the read in place (decrypt and read of the dump). The next run shows if the stop is the
read itself (a slow read of the size of the stop) or a short read followed by work of the main thread.

### Second hitch run (patches 0004 and 0005, 2026-10-31 17:54)

The stop came at 15.6 s to 16.9 s (the frame after it took 1297 ms). `hitch-2.txt` (416 ms) and `hitch-3.txt` (816 ms):

- No RomFS read of 50 ms or more was logged during the stop. The one slow read of the run (66 MB at offset 3c59c670, 51 ms,
  about 1.3 GB/s) came at 17.5 s, after the stop. So the read in place of the dump is fast, and the stop is **not** a slow
  file read. The first run's `FileManager` in a read at 400 ms was a read that happened to run then.
- All threads wait on condition variables, `FileManager` and the three `FsDecodeThread`s too. None is busy.
- `MainThread` is in the same system call at 416 ms and at 816 ms, with the same stack pointer and the same callers
  (`main+276a3e4`, `main+c85270`, `main+c18fb4`, `main+c19878`, `main+c89ec0`, `main+c8f210`, then the frame loop). The
  system call is `svc #0xb`, `svcSleepThread` (the word before the return address `sdk+39fa20` is `svc #0xb`).
- The code (disassembled with `tools/disasm.py`): the function at `main+2762...` (guest `a7678f0`) is called by the
  function at `a76eb20` after it sets a flag. It goes through a table of 39 entries of 288 bytes. For each entry whose
  state (offset 0x100) is 1 or 2 and whose object pointer (offset 0x108) is not null, it calls a virtual function of the
  object (vtable offset 0x40). While that returns something other than 1, it calls `SleepThread` with 16,000,000 ns (16 ms)
  and asks again. When it returns 1, the entry gets state 3. So the main thread **polls with 16 ms sleeps until each
  active entry reports done**.
- What the entries are is not known (names are stripped). A pool of 39 slots of 288 bytes, polled by the main thread
  before it goes on, looks like a table of asynchronous requests (loads, decodes or fences).
- Reading: the main thread waits for an entry that is not done, and nothing else in the guest runs, so what finishes the
  entry is not a guest thread. It may be the host side (the GPU thread, a service, a timer), or a completion that
  is missed and ends by a time limit in the virtual function. The first run's snapshot fits too: its read had just
  ended, and the main thread was out of the loop at 816 ms.

Patch `0006-stop-dump-sleep-and-gpu.patch` adds what is missing in the next dump:

- The registers of the main thread's last `svcSleepThread` and what they point to (`dump_sleeps`, a function that was
  there already but needed 100 sleeps; a stop makes 25). The registers hold the entry (x25) and the object. The pointees
  give the object's type through its virtual function table, and the entry's words.
- The GPU thread's state (`gpu_report_stall`) and the host threads that wait for a lock (`report_lock_waits`).

### Third hitch run (patches 0004 to 0006, 2026-10-31 18:20)

The stop came at 10.3 s to 11.6 s (the frame after it took 1263 ms). The four files are kept in
`game/work/hitch-run3/` (not in git). The facts:

- At 416 ms (`hitch-3.txt`) the main thread polls the entry **5** of the table of 39 (x21 = 5). Its state (offset 0x100) is 1,
  and its object pointer (offset 0x108) is 0x146e45000. 20 `svcSleepThread` calls of 16,000,000 ns (x20 = f42400) were made
  since the previous dump.
- At 816 ms (`hitch-4.txt`) it polls the entry **27** (x21 = 0x1b). 11 more sleeps were made. So the loop goes on
  and about 22 entries were passed in 400 ms: each entry takes about one sleep of 16 ms.
- The GPU thread is between jobs (`gpu: the GPU thread is between jobs`), so it holds nothing. The host threads
  wait on conditions (the guest threads' own).
- No RomFS read of 50 ms or more during the stop (one at 12.2 s, after it: 66 MB, 50 ms).

Reading: the stop is the sum of the sleeps of this loop: 39 entries, one sleep of 16 ms for each entry that is not done at
the first question (about 0.6 s for 39 entries at one sleep, more when an entry needs two). On the console the entries
are done at the first question, and the loop does not sleep. So the question is why each entry is not done when the
main thread asks, and is done one sleep later, while all other threads idle.

Not known: the class of the objects and what their function at vtable +0x40 reads. Patch
`0007-stop-dump-object-chain.patch` prints, for the callee-saved registers of the sleeping thread, the pointer, the object
and its virtual function table with the names of its first twelve functions. The vtable's +0x40 is the slot 8.
Next run: read the chain of x25 (the pointer of the object, at +0x108 of the entry), disassemble the function in the
slot 8 with `tools/disasm.py`, and see what it reads.

### Fourth hitch run (patches 0004 to 0007, 2026-10-31 18:25)

The stop came at 12.0 s to 13.4 s (the frame after it took 1401 ms). The four files are in `game/work/hitch-run4/`
(not in git).

- At 416 ms the main thread is at the entry **3** of the table of 39, at 800 ms at entry **10**: 7 entries in 384 ms with
  10 `svcSleepThread` calls in that time. **One sleep took about 38 ms**, and the game asks for 16,000,000 ns (16 ms).
  The interval includes the time to write the first dump, so the figure is a rough one, but it is far from 16 ms.
- The object of the entry (x25, 0x14a4ec000) has the virtual function table at 0xc1062c8. The function in its slot 8 (offset 0x40,
  guest `8c3d488`, the one that the loop calls) is a state machine. State 0: set state 1 and call the virtual function at
  +0xa0 (the start of the work). State 1: look at the object's children (`[this+0xd8]`, and the array behind `[this+0xf8]`
  with a 0x18 stride): each child has a flag byte at +0xd8 (an acquire load), and the entry is done when all are set.
  Then it goes on to state 2 and finishes. So **an entry is started by its first poll**, and the loop waits one sleep for
  the children to set their flags.
- That is how the game is written: the 39 entries are started and finished one after the other, one sleep each at the
  least. On the console the sleep is 16 ms, so the loop takes about 39 x 16 = 0.6 s at the least, if each entry needs one
  sleep. Whether the console shows this stop too is not known. But here the stop is 1.2 s to 1.4 s, about twice
  that, which fits sleeps that last twice as long as asked.
- A test of this machine (a program of its own, the same call as the runtime's sleep, `SleepConditionVariableSRW` with
  16 ms): with the default 15.6 ms tick the wait takes 26.6 ms on average (up to 31.6 ms), and after
  `timeBeginPeriod(1)` it takes 16.4 ms (15.9 to 17.1). The runtime asks for 1 ms at its first timed wait
  (`plat_win32.c`, `timer_period_1ms`), but Windows 11 may not honor that for a process in some states (a window that is not
  in front). Whether it does in the game's process is what the next run measures.

Patch `0008-sleep-time-in-dump.patch` makes the dump of a sleeping thread say how much `svcSleepThread` time it asked for
and how long the sleeps took, since the last dump and for one call. If one call takes about 38 ms or more for 16 ms, the
runtime's sleep is too long and the cause is in the timer (the 1 ms request is not honored, or the thread waits for its
core after the sleep). If one call takes about 16 ms, the game's own loop is the stop.

### Fifth hitch run (patches 0004 to 0008, 2026-10-31 18:31) and the result

The stop came at 12.3 s to 13.6 s (the frame after it took 1335 ms). The numbers below were read from the dumps before
the files were deleted.

- The runtime's sleep is exact. In the 400 ms between the dumps at 416 ms and 816 ms the main thread made 13 sleeps that asked
  for 208 ms and took 215 ms (16.5 ms for a call). In the 416 ms before, 18 sleeps asked for 272 ms and took 282 ms (15.7 ms
  for a call). So the estimate of 38 ms for a sleep in the fourth run was wrong: the interval between dumps held the
  time of the dump itself and the work of the main thread. The default timer tick is not in the way (the runtime's
  request for 1 ms is honored in the game's process).
- The other 185 ms of the 400 ms were not sleep: the main thread was running. It starts each entry on its first poll
  (the virtual function at +0xa0), and 22 entries took 13 sleeps and about 8 ms of work each.
- The sum fits the stop: 39 entries, one sleep of 16 ms for about 60% of them, and about 8 ms of work for each.

Result: **the stop at the start is the game's own loop**, a step of its loading in which the main thread starts the 39 pooled
objects one after the other and waits for the workers' flags with sleeps of 16 ms. The runtime's sleeps are accurate, no
other thread is busy, no file read is slow, and the GPU thread is idle. The console runs the same code with the same sleeps,
and its CPU is slower than this machine's for the 8 ms of work of each entry, so the console is likely to stop for at least as long
at this point. This is a reading of the code. A run on the console is not available here.

What the runtime cannot change without breaking the game's timing: the sleeps (the game asks for them) and the order of the
starts. No fix is planned. The diagnostic patches `0004` to `0008` stay in `recomp/patches/` as optional tools (not applied
to the runtime), to find a wait of another stop. The setting `hitch_dump` is gone from the `.ini` of this game.
