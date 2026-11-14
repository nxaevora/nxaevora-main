/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/diag/nvn_measure_calc.h"

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

/* 1 when key i may follow the last picked pair: a smaller key, or an equal key at a larger index. */
static int top_candidate(const uint64_t* keys, uint32_t i, int have_last, uint64_t last_key, uint32_t last_index) {
    if (!keys[i]) return 0;
    if (!have_last) return 1;
    return keys[i] < last_key || (keys[i] == last_key && i > last_index);
}

uint32_t nvn_measure_top(const uint64_t* keys, uint32_t count, uint32_t* out_index, uint32_t k) {
    uint32_t written = 0;
    uint64_t last_key = 0;
    uint32_t last_index = 0;
    int have_last = 0;
    if (!keys || !out_index) return 0;
    while (written < k) {
        uint64_t best_key = 0;
        uint32_t best_index = 0;
        int found = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (!top_candidate(keys, i, have_last, last_key, last_index)) continue;
            if (!found || keys[i] > best_key) {
                best_key = keys[i];
                best_index = i;
                found = 1;
            }
        }
        if (!found) break;
        out_index[written++] = best_index;
        last_key = best_key;
        last_index = best_index;
        have_last = 1;
    }
    return written;
}

uint32_t nvn_measure_holder_code(uint32_t slot1, int on_gpu_thread, uint32_t slots) {
    if (slot1 && slot1 <= slots) return slot1;
    return on_gpu_thread ? slots + 1u : slots + 2u;
}

NvnMeasureHolder nvn_measure_holder_from_code(uint32_t code, uint32_t slots) {
    NvnMeasureHolder holder = {NVN_MEASURE_HOLDER_UNKNOWN, 0};
    if (code == 0) return holder;
    if (code <= slots) {
        holder.holder_class = NVN_MEASURE_HOLDER_HANDLER;
        holder.slot = code - 1u;
        return holder;
    }
    if (code == slots + 1u) holder.holder_class = NVN_MEASURE_HOLDER_GPU;
    else if (code == slots + 2u) holder.holder_class = NVN_MEASURE_HOLDER_OTHER;
    return holder;
}
