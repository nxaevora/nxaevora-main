/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/nvn_frame.h"
#include <stdlib.h>

int nvn_frame_add_lists(NvnPendingFrame* frame, NvnListStore* store, const uint64_t* handles, uint32_t count,
                        uint32_t* skipped) {
    uint32_t total = frame->handle_count;
    if (count > NVN_FRAME_LISTS_MAX - total) return NVN_FRAME_LIMIT;
    total += count;
    if (total > frame->handle_cap) {
        uint32_t cap = frame->handle_cap ? frame->handle_cap : NVN_FRAME_HANDLE_CAP;
        while (cap < total) cap *= 2;
        if (cap > NVN_FRAME_LISTS_MAX) cap = NVN_FRAME_LISTS_MAX;
        uint64_t* fresh = realloc(frame->handles, (size_t)cap * sizeof *fresh);
        if (!fresh) return NVN_FRAME_MEMORY;
        frame->handles = fresh;
        frame->handle_cap = cap;
    }
    uint32_t added = 0, missed = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (nvn_list_ref(store, handles[i]))
            frame->handles[frame->handle_count + added++] = handles[i];
        else
            missed++;
    }
    frame->handle_count += added;
    if (skipped) *skipped = missed;
    return NVN_FRAME_ADDED;
}

int nvn_frame_add_token(NvnPendingFrame* frame, uint64_t sync, uint32_t generation, uint32_t serial) {
    if (frame->token_count >= NVN_FRAME_TOKENS_MAX) return 0;
    if (frame->token_count == frame->token_cap) {
        uint32_t cap = frame->token_cap ? frame->token_cap * 2 : NVN_FRAME_TOKEN_CAP;
        if (cap > NVN_FRAME_TOKENS_MAX) cap = NVN_FRAME_TOKENS_MAX;
        NvnFrameToken* fresh = realloc(frame->tokens, (size_t)cap * sizeof *fresh);
        if (!fresh) return 0;
        frame->tokens = fresh;
        frame->token_cap = cap;
    }
    frame->tokens[frame->token_count++] = (NvnFrameToken){sync, generation, serial};
    return 1;
}

int nvn_frame_is_empty(const NvnPendingFrame* frame) { return frame->handle_count == 0; }

int nvn_frame_token_matches(const NvnFrameToken* token, uint32_t generation, uint32_t serial) {
    return token->generation == generation && token->serial == serial;
}

NvnPendingFrame nvn_frame_take(NvnPendingFrame* frame) {
    NvnPendingFrame taken = *frame;
    *frame = (NvnPendingFrame){0};
    return taken;
}

void nvn_frame_release(NvnPendingFrame* frame, NvnListStore* store) {
    for (uint32_t i = 0; i < frame->handle_count; i++) nvn_list_unref(store, frame->handles[i]);
    free(frame->handles);
    free(frame->tokens);
    *frame = (NvnPendingFrame){0};
}

int nvn_frame_log_due(uint32_t frame_number, int abnormal) {
    if (abnormal) return 1;
    if (frame_number <= NVN_FRAME_LOG_FIRST) return 1;
    return frame_number % NVN_FRAME_LOG_EVERY == 0;
}
