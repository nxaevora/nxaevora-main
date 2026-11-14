/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_measure.h"
#include "nx/gpu/nvn_measure_calc.h"
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/platform/log.h"
#include "snx_settings.h"
#include <stdio.h>

static uint64_t old_guest_ns, old_gpu_ns, native_guest_ns, native_gpu_ns;

/* The present window. nvn_measure_present runs under vk_lock, so only one thread uses these. */
static uint32_t frame_us[NVN_MEASURE_WINDOW];
static uint32_t frame_count;
static uint64_t last_present_ns;
static int have_last_present;
static uint64_t window_drawn, window_skipped;

void nvn_measure_old_guest(uint64_t ns) { __atomic_fetch_add(&old_guest_ns, ns, __ATOMIC_RELAXED); }
void nvn_measure_old_gpu(uint64_t ns) { __atomic_fetch_add(&old_gpu_ns, ns, __ATOMIC_RELAXED); }
void nvn_measure_native_guest(uint64_t ns) { __atomic_fetch_add(&native_guest_ns, ns, __ATOMIC_RELAXED); }
void nvn_measure_native_gpu(uint64_t ns) { __atomic_fetch_add(&native_gpu_ns, ns, __ATOMIC_RELAXED); }

/* Takes a counter's window and starts its next window. */
static uint64_t counter_take(uint64_t* counter) { return __atomic_exchange_n(counter, 0, __ATOMIC_RELAXED); }

/* The first present has no previous frame time: its window starts now, with the counters at 0. */
static void counters_clear(void) {
    counter_take(&old_guest_ns);
    counter_take(&old_gpu_ns);
    counter_take(&native_guest_ns);
    counter_take(&native_gpu_ns);
}

static void window_restart(uint64_t now_ns) {
    last_present_ns = now_ns;
    have_last_present = 1;
    frame_count = 0;
    window_drawn = g_vk_drawn;
    window_skipped = g_vk_skipped;
}

/* One line for a full window: the mean CPU time of each side per frame, the frame time spread and the draws. The
   counters of the other path are read and cleared too, so a value there shows a fault. */
static void window_log(uint64_t guest_ns, uint64_t gpu_ns, uint64_t other_ns) {
    NvnMeasureFrameStats stats;
    char other[32] = "";
    nvn_measure_frame_stats(frame_us, frame_count, &stats);
    if (other_ns) snprintf(other, sizeof other, " other_ns=%llu", (unsigned long long)other_ns);
    LOG("nvn-measure: path=%s presents=%u guest_ms=%.3f gpu_ms=%.3f frame_ms median=%.3f p99=%.3f max=%.3f "
        "draws=%llu skipped=%llu%s\n",
        g_settings.nvn_native ? "native" : "old", frame_count, nvn_measure_ms(guest_ns, frame_count),
        nvn_measure_ms(gpu_ns, frame_count), (double)stats.median_us / 1000.0, (double)stats.p99_us / 1000.0,
        (double)stats.max_us / 1000.0, (unsigned long long)(g_vk_drawn - window_drawn),
        (unsigned long long)(g_vk_skipped - window_skipped), other);
}

void nvn_measure_present(uint64_t now_ns) {
    uint64_t guest_ns, gpu_ns, other_ns;
    if (!g_settings.nvn_measure) return;
    if (!have_last_present) {
        window_restart(now_ns);
        counters_clear();
        return;
    }
    {
        uint64_t delta_us = (now_ns - last_present_ns) / 1000u;
        frame_us[frame_count++] = delta_us > UINT32_MAX ? UINT32_MAX : (uint32_t)delta_us;
    }
    last_present_ns = now_ns;
    if (frame_count < NVN_MEASURE_WINDOW) return;
    if (g_settings.nvn_native) {
        guest_ns = counter_take(&native_guest_ns);
        gpu_ns = counter_take(&native_gpu_ns);
        other_ns = counter_take(&old_guest_ns) + counter_take(&old_gpu_ns);
    } else {
        guest_ns = counter_take(&old_guest_ns);
        gpu_ns = counter_take(&old_gpu_ns);
        other_ns = counter_take(&native_guest_ns) + counter_take(&native_gpu_ns);
    }
    window_log(guest_ns, gpu_ns, other_ns);
    window_restart(now_ns);
}
