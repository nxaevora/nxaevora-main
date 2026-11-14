/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_measure_calc.h"

/* The sorted index of a percentile: ceil(percent * count / 100) - 1. */
static uint32_t rank_index(uint32_t percent, uint32_t count) {
    return (uint32_t)(((uint64_t)percent * count + 99u) / 100u) - 1u;
}

static void sort_ascending(uint32_t* values, uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        uint32_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = value;
    }
}

void nvn_measure_frame_stats(const uint32_t* frame_us, uint32_t count, NvnMeasureFrameStats* out) {
    uint32_t sorted[NVN_MEASURE_WINDOW];
    if (!out) return;
    if (count > NVN_MEASURE_WINDOW) count = NVN_MEASURE_WINDOW;
    if (count == 0) {
        out->median_us = 0;
        out->p99_us = 0;
        out->max_us = 0;
        return;
    }
    for (uint32_t i = 0; i < count; i++) sorted[i] = frame_us[i];
    sort_ascending(sorted, count);
    out->median_us = sorted[rank_index(50, count)];
    out->p99_us = sorted[rank_index(99, count)];
    out->max_us = sorted[count - 1];
}

double nvn_measure_ms(uint64_t ns, uint64_t presents) {
    if (presents == 0) return 0.0;
    return (double)ns / (double)presents / 1000000.0;
}
