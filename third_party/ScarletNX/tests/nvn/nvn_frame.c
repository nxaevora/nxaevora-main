/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pending frame of a queue (gpu/nvn/nvn_frame.c): the references that a frame holds, the stale handles it skips, its
   limits, the move out, and the release of every handle. */
#include "nx/gpu/nvn/nvn_frame.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static uint64_t publish_one(NvnListStore* s) {
    NvnOpList l = {0};
    NvnOpBarrier b = {1, 0};
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &b, sizeof b));
    uint64_t handle = nvn_list_publish(s, &l);
    CHECK(handle);
    return handle;
}

static void test_add_lists(void) {
    NvnListStore s = {0};
    uint64_t a = publish_one(&s), b = publish_one(&s);
    CHECK(nvn_list_live(&s) == 2);
    NvnPendingFrame f = {0};
    CHECK(nvn_frame_is_empty(&f));
    uint32_t skipped = 99;
    uint64_t two[2] = {a, b};
    CHECK(nvn_frame_add_lists(&f, &s, two, 2, &skipped) == NVN_FRAME_ADDED);
    CHECK(skipped == 0 && f.handle_count == 2 && !nvn_frame_is_empty(&f));
    CHECK(s.slot[0]->refs == 2 && s.slot[1]->refs == 2); /* one from publish, one from the frame */
    uint64_t one[1] = {a};
    CHECK(nvn_frame_add_lists(&f, &s, one, 1, &skipped) == NVN_FRAME_ADDED);
    CHECK(skipped == 0 && f.handle_count == 3 && s.slot[0]->refs == 3);
    nvn_frame_release(&f, &s);
    CHECK(s.slot[0]->refs == 1 && s.slot[1]->refs == 1 && nvn_frame_is_empty(&f));
    nvn_list_store_free(&s);
}

static void test_stale_handles(void) {
    NvnListStore s = {0};
    uint64_t dead = publish_one(&s);
    nvn_list_unref(&s, dead);
    CHECK(nvn_list_live(&s) == 0);
    uint64_t live = publish_one(&s);
    NvnPendingFrame f = {0};
    uint32_t skipped = 0;
    uint64_t handles[2] = {dead, live};
    CHECK(nvn_frame_add_lists(&f, &s, handles, 2, &skipped) == NVN_FRAME_ADDED);
    CHECK(skipped == 1 && f.handle_count == 1 && f.handles[0] == live);
    nvn_frame_release(&f, &s);
    CHECK(nvn_list_live(&s) == 1);
    nvn_list_store_free(&s);
}

static void test_limit(void) {
    NvnListStore s = {0};
    NvnPendingFrame f = {0};
    f.handle_count = NVN_FRAME_LISTS_MAX - 1;
    f.handle_cap = NVN_FRAME_LISTS_MAX - 1;
    f.handles = calloc(f.handle_cap, sizeof *f.handles);
    CHECK(f.handles);
    uint64_t live = publish_one(&s);
    uint64_t two[2] = {live, live};
    uint32_t skipped = 7;
    CHECK(nvn_frame_add_lists(&f, &s, two, 2, &skipped) == NVN_FRAME_LIMIT);
    CHECK(f.handle_count == NVN_FRAME_LISTS_MAX - 1 && skipped == 7 && s.slot[0]->refs == 1);
    nvn_frame_release(&f, &s);
    CHECK(nvn_list_live(&s) == 1);
    nvn_list_store_free(&s);
}

static void test_take_and_release(void) {
    NvnListStore s = {0};
    uint64_t h = publish_one(&s);
    NvnPendingFrame f = {0};
    uint32_t skipped = 0;
    CHECK(nvn_frame_add_lists(&f, &s, &h, 1, &skipped) == NVN_FRAME_ADDED);
    CHECK(nvn_frame_add_token(&f, 0x1234, 5, 42));
    NvnPendingFrame taken = nvn_frame_take(&f);
    CHECK(nvn_frame_is_empty(&f) && !f.handles && !f.tokens && f.handle_count == 0);
    CHECK(taken.handle_count == 1 && taken.token_count == 1 && taken.tokens[0].sync == 0x1234);
    CHECK(taken.tokens[0].generation == 5 && taken.tokens[0].serial == 42);
    nvn_list_unref(&s, h); /* the owner goes: the frame still holds one */
    CHECK(nvn_list_live(&s) == 1);
    nvn_frame_release(&taken, &s);
    CHECK(nvn_list_live(&s) == 0 && s.leaked == 0);
    nvn_frame_release(&f, &s);     /* an empty frame is safe */
    nvn_frame_release(&taken, &s); /* it is empty already */
    nvn_list_store_free(&s);
}

static void test_token_limit(void) {
    NvnPendingFrame f = {0};
    for (uint32_t i = 0; i < NVN_FRAME_TOKENS_MAX; i++) CHECK(nvn_frame_add_token(&f, i, 1, i + 1));
    CHECK(!nvn_frame_add_token(&f, 1, 1, 1) && f.token_count == NVN_FRAME_TOKENS_MAX);
    CHECK(f.tokens[NVN_FRAME_TOKENS_MAX - 1].serial == NVN_FRAME_TOKENS_MAX);
    nvn_frame_release(&f, NULL); /* no handle, so the store is not read */
    CHECK(f.token_count == 0 && !f.tokens);
}

static void test_token_matches(void) {
    const NvnFrameToken token = {0x1234, 5, 42};
    CHECK(nvn_frame_token_matches(&token, 5, 42));
    CHECK(!nvn_frame_token_matches(&token, 6, 42)); /* another record generation */
    CHECK(!nvn_frame_token_matches(&token, 5, 43)); /* a newer fence of the same sync */
    CHECK(!nvn_frame_token_matches(&token, 6, 43));
}

static void test_log_due(void) {
    CHECK(nvn_frame_log_due(1, 0));
    CHECK(nvn_frame_log_due(NVN_FRAME_LOG_FIRST, 0));
    CHECK(!nvn_frame_log_due(NVN_FRAME_LOG_FIRST + 1, 0));
    CHECK(!nvn_frame_log_due(NVN_FRAME_LOG_EVERY - 1, 0));
    CHECK(nvn_frame_log_due(NVN_FRAME_LOG_EVERY, 0));
    CHECK(nvn_frame_log_due(NVN_FRAME_LOG_EVERY * 2, 0));
    CHECK(!nvn_frame_log_due(NVN_FRAME_LOG_EVERY * 2 + 1, 0));
    CHECK(nvn_frame_log_due(NVN_FRAME_LOG_FIRST + 1, 1));
    CHECK(nvn_frame_log_due(0, 1));
}

int main(void) {
    test_add_lists();
    test_stale_handles();
    test_limit();
    test_take_and_release();
    test_token_limit();
    test_token_matches();
    test_log_due();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_frame: ok\n");
    return 0;
}
