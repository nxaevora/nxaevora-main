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
