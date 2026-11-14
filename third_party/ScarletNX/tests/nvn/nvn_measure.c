/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the nvn measure (gpu/nvn/diag/nvn_measure_calc.c): the nearest-rank median and 99th percentile, the
   maximum, the unchanged input, the mean milliseconds and the indexes of the largest keys. */
#include "nx/gpu/nvn/diag/nvn_measure_calc.h"
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

/* count 0, k 0, and keys that are all 0: no index is written. */
static void test_top_empty(void) {
    const uint64_t keys[] = {5, 6};
    const uint64_t zeros[] = {0, 0, 0};
    uint32_t out[4];
    CHECK(nvn_measure_top(keys, 0, out, 4) == 0);
    CHECK(nvn_measure_top(keys, 2, out, 0) == 0);
    CHECK(nvn_measure_top(zeros, 3, out, 3) == 0);
}

/* k is larger than the count of keys above 0: only those keys are written. */
static void test_top_fewer_than_k(void) {
    const uint64_t keys[] = {0, 7, 0, 4};
    uint32_t out[5];
    CHECK(nvn_measure_top(keys, 4, out, 5) == 2);
    CHECK(out[0] == 1);
    CHECK(out[1] == 3);
}

/* Equal keys keep index order. */
static void test_top_equal_keys(void) {
    const uint64_t keys[] = {7, 7, 7, 7};
    uint32_t out[4];
    CHECK(nvn_measure_top(keys, 4, out, 3) == 3);
    CHECK(out[0] == 0);
    CHECK(out[1] == 1);
    CHECK(out[2] == 2);
}

/* The largest key is the last entry. */
static void test_top_last_is_largest(void) {
    const uint64_t keys[] = {1, 2, 3, 9};
    uint32_t out[4];
    CHECK(nvn_measure_top(keys, 4, out, 4) == 4);
    CHECK(out[0] == 3);
    CHECK(out[1] == 2);
    CHECK(out[2] == 1);
    CHECK(out[3] == 0);
}

/* A mixed case, and keys stays unchanged. */
static void test_top_does_not_change_keys(void) {
    const uint64_t keys[] = {3, 9, 9, 5, 1};
    const uint64_t before[] = {3, 9, 9, 5, 1};
    uint32_t out[3];
    CHECK(nvn_measure_top(keys, 5, out, 3) == 3);
    CHECK(out[0] == 1);
    CHECK(out[1] == 2);
    CHECK(out[2] == 3);
    for (uint32_t i = 0; i < 5; i++) CHECK(keys[i] == before[i]);
}

/* The holder codes of the nvn measure: a slot count of 4 gives the handler codes 1..4, the GPU code 5 and the other
   code 6. */
static void test_holder_unknown(void) {
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(0, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_UNKNOWN);
    CHECK(holder.slot == 0);
}

static void test_holder_first_slot(void) {
    CHECK(nvn_measure_holder_code(1, 0, 4) == 1);
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(1, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_HANDLER);
    CHECK(holder.slot == 0);
}

static void test_holder_last_slot(void) {
    CHECK(nvn_measure_holder_code(4, 0, 4) == 4);
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(4, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_HANDLER);
    CHECK(holder.slot == 3);
}

static void test_holder_gpu(void) {
    CHECK(nvn_measure_holder_code(0, 1, 4) == 5);
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(5, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_GPU);
    CHECK(holder.slot == 0);
}

static void test_holder_other(void) {
    CHECK(nvn_measure_holder_code(0, 0, 4) == 6);
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(6, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_OTHER);
    CHECK(holder.slot == 0);
}

static void test_holder_too_large(void) {
    const NvnMeasureHolder holder = nvn_measure_holder_from_code(7, 4);
    CHECK(holder.holder_class == NVN_MEASURE_HOLDER_UNKNOWN);
    CHECK(holder.slot == 0);
}

int main(void) {
    test_empty();
    test_one();
    test_window();
    test_unsorted_input();
    test_ms();
    test_top_empty();
    test_top_fewer_than_k();
    test_top_equal_keys();
    test_top_last_is_largest();
    test_top_does_not_change_keys();
    test_holder_unknown();
    test_holder_first_slot();
    test_holder_last_slot();
    test_holder_gpu();
    test_holder_other();
    test_holder_too_large();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_measure: ok\n");
    return 0;
}
