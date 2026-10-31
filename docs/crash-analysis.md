<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Crash analysis: 2026-10-31 00:50:22

Status: the cause is not found. This note records what is verified and what is not.

## Runs and crashes

The folder `game/recompiled/01001F5010DFA000/logs/` holds four logs from 00:50 to 01:15. One of them crashed.

| Log | Length | End |
| --- | --- | --- |
| 2026-10-31_00-50-22 | 1.42 s | crash, invalid memory access |
| 2026-10-31_00-56-18 | 346 s | normal exit |
| 2026-10-31_01-09-34 | 794 s | normal exit |
| 2026-10-31_01-15-50 | 253 s | normal exit |

The newest run (log file in the game folder, 375 s) ended normally too. So 1 run of 5 crashed. `scarletnx-error.log`
has only the one entry. The crash happened at the start, while the game loaded data (many `IStorage` reads).

## Verified facts

- Host fault: access violation at guest address 0x18e9e6a28, in the recompiled code of function a97a858.
- The logged pc a97a934 is the return site of `bl a949e0c`. The runtime updates the pc only at calls. So the faulting
  instruction is after this point. The only access that can use the address is the reference-count release at
  a97a944 (`ldaxr w10, [x9]` with `x9 = x8 + 8`). So `x8 = [sp + 8]` was 0x18e9e6a20.
- The pointer is 32-byte aligned. a949e0c is a lookup in a table of handles (32-byte entries in 128-entry chunks)
  under a recursive mutex (`x19 + 0x54`, owner at `x19 + 0x60`). It returns the entry and adds 1 to its reference
  count. The helper 8840cac moves such a handle pair into the local at `sp + 8`.
- So a97a944 releases an entry pointer that the lookup returned. The pointer lies about 1186 MiB above the buffer
  of the failing read (0x1474f3000), and the log says 1081 MiB of physical memory were mapped. The address may be
  outside the mapped range. Not verified, because the log has no map of the regions.

## Not verified

- Whether 0x18e9e6a20 is mapped at the fault. The crash report does not print the memory blocks.
- A race in the handling of `ldaxr`/`stlxr` (the function uses them for reference counts). It is only a lead. The
  lookup runs under a mutex, and the mutex itself is taken with an exclusive pair (`ldaxr`, `stxr` at a949e60).
- A stale chunk pointer from a table that another thread grows at the same time.
- `alias_pool=1` is in the ini, but the log of every run says "views are copies (no alias pool)". The Windows host
  does not use the pool. This changes how views of memory behave, and no link to the crash is shown.

## What would give the answer

1. Add to the crash report: the memory block that holds the fault address (or "free"), and 16 words of the stack
   at `sp`. That is a change in `third_party/ScarletNX`. Tell the user first. A patch in `recomp/patches/` is better.
2. Run the same start many times (30 or more starts, quit after 5 s). The crash report then shows if the address
   is always unmapped.
3. A fix needs many runs. One good run does not prove it.
