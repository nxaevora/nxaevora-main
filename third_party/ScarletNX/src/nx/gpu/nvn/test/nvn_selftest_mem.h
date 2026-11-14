/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_MEM_H
#define SNX_NVN_SELFTEST_MEM_H

/* The isolated memory of the NVN selftest (nvn-unobserved.md, "The selftest", isolation). The selftest has its own
   area, not the heap of the game. This part is the allocator over one range of addresses. It does not map memory, so
   host tests can run it: nvn_selftest.c gives it a guest range from kmem_alloc, a test gives it any range. */

#include <stdint.h>

typedef struct {
    uint64_t base, size, used;
    uint64_t high_water; /* the largest used since the init, for the report */
} NvnStArena;

/* base and size are not 0. The arena does not own the range. */
void nvn_st_arena_init(NvnStArena* a, uint64_t base, uint64_t size);
/* The next block of size bytes with the alignment (a power of two, 0 means 1). Returns the address, or 0 when the arena
   is full or the arguments are wrong. A block is zero only if the range was zero when it was given: the caller clears. */
uint64_t nvn_st_arena_alloc(NvnStArena* a, uint64_t size, uint64_t align);
/* All blocks are free again. The caller clears the memory it needs zero. */
void nvn_st_arena_reset(NvnStArena* a);
/* 1 when [addr, addr + size) lies in the used part of the arena (a check that a call wrote only inside the selftest) */
int nvn_st_arena_contains(const NvnStArena* a, uint64_t addr, uint64_t size);

#endif
