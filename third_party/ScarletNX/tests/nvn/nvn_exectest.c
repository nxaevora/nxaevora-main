/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure parts of the NVN execution test (gpu/nvn/test/nvn_exectest.c): the RGBA8 clear bytes, the byte comparison and its
   text, the pass rules of E1, E4, E7 and E8, the stall result, the cleanup drain rule, and the mutation names. The
   expected values come from the design, not from the code under test. */
#include "nx/gpu/nvn/test/nvn_exectest.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_rgba8(void) {
    float magenta[4] = {1.0f, 0.0f, 1.0f, 0.0f};
    uint8_t bytes[4];
    nvn_ex_rgba8_bytes(magenta, bytes);
    CHECK(bytes[0] == 0xFF && bytes[1] == 0x00 && bytes[2] == 0xFF && bytes[3] == 0x00);
    float mixed[4] = {0.5f, 0.25f, -1.0f, 2.0f};
    nvn_ex_rgba8_bytes(mixed, bytes);
    CHECK(bytes[0] == 0x80); /* 127.5 rounds up */
    CHECK(bytes[1] == 0x40); /* 63.75 rounds up */
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == 0xFF);
}

static void test_compare(void) {
    const uint8_t expected[4] = {1, 2, 3, 4};
    const uint8_t actual[4] = {1, 2, 0, 4};
    char text[96];
    CHECK(!nvn_ex_compare_bytes(expected, actual, 4, text, sizeof text));
    CHECK(!strcmp(text, "byte 2: expected 03, actual 00"));
    CHECK(nvn_ex_compare_bytes(expected, expected, 4, text, sizeof text));
    CHECK(text[0] == '\0');
    CHECK(!nvn_ex_compare_u64("wait", 2, 1, text, sizeof text));
    CHECK(!strcmp(text, "wait: expected 2, actual 1"));
    CHECK(nvn_ex_compare_u64("wait", 3, 3, text, sizeof text));
}

static void test_decisions(void) {
    CHECK(nvn_ex_e1_ok(NVN_EX_SYNC_SIGNALED, NVN_EX_SYNC_SIGNALED));
    CHECK(nvn_ex_e1_ok(NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_SIGNALED));
    CHECK(!nvn_ex_e1_ok(NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_SIGNALED));
    CHECK(!nvn_ex_e1_ok(NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_TIMEOUT));
    CHECK(nvn_ex_e4_ok(NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_FAILED));
    CHECK(!nvn_ex_e4_ok(NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_FAILED));
    CHECK(nvn_ex_e4_ok(NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_SIGNALED, NVN_EX_SYNC_FAILED));
    CHECK(!nvn_ex_e4_ok(NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_FAILED));
    CHECK(!nvn_ex_e4_ok(NVN_EX_SYNC_TIMEOUT, NVN_EX_SYNC_SATISFIED, NVN_EX_SYNC_SIGNALED));
    CHECK(nvn_ex_e7_ok(NVN_EX_SYNC_SIGNALED));
    CHECK(nvn_ex_e7_ok(NVN_EX_SYNC_SATISFIED));
    CHECK(!nvn_ex_e7_ok(NVN_EX_SYNC_TIMEOUT));
    CHECK(!nvn_ex_e7_ok(NVN_EX_SYNC_FAILED));
    CHECK(nvn_ex_e8_ok(NVN_EX_SYNC_FAILED));
    CHECK(!nvn_ex_e8_ok(NVN_EX_SYNC_TIMEOUT));
    CHECK(!nvn_ex_e8_ok(NVN_EX_SYNC_SATISFIED));
    CHECK(!nvn_ex_e8_ok(NVN_EX_SYNC_SIGNALED));
    CHECK(nvn_ex_wait_stalled(NVN_EX_SYNC_TIMEOUT));
    CHECK(!nvn_ex_wait_stalled(NVN_EX_SYNC_SIGNALED));
    CHECK(!nvn_ex_wait_stalled(NVN_EX_SYNC_SATISFIED));
    CHECK(!nvn_ex_wait_stalled(NVN_EX_SYNC_FAILED));
}

static void test_drain(void) {
    CHECK(nvn_ex_drain_ok(NULL, NULL, 0x1000)); /* a queue with every handler */
    CHECK(!nvn_ex_drain_ok(NULL, NULL, 0));     /* no queue, so nothing to drain */
    CHECK(!nvn_ex_drain_ok("nvnQueueFenceSync", NULL, 0x1000));
    CHECK(!nvn_ex_drain_ok(NULL, "nvnSyncInitialize", 0x1000));
    CHECK(nvn_ex_drain_done(NVN_EX_SYNC_SIGNALED));
    CHECK(nvn_ex_drain_done(NVN_EX_SYNC_SATISFIED));
    CHECK(!nvn_ex_drain_done(NVN_EX_SYNC_TIMEOUT));
    CHECK(!nvn_ex_drain_done(NVN_EX_SYNC_FAILED)); /* a refused fence leaves the sync unsignaled */
}

static void test_names(void) {
    CHECK(nvn_ex_case_from_name("exec_fence_order") == NVN_EX_CASE_FENCE_ORDER);
    CHECK(nvn_ex_case_from_name("exec_gpu_to_cpu") == NVN_EX_CASE_GPU_TO_CPU);
    CHECK(nvn_ex_case_from_name("exec_cpu_to_gpu") == NVN_EX_CASE_CPU_TO_GPU);
    CHECK(nvn_ex_case_from_name("exec_timeout") == NVN_EX_CASE_TIMEOUT);
    CHECK(nvn_ex_case_from_name("exec_event_report") == NVN_EX_CASE_EVENT_REPORT);
    CHECK(nvn_ex_case_from_name("exec_two_queues") == NVN_EX_CASE_TWO_QUEUES);
    CHECK(nvn_ex_case_from_name("exec_cb_fence_flush") == NVN_EX_CASE_CB_FENCE_FLUSH);
    CHECK(nvn_ex_case_from_name("exec_queue_reinit") == NVN_EX_CASE_QUEUE_REINIT);
    CHECK(nvn_ex_case_from_name("") == NVN_EX_CASE_COUNT);
    CHECK(nvn_ex_case_from_name("exec_unknown") == NVN_EX_CASE_COUNT);
    CHECK(nvn_ex_case_from_name(NULL) == NVN_EX_CASE_COUNT);
    CHECK(!strcmp(nvn_ex_case_name(NVN_EX_CASE_GPU_TO_CPU), "exec_gpu_to_cpu"));
    CHECK(!strcmp(nvn_ex_case_name(NVN_EX_CASE_CB_FENCE_FLUSH), "exec_cb_fence_flush"));
    CHECK(!strcmp(nvn_ex_case_name(NVN_EX_CASE_QUEUE_REINIT), "exec_queue_reinit"));
}

int main(void) {
    test_rgba8();
    test_compare();
    test_decisions();
    test_drain();
    test_names();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_exectest: all checks passed\n");
    return 0;
}
