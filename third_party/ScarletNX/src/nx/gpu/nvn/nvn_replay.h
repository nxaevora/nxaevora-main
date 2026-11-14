/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_REPLAY_H
#define SNX_NVN_REPLAY_H
#include "nx/gpu/nvn/nvn_cmdlist.h"
#include <stdint.h>

/* The replay of a command list (nvn-design.md, "3c Host design decisions", decision 6). The module walks the ops
   of a list and turns each op into Maxwell 3D methods. It reaches the outside only through NvnReplayEnv, so a host
   test can run it. It has no lock, no global state and no allocation. */

#define NVN_REPLAY_LAYER_MAX 4096u /* the most layers of one clear */

typedef struct NvnReplayEnv {
    void* ctx;
    /* one complete method write: the value v for the method m. The caller binds it to maxwell3d_method. */
    void (*method)(void* ctx, uint32_t m, uint32_t v);
    /* the current value of an engine register (the caller reads the channel's registers) */
    uint32_t (*read_register)(void* ctx, uint32_t m);
    /* the published list of a handle, or NULL when the handle is not live */
    const NvnOpList* (*find_list)(void* ctx, uint64_t handle);
    /* The GPU base address and the descriptor count of the pool that a pool op names. 1 when found, 0 when the pool
       object is gone (op is NVN_OP_SET_TEXTURE_POOL or NVN_OP_SET_SAMPLER_POOL). */
    int (*descriptor_pool)(void* ctx, NvnOpCode op, const NvnOpPool* pool, uint64_t* gpu_base, uint32_t* count);
    /* The bind state of one stage and slot. 1 when stored, 0 when refused (the host reports the cause). */
    int (*bind_texture)(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle);
    /* The same for a bind that came from BindTextures: the host keeps the library's mode 5 packet form. */
    int (*bind_texture_plural)(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle);
    /* The bind state of one buffer slot. 1 when stored, 0 when refused (the host reports the cause). */
    int (*bind_buffer)(void* ctx, const NvnOpBindBuffer* op);
    /* The bind state of one state object. 1 when stored, 0 when refused (the host reports the cause). */
    int (*bind_state)(void* ctx, const NvnOpBindState* op);
    /* A draw, a dispatch or a program bind. 1 when stored, 0 when refused (the host reports the cause). */
    int (*draw)(void* ctx, const NvnOpDraw* op);
    int (*dispatch)(void* ctx, const NvnOpDispatch* op);
    int (*bind_program)(void* ctx, const NvnOpBindProgram* op);
    /* The vertex state of one bind. 1 when stored, 0 when refused (the host reports the cause). */
    int (*bind_vertex_attrib)(void* ctx, const NvnOpBindVertexAttrib* op);
    int (*bind_vertex_stream)(void* ctx, const NvnOpBindVertexStream* op);
    /* The caller writes the render target methods itself. 1 when done, 0 when it could not (it reports the cause). */
    int (*render_targets)(void* ctx, const NvnOpRenderTargets* op);
    /* A clear, a copy, a report or an image bind. 1 when stored, 0 when refused (the host reports the cause). */
    int (*gpu_op)(void* ctx, NvnOpCode code, const void* payload);
    /* a bad part: a short text and one value. The module then skips the op and goes on. */
    void (*fault)(void* ctx, const char* what, uint64_t value);
    int window_origin_flip; /* 1 when the device window origin mode is 1: the viewport Y scale is negative */
    /* a method of a given subchannel: a puller method below 0x40 on any subchannel, or a method of the compute
       engine on subchannel 1. The caller binds it to gpu_channel_method. */
    void (*channel_method)(void* ctx, uint32_t subchannel, uint32_t m, uint32_t v);
} NvnReplayEnv;

typedef struct {
    uint32_t replayed[NVN_OP_COUNT];   /* ops that ended without a fault, by op code */
    uint32_t barrier_bits[9];          /* how many times bit 0 to bit 8 was set in a Barrier op */
    uint32_t methods;                  /* method writes done */
    uint32_t faults;                   /* ops skipped, or parts of an op skipped, with a fault call */
} NvnReplayStats;

/* Replays the ops of list in order. Never stops early: a fault skips the op. stats is added to, not cleared. */
void nvn_replay_list(const NvnReplayEnv* env, const NvnOpList* list, NvnReplayStats* stats);

#endif
