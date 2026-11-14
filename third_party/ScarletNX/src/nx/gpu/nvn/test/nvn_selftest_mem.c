/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/test/nvn_selftest_mem.h"
#include <string.h>

void nvn_st_arena_init(NvnStArena* a, uint64_t base, uint64_t size) {
    memset(a, 0, sizeof *a);
    a->base = base;
    a->size = size;
}

uint64_t nvn_st_arena_alloc(NvnStArena* a, uint64_t size, uint64_t align) {
    if (!a->base || !size) return 0;
    if (!align) align = 1;
    if (align & (align - 1)) return 0;
    uint64_t start = a->base + a->used;
    uint64_t pad = (align - (start & (align - 1))) & (align - 1);
    if (pad > a->size - a->used || size > a->size - a->used - pad) return 0;
    a->used += pad + size;
    if (a->used > a->high_water) a->high_water = a->used;
    return start + pad;
}

void nvn_st_arena_reset(NvnStArena* a) { a->used = 0; }

int nvn_st_arena_contains(const NvnStArena* a, uint64_t addr, uint64_t size) {
    if (addr < a->base || addr - a->base > a->used) return 0;
    return size <= a->used - (addr - a->base);
}
