/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_FRAME_H
#define SNX_NVN_FRAME_H
#include "nx/gpu/nvn/nvn_cmdlist.h"
#include <stdint.h>

/* The pending frame of a queue (nvn-design.md, 3c decisions 4, 8 and 11). A frame holds one reference of each
   submitted list and the tokens that FenceSync named. The module has no locking: the owner serializes the calls. */

#define NVN_FRAME_LISTS_MAX   65536u /* handles in one frame */
#define NVN_FRAME_TOKENS_MAX  256u   /* syncs fenced on one frame */
#define NVN_FRAME_HANDLE_CAP  64u    /* first allocation of the handle array */
#define NVN_FRAME_TOKEN_CAP   16u    /* first allocation of the token array */
#define NVN_FRAME_LOG_FIRST   60u    /* the first frames of a run, each logged */
#define NVN_FRAME_LOG_EVERY   300u   /* after that, every frame with this number */

/* nvn_frame_add_lists returns one of these. LIMIT and MEMORY differ, so a caller can log one and stop on the other. */
#define NVN_FRAME_ADDED   1
#define NVN_FRAME_LIMIT   0
#define NVN_FRAME_MEMORY  (-1)

typedef struct {
    uint64_t sync;       /* the guest address of the sync */
    uint32_t generation; /* the record generation that FenceSync named */
    uint32_t serial;     /* the newest fence of that sync when the token was made */
} NvnFrameToken;

typedef struct {
    uint64_t* handles;
    uint32_t handle_count, handle_cap;
    NvnFrameToken* tokens;
    uint32_t token_count, token_cap;
} NvnPendingFrame; /* zero-initialized is an empty frame */

/* Takes one reference of each live handle and adds it to the frame. A handle that is not live goes to *skipped and is
   not stored; *skipped is written only on success. When the new handles do not fit, nothing is added and the function
   returns NVN_FRAME_LIMIT with *skipped as it was. Memory is out: no reference is taken and the frame keeps its
   handles. */
int nvn_frame_add_lists(NvnPendingFrame* frame, NvnListStore* store, const uint64_t* handles, uint32_t count,
                        uint32_t* skipped);
/* Adds a token. 0 when the token list is full or memory is out. */
int nvn_frame_add_token(NvnPendingFrame* frame, uint64_t sync, uint32_t generation, uint32_t serial);
/* 1 when the token names the newest fence of its sync record: the record generation and the serial both match. Pure. */
int nvn_frame_token_matches(const NvnFrameToken* token, uint32_t generation, uint32_t serial);
/* 1 when the frame has no lists. A token only exists after a non-empty frame, so tokens are not counted. */
int nvn_frame_is_empty(const NvnPendingFrame* frame);
/* The frame moves to the result and the source is empty. */
NvnPendingFrame nvn_frame_take(NvnPendingFrame* frame);
/* Gives back one reference of each handle, frees both arrays, and zeroes the frame. */
void nvn_frame_release(NvnPendingFrame* frame, NvnListStore* store);
/* 1 when the frame line is logged: the first NVN_FRAME_LOG_FIRST frames, then every NVN_FRAME_LOG_EVERYth, and always
   when abnormal is set. frame_number counts from 1. Pure. */
int nvn_frame_log_due(uint32_t frame_number, int abnormal);

#endif
