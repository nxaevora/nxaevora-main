/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_EXECTEST_H
#define SNX_NVN_EXECTEST_H

/* nvn_exectest=1: the execution test of the NVN host path (nvn-unobserved.md, "Design of 4-u3-4"). It runs real work
   through the host handlers on its own queues and checks the result in memory. It needs nvn_trace=1 and nvn_native=1.
   The pure parts are here, so a host test can run them: the clear bytes, the comparison and its text, the pass rules
   of E1, E4, E7 and E8, the stall result, the cleanup drain rule, and the mutation name. */

#include "common/cpu/aarch64/cpu.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The results of nvnSyncWait (nvn-design.md, "Queue fences and flush jobs (4-u3-4)", decision 5). */
#define NVN_EX_SYNC_SIGNALED 0u
#define NVN_EX_SYNC_SATISFIED 1u
#define NVN_EX_SYNC_TIMEOUT 2u
#define NVN_EX_SYNC_FAILED 3u

/* The cases of the design, in the order of the log. The names are the values of nvn_selftest_mutate that change one
   host argument. */
typedef enum {
    NVN_EX_CASE_FENCE_ORDER = 0, /* E1 */
    NVN_EX_CASE_GPU_TO_CPU,      /* E2 */
    NVN_EX_CASE_CPU_TO_GPU,      /* E3 */
    NVN_EX_CASE_TIMEOUT,         /* E4 */
    NVN_EX_CASE_EVENT_REPORT,    /* E5 */
    NVN_EX_CASE_TWO_QUEUES,      /* E6 */
    NVN_EX_CASE_CB_FENCE_FLUSH,  /* E7 */
    NVN_EX_CASE_QUEUE_REINIT,    /* E8 */
    NVN_EX_CASE_COUNT
} NvnExCase;

static inline const char* nvn_ex_case_name(unsigned kase) {
    static const char* const names[NVN_EX_CASE_COUNT] = {
        "exec_fence_order", "exec_gpu_to_cpu", "exec_cpu_to_gpu", "exec_timeout",
        "exec_event_report", "exec_two_queues", "exec_cb_fence_flush", "exec_queue_reinit",
    };
    return kase < NVN_EX_CASE_COUNT ? names[kase] : "?";
}

/* The case of a mutation name, or NVN_EX_CASE_COUNT for an empty or unknown name. */
static inline unsigned nvn_ex_case_from_name(const char* name) {
    if (!name) return NVN_EX_CASE_COUNT;
    for (unsigned i = 0; i < NVN_EX_CASE_COUNT; i++)
        if (!strcmp(name, nvn_ex_case_name(i))) return i;
    return NVN_EX_CASE_COUNT;
}

/* The RGBA8 unorm bytes of a clear color. A value at or above 1 is 255, at or below 0 is 0, else rounded. */
static inline void nvn_ex_rgba8_bytes(const float color[4], uint8_t out[4]) {
    for (unsigned i = 0; i < 4; i++) {
        const float v = color[i];
        out[i] = !(v > 0.0f) ? 0 : v >= 1.0f ? 0xFF : (uint8_t)(v * 255.0f + 0.5f);
    }
}

/* 1 when the ranges are equal, else the first difference goes into text. */
static inline int nvn_ex_compare_bytes(const uint8_t* expected, const uint8_t* actual, uint32_t size, char* text,
                                       size_t cap) {
    for (uint32_t i = 0; i < size; i++)
        if (expected[i] != actual[i]) {
            if (text && cap)
                snprintf(text, cap, "byte %" PRIu32 ": expected %02x, actual %02x", i, (unsigned)expected[i],
                         (unsigned)actual[i]);
            return 0;
        }
    if (text && cap) text[0] = '\0';
    return 1;
}

/* 1 when the values are equal, else text says both. */
static inline int nvn_ex_compare_u64(const char* field, uint64_t expected, uint64_t actual, char* text, size_t cap) {
    if (expected == actual) {
        if (text && cap) text[0] = '\0';
        return 1;
    }
    if (text && cap)
        snprintf(text, cap, "%s: expected %" PRIu64 ", actual %" PRIu64, field, expected, actual);
    return 0;
}

/* E1: the second fence is signaled in the limit (0 or 1), and the first fence is already signaled (0 at once). */
static inline int nvn_ex_e1_ok(uint64_t second, uint64_t first) {
    return (second == NVN_EX_SYNC_SIGNALED || second == NVN_EX_SYNC_SATISFIED) && first == NVN_EX_SYNC_SIGNALED;
}

/* E4: the short wait runs out, the wait after the flush is reached (0 or 1: the job can
   end before the call), and the sync that was never fenced fails. */
static inline int nvn_ex_e4_ok(uint64_t short_wait, uint64_t after_flush, uint64_t never_fenced) {
    return short_wait == NVN_EX_SYNC_TIMEOUT &&
           (after_flush == NVN_EX_SYNC_SIGNALED || after_flush == NVN_EX_SYNC_SATISFIED) &&
           never_fenced == NVN_EX_SYNC_FAILED;
}

/* E7: the fence inside the list posted its frame, so the wait is reached (0 or 1). */
static inline int nvn_ex_e7_ok(uint64_t wait) {
    return wait == NVN_EX_SYNC_SIGNALED || wait == NVN_EX_SYNC_SATISFIED;
}

/* E8: the new QueueInitialize dropped the pending frame, so its fence fails (3). */
static inline int nvn_ex_e8_ok(uint64_t wait) { return wait == NVN_EX_SYNC_FAILED; }

/* 1 when a bounded wait that must signal ran out: the GPU job that it waits for is still running. */
static inline int nvn_ex_wait_stalled(uint64_t result) { return result == NVN_EX_SYNC_TIMEOUT; }

/* 1 when a drain wait proves that the queue's work ended. Any other result (a refused fence gives 3) proves nothing. */
static inline int nvn_ex_drain_done(uint64_t wait) {
    return wait == NVN_EX_SYNC_SIGNALED || wait == NVN_EX_SYNC_SATISFIED;
}

/* 1 when the cleanup drain can run on this queue: the queue exists and the queue and sync handlers of a fence and a
   wait are present. A queue that cannot drain must keep its objects. */
static inline int nvn_ex_drain_ok(const char* missing_qops, const char* missing_sync, uint64_t queue) {
    return queue != 0 && !missing_qops && !missing_sync;
}

/* Runs the execution test once, after the game's first nvnQueueInitialize (nvn_trace.c). The queue is the one of the
   game, read only: the test makes its own queues. The CPU context is restored when it returns. */
void nvn_exectest_run(CPU* c, uint64_t game_queue);

#endif
