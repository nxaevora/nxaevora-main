/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arena of the NVN selftest (gpu/nvn/test/nvn_selftest_mem.c): alignment, the limit, the wrong arguments, the reset, and the
   check that an address lies in the used part. */
#include "nx/gpu/nvn/test/nvn_selftest_mem.h"
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_alloc(void) {
    NvnStArena a;
    nvn_st_arena_init(&a, 0x1004, 0x1000);
    CHECK(nvn_st_arena_alloc(&a, 16, 0) == 0x1004);
    CHECK(nvn_st_arena_alloc(&a, 16, 0x100) == 0x1100); /* aligned up */
    CHECK(nvn_st_arena_alloc(&a, 1, 8) == 0x1110);
    CHECK(nvn_st_arena_alloc(&a, 8, 3) == 0); /* not a power of two */
    CHECK(nvn_st_arena_alloc(&a, 0, 8) == 0);
}

static void test_limit(void) {
    NvnStArena a;
    nvn_st_arena_init(&a, 0x10000, 0x100);
    CHECK(nvn_st_arena_alloc(&a, 0x100, 1) == 0x10000);
    CHECK(nvn_st_arena_alloc(&a, 1, 1) == 0); /* full */
    nvn_st_arena_reset(&a);
    CHECK(nvn_st_arena_alloc(&a, 0x10, 1) == 0x10000);
    CHECK(nvn_st_arena_alloc(&a, 0x10, 0x100) == 0); /* the padding does not fit */
    CHECK(nvn_st_arena_alloc(&a, ~(uint64_t)0, 1) == 0); /* no overflow of the arithmetic */
    CHECK(a.high_water == 0x100);
}

static void test_empty_arena(void) {
    NvnStArena a;
    nvn_st_arena_init(&a, 0, 0x100);
    CHECK(nvn_st_arena_alloc(&a, 8, 8) == 0);
}

static void test_contains(void) {
    NvnStArena a;
    nvn_st_arena_init(&a, 0x2000, 0x1000);
    uint64_t p = nvn_st_arena_alloc(&a, 0x40, 0x10);
    CHECK(p == 0x2000);
    CHECK(nvn_st_arena_contains(&a, p, 0x40));
    CHECK(nvn_st_arena_contains(&a, p + 0x3f, 1));
    CHECK(!nvn_st_arena_contains(&a, p + 0x3f, 2));
    CHECK(!nvn_st_arena_contains(&a, p - 1, 1));
    CHECK(!nvn_st_arena_contains(&a, 0x2000 + 0x1000, 1));
    CHECK(!nvn_st_arena_contains(&a, ~(uint64_t)0, 1));
}

int main(void) {
    test_alloc();
    test_limit();
    test_empty_arena();
    test_contains();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_mem: ok\n");
    return 0;
}
