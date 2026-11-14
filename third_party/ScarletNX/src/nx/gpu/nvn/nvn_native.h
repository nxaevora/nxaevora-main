/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_NATIVE_H
#define SNX_NVN_NATIVE_H

#include "common/cpu/aarch64/cpu.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/nvn/nvn_replay.h"

/* nvn_native=1: the host answers the NVN functions that the game resolves, and the game's NVN library does not run
   (nvn-design.md). Init runs once the game's symbols and dispatch table are loaded, before nvn_trace_init and
   before any guest thread starts. */
void nvn_native_init(void);
/* at a clean exit: the functions that the game called and the host does not have */
void nvn_native_report(void);
/* 1 once nvn_native_init found nvn_native=1 and hooked the loader */
int nvn_native_active(void);
/* the host handler of an NVN function by name, or NULL when the host has none (the selftest compares it with the
   library). It does not need nvn_native=1. */
GuestFn nvn_native_handler(const char* name);
/* The device of the host record of a queue, for the execution test. 0 when the queue has no record. */
uint64_t nvn_native_queue_device(uint64_t queue);

/* The bind state of one buffer slot. bound follows the library: a size 0 uniform or vertex bind is stored but not active.
   plural is 1 when the bind came from a plural function and keeps the library's packet form at the draw. */
typedef struct { uint64_t addr, size; uint8_t bound, plural; } NvnBindEntry;
/* The state of one state-object bind, stored by the replay from the bind state op. */
typedef struct { uint32_t w[NVN_STATE_WORDS_MAX]; uint8_t bound; } NvnBoundState;
/* The value of a bind dirty flag: the bind stored new state that the next draw has not sent yet. */
#define NVN_BIND_DIRTY 1u
/* The state behind the callbacks of a replay (nvn-design.md, 3c decision 6). The frame job owns it for one replay. */
typedef struct {
    GpuChannel* channel; /* the private channel that the methods go to, used on the GPU thread only */
    uint32_t clip_limit; /* the device word at +0x18: 0 until its source is known, then SetRenderTargets is refused */
    /* The texture binds of one frame job: written by the replay, read by the shader and pipeline stage later. The host
       struct starts zeroed at each frame job, and the table is not cleared between the command lists of one job. */
    uint64_t texture_handle[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    uint8_t texture_bound[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    uint8_t texture_plural[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    uint8_t texture_dirty[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    /* The buffer binds of one frame job, written by the replay and read by the shader and pipeline stage later. */
    NvnBindEntry uniform[NVN_BIND_STAGE_MAX + 1u][NVN_UNIFORM_SLOT_MAX];
    NvnBindEntry storage[NVN_BIND_STAGE_MAX + 1u][NVN_STORAGE_SLOT_MAX];
    NvnBindEntry vertex[NVN_VERTEX_SLOT_MAX];
    NvnBindEntry xfb[NVN_XFB_SLOT_MAX];
    uint8_t uniform_dirty[NVN_BIND_STAGE_MAX + 1u][NVN_UNIFORM_SLOT_MAX];
    uint8_t storage_dirty[NVN_BIND_STAGE_MAX + 1u][NVN_STORAGE_SLOT_MAX];
    uint8_t vertex_dirty[NVN_VERTEX_SLOT_MAX];
    /* The bound state objects of one frame job, written by the replay and read by the pipeline stage later. */
    NvnBoundState state[NVN_STATE_COUNT];
    uint8_t state_dirty[NVN_STATE_COUNT];
    /* The draws, dispatches and program binds of one frame job. The replay thread writes them without a lock. */
    uint64_t draws[NVN_DRAW_KIND_COUNT];
    NvnOpDraw last_draw;
    uint64_t dispatches;       /* sent dispatches: a refusal does not count */
    uint32_t dispatch_refusals;
    uint32_t last_dispatch[3];
    uint64_t program[NVN_PROGRAM_STAGE_COUNT]; /* 0: no program of that stage */
    /* The 18 plain words of the bound compute program, stored by the replay at BindProgram. */
    uint32_t compute_words[NVN_PROGRAM_COMPUTE_WORDS];
    uint8_t compute_valid;
    /* The bound vertex state of one frame job. The replay thread writes it without a lock, and it is not cleared
       between the command lists of one job. */
    struct { uint32_t w; uint8_t enabled; } vertex_attrib[NVN_VERTEX_ATTRIB_MAX];
    struct { uint32_t stride, divisor; uint8_t enabled; } vertex_stream[NVN_VERTEX_STREAM_MAX];
    uint8_t vertex_attrib_dirty, vertex_stream_dirty;
    uint32_t vattrib_binds, vstream_binds;
    /* The clears, copies, reports and image binds of one frame job. The replay thread writes them without a lock. */
    uint64_t gpu_ops[NVN_OP_COUNT];
    NvnOpReportCounter last_report;
    NvnOpBindImage last_bind_image;
    /* Set at the first graphics program bind of the frame job, after the region base is written once. */
    uint8_t region_base_sent;
    /* The bind methods sent at the draws of the frame job, and the binds that a builder refused. */
    uint32_t bind_methods, bind_refusals;
} NvnNativeReplayHost;
/* Fills env for the replay of host lists. The callbacks read the records of the host under its lock and write
   methods on host->channel. window_origin_flip is 1 when the device window origin mode is 1. */
void nvn_native_replay_env(NvnNativeReplayHost* host, int window_origin_flip, NvnReplayEnv* env);
/* Replays the ops of the live command list that handle names on ch, with a host state of its own (the selftest). GPU
   thread only. 0, or -1 when the handle is not a live list, or the host state cannot be allocated */
int nvn_native_selftest_replay(GpuChannel* ch, uint64_t handle, int window_origin_flip, NvnReplayStats* stats);
/* Builds the queue init stream for the selftest's private channels, with the library's queue builders on a host arena
   (the macro code and the default state that a command buffer runs on). The selftest has no device record, so the
   window origin and the depth mode come from its device. Guest thread. The caller frees *words. 1 on success */
int nvn_native_selftest_init_words(CPU* c, uint32_t window_origin, uint32_t depth_mode, uint32_t** words,
                                   uint32_t* count);

#endif
