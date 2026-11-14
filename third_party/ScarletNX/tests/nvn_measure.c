/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the nvn measure (gpu/nvn_measure_calc.c): the nearest-rank median and 99th percentile, the
   maximum, the unchanged input, and the mean milliseconds. */
#include "nx/gpu/nvn_measure_calc.h"
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_empty(void) {
    NvnMeasureFrameStats stats = {1, 2, 3};
    nvn_measure_frame_stats(NULL, 0, &stats);
    CHECK(stats.median_us == 0);
    CHECK(stats.p99_us == 0);
    CHECK(stats.max_us == 0);
}

static void test_one(void) {
    const uint32_t frame_us[] = {42};
    NvnMeasureFrameStats stats;
    nvn_measure_frame_stats(frame_us, 1, &stats);
    CHECK(stats.median_us == 42);
    CHECK(stats.p99_us == 42);
    CHECK(stats.max_us == 42);
}

/* 600 values in descending order (600 down to 1). Sorted, the value at index i is i + 1. The median's index is
   ceil(50 * 600 / 100) - 1 = 299, so 300 us. The 99th percentile's index is ceil(99 * 600 / 100) - 1 = 593, so
   594 us. The maximum is 600 us. */
static void test_window(void) {
    uint32_t frame_us[NVN_MEASURE_WINDOW];
    NvnMeasureFrameStats stats;
    for (uint32_t i = 0; i < NVN_MEASURE_WINDOW; i++) frame_us[i] = NVN_MEASURE_WINDOW - i;
    nvn_measure_frame_stats(frame_us, NVN_MEASURE_WINDOW, &stats);
    CHECK(stats.median_us == 300);
    CHECK(stats.p99_us == 594);
    CHECK(stats.max_us == 600);
}

/* Sorted, 1 2 3 4 5: the median's index is ceil(50 * 5 / 100) - 1 = 2, so 3 us, and the 99th percentile's index is
   ceil(99 * 5 / 100) - 1 = 4, so 5 us. */
static void test_unsorted_input(void) {
    uint32_t frame_us[] = {5, 1, 4, 2, 3};
    const uint32_t before[] = {5, 1, 4, 2, 3};
    NvnMeasureFrameStats stats;
    nvn_measure_frame_stats(frame_us, 5, &stats);
    CHECK(stats.median_us == 3);
    CHECK(stats.p99_us == 5);
    CHECK(stats.max_us == 5);
    for (uint32_t i = 0; i < 5; i++) CHECK(frame_us[i] == before[i]);
}

static void test_ms(void) {
    CHECK(nvn_measure_ms(1000000000u, 0) == 0.0);
    CHECK(nvn_measure_ms(0, 1) == 0.0);
    CHECK(nvn_measure_ms(1000000000u, 1) == 1000.0);
    CHECK(nvn_measure_ms(3000000000u, 2) == 1500.0);
}

int main(void) {
    test_empty();
    test_one();
    test_window();
    test_unsorted_input();
    test_ms();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_measure: ok\n");
    return 0;
}
