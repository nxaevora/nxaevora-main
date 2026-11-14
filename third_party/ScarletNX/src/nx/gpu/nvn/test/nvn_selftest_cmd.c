/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The command buffer cases of the NVN selftest (nvn-unobserved.md, "The selftest") for step 4-u3-3: the same calls go
   to the library's command buffer and to the host's, the library recording is decoded into push buffer words, and both
   sides run on a private channel of their own in one GPU call. The records compare kinds S (the registers at the clear)
   and Y (the order of the sync operations). */
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/gpu/nvn/test/nvn_selftest_dec.h"
#include "nx/gpu/nvn/test/nvn_selftest_rec.h"
#include "nx/gpu/nvn/test/nvn_selftest_regdiff.h"
#include "nx/gpu/nvn/nvn_native.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/renderer.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/memory/mem.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A GPU call that does not finish in this time is a hang, not a slow case. */
#define ST_GPU_WAIT_MS 5000u
#define ST_CMD_BYTES 0x1000u
#define ST_CONTROL_BYTES 0x800u
#define ST_CMD_REFILL_BYTES 0x400u
#define ST_CB_OBJ_BYTES 0x200u /* the NVN part of a game command buffer is at most 0xE0 (docs/graphics/nvn-design.md) */
#define ST_MAX_RANGES 64u
#define ST_VIEWPORTS 2u
#define ST_SCISSORS 2u
#define ST_CLEAR_MASK 0xFu
#define ST_BARRIERS 9u
#define ST_STATE_CASES 29u
#define ST_BIND_CASES 15u
#define ST_D3_CASES 30u
#define ST_D4_CASES 11u
#define ST_CASE_COUNT (1u + ST_BARRIERS + ST_STATE_CASES + ST_BIND_CASES + ST_D3_CASES + ST_D4_CASES)
#define ST_IGNORE_MAX 12u

/* The work calls of one side: the registers at the clear. A later call means that the case cannot be compared. */
#define ST_MAX_WORKS 2u

/* The device word that SetRenderTargets compares with 0x08c8b771 (nvn_selftest.c's DEVICE_DEPTH_MODE_OFFSET). */
#define ST_DEVICE_DEPTH_MODE_OFFSET 0x1a04u

/* ClearColor is macro 1: it writes CLEAR_SURFACE once per layer of the target, at 0x206 + 16 per target. A real
   render target has one layer; the cases do not call SetRenderTargets, so the fixture sets that count. */
#define ST_CLEAR_LAYER_COUNT_REG 0x206u
#define ST_CLEAR_LAYER_COUNT 1u

/* The work call of a Bind case that the host sends at a draw: mode 4 is NVN triangles (an assumption that the caller
   checks), the first vertex is 0 and the count is 3. */
#define ST_DRAW_MODE 4u
#define ST_DRAW_FIRST 0u
#define ST_DRAW_COUNT 3u

/* The d3 calls: the arguments that the case table does not keep. A buffer address is the same literal on both sides,
   because the recorder writes no memory, so no arena address can differ. */
#define ST_D3_DRAW_MODE 4u
#define ST_D3_DRAW_FIRST 1u
#define ST_D3_DRAW_COUNT 3u
#define ST_D3_INDEX_TYPE 1u
#define ST_D3_INSTANCES 2u
#define ST_D3_INDEX_ADDRESS 0x100005000ull
#define ST_D3_BUFFER_A 0x100005000ull
#define ST_D3_BUFFER_B 0x100006000ull
#define ST_D3_UNIFORM_ADDRESS 0x100007000ull
#define ST_D3_COPY_SIZE 0x100ull
#define ST_D3_CLEAR_VALUE 0xDEADBEEFu
#define ST_D3_UNIFORM_SIZE 0x180u
#define ST_D3_UNIFORM_OFFSET 0x40u
#define ST_D3_UNIFORM_BYTES 0x40u
#define ST_D3_DATA_WORDS 16u
#define ST_D3_REGION_X 0u
#define ST_D3_REGION_Y 0u
#define ST_D3_REGION_LAYER 0u
#define ST_D3_REGION_WIDTH 32u
#define ST_D3_REGION_WIDTH_OFFSET 0x0cu
#define ST_D3_REGION_HEIGHT 32u
#define ST_D3_REGION_LAYERS 1u
#define ST_D3_COPY_COMMANDS 1u
#define ST_D3_DISCARD_INDEX 3u
#define ST_D3_DEPTH 0.25f
#define ST_D3_DEPTH_ENABLE 1u
#define ST_D3_STENCIL 0x5Au
#define ST_D3_STENCIL_MASK 0xFFu
#define ST_D3_MASK 0xFu

/* The d4 calls: the debug description literal, the text of the two Dynamic functions, the ZCull range and the group
   id. A description literal is the same on both sides, so neither side needs a per-side address. */
#define ST_D4_DESCRIPTION 0x1122334455667788ull
#define ST_D4_DESC_LENGTH 0x10u
#define ST_D4_MARKER_ID 7u
#define ST_D4_ZCULL_ADDRESS 0x12340000u
#define ST_D4_ZCULL_SIZE 0x1000u
#define ST_D4_TEXT "abc"

/* The d2 descriptor pools and render targets: one memory pool of each side holds the texture table at 0, the sampler
   table at 0x4000 and two 32x32 textures. */
#define ST_BIND_OBJ_BYTES 0x1000u
#define ST_BIND_POOL_BYTES 0x10000u
#define ST_BIND_ENTRIES 512u
#define ST_BIND_TEXTURE_POOL_OFFSET 0u
#define ST_BIND_SAMPLER_POOL_OFFSET 0x4000u
#define ST_BIND_TEX_OFFSET_A 0x8000u
#define ST_BIND_TEX_OFFSET_B 0x9000u
#define ST_BIND_TEX_SIDE 32u
#define ST_BIND_TEX_FORMAT 0x25u
#define ST_RT_COUNT 2u

/* A pool record (0x13 texture pool, 0x14 sampler pool) names the pool object at +0, the base offset at +8 and the size
   at +0x10. The object's second pointer holds the virtual function at +0x38 that gives the base of the pool memory. */
#define ST_POOL_READ_BYTES 0x14u
#define ST_POOL_BASE_OFFSET 0x08u
#define ST_POOL_SIZE_OFFSET 0x10u
#define ST_POOL_VT_FN_OFFSET 0x38u
#define ST_POOL_ITEMS_MAX 4u
#define ST_POOL_ITEM_WORDS 12u

/* The argument block of the cases, in the arena: the same values for the library, a changed copy for a mutation. Each
   region is 16-byte aligned. */
#define ST_ARG_VP_LIB 0x000u
#define ST_ARG_VP_HOST 0x020u
#define ST_ARG_SC_LIB 0x040u
#define ST_ARG_SC_HOST 0x060u
#define ST_ARG_COLOR 0x080u
#define ST_ARG_ARRAY_LIB 0x090u /* the floats of an array argument: a blend color or a depth range pair */
#define ST_ARG_ARRAY_HOST 0x0A0u
#define ST_ARG_STATE_LIB 0x0B0u /* one state object for each side of the Bind cases */
#define ST_ARG_STATE_HOST 0x2B0u
#define ST_ARG_ATTRIB_LIB 0x4B0u /* the Bind case arrays: a lib copy and a host copy of each */
#define ST_ARG_ATTRIB_HOST 0x4C0u
#define ST_ARG_STREAM_LIB 0x4D0u
#define ST_ARG_STREAM_HOST 0x4E0u
#define ST_ARG_VBUF_LIB 0x4F0u
#define ST_ARG_VBUF_HOST 0x510u
#define ST_ARG_UBUF_LIB 0x530u
#define ST_ARG_UBUF_HOST 0x550u
#define ST_ARG_SBUF_LIB 0x570u
#define ST_ARG_SBUF_HOST 0x590u
#define ST_ARG_TEX_LIB 0x5B0u
#define ST_ARG_TEX_HOST 0x5C0u
#define ST_ARG_IMG_LIB 0x5D0u
#define ST_ARG_IMG_HOST 0x5E0u
#define ST_ARG_RT_LIB 0x5F0u
#define ST_ARG_RT_HOST 0x600u
#define ST_ARG_REGION_LIB 0x610u /* a 6-word copy or clear region, one for each side */
#define ST_ARG_REGION_HOST 0x630u
#define ST_ARG_TEXCOLOR_LIB 0x650u /* the 4-word color of a clear, one for each side */
#define ST_ARG_TEXCOLOR_HOST 0x660u
#define ST_ARG_DATA_LIB 0x670u /* the 16-word upload data, one for each side */
#define ST_ARG_DATA_HOST 0x6B0u
#define ST_ARG_HANDLE_LIB 0x6F0u /* the one source handle of CopyCommands, one for each side */
#define ST_ARG_HANDLE_HOST 0x6F8u
#define ST_ARG_TEXT 0x700u /* the "abc" text of the two Dynamic debug functions, the same on both sides */
#define ST_ARG_BYTES 0x710u

/* The vertex arrays of the two Bind cases: two elements in each 16-byte argument region. */
#define ST_ATTRIB_ELEM_BYTES 4u
#define ST_ATTRIB_ELEMENTS 2u
#define ST_STREAM_ELEM_BYTES 8u
#define ST_STREAM_ELEMENTS 2u

#define ST_CLEAR_INDEX 0u
#define ST_MUTATE_VIEWPORT_WORD 0u /* the x of viewport 0 */
#define ST_MUTATE_VIEWPORT_ADD 1.0f

static const float st_viewports[8] = {0.0f, 0.0f, 1280.0f, 720.0f, 16.0f, 32.0f, 640.0f, 360.0f};
static const int32_t st_scissors[8] = {0, 0, 1280, 720, 8, 8, 100, 50};
static const float st_clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};

typedef struct {
    NvnStProc initialize, finalize, add_command_memory, add_control_memory, begin, end;
    NvnStProc set_viewports, set_scissors, clear_color, barrier, draw_arrays;
    NvnStProc free_bytes; /* optional: without it the command memory is refilled before each case */
} StCmdProcs;

/* One GPU job: both channels, the decoded ranges of the library recording, one record of each side, and the register
   copies of each work call. The copies are large, so they live in this heap object (the job is calloc'd). */
typedef struct {
    GpuChannel* lib;
    GpuChannel* host;
    const char* name;
    NvnStRange ranges[ST_MAX_RANGES];
    unsigned n_ranges;
    uint64_t host_handle;
    int window_origin_flip;
    const u32* init_words; /* the queue init stream: the channels run it once, before the first case */
    GpuChannel* base;       /* the channel state after the init stream, which each case starts from */
    unsigned init_count;
    int channels_ready;
    int init_failed; /* the init stream ended inside a header: no case runs on these channels */
    u32 lib_regs[ST_MAX_WORKS][NVN_ST_ENGINE_COUNT][KEPLER_COMPUTE_METHODS];
    u32 host_regs[ST_MAX_WORKS][NVN_ST_ENGINE_COUNT][KEPLER_COMPUTE_METHODS];
    unsigned lib_works, host_works;
    int work_overflow; /* a side had a third work call */
    const uint32_t* ignore; /* the registers that the diff leaves out of this case */
    unsigned n_ignore;
    NvnStRec* ref;
    NvnStRec* got;
    NvnStRec lib_rec, host_rec;
    int status; /* 0, or -1 when a side could not run */
    int done;
    PMutex lock;
    PCond cv;
    uint64_t lib_control, host_control; /* the control memory of each command buffer, given again each case */
    uint64_t pool_scratch;              /* the push buffer words of the expanded pool records, one block per item */
    uint64_t texture_pool_gva[2];       /* the GPU address of each side's texture pool, for the write records */
} StCmdJob;

/* The recorder calls the snapshot on the GPU thread, and one job runs at a time on that thread (one guest thread runs
   the selftest). */
static StCmdJob* st_snapshot_job;

/* One ignore entry: the engine in the high half, the register in the low half. Engine 0 keeps the plain register value,
   so the r3d lists of the earlier cases need no change. */
#define ST_IGNORE_REG(engine, reg) (((uint32_t)(engine) << 16) | (uint32_t)(reg))

/* The 3D registers end at MAXWELL3D_NUM_REGS: the macro calls above them are not registers. The 2D, DMA and compute
   engines use all of their arrays. */
static const u32* st_engine_regs(const GpuChannel* ch, unsigned engine) {
    return (const u32*)((const u8*)ch + nvn_st_engines[engine].offset);
}

/* The snapshot of one work call: copy the four engine register arrays of the side, for the diff after both sides ran.
   It adds no field, because a register that both sides left as the base is not a difference. */
static void st_snapshot_regs(GpuChannel* ch, NvnStRec* rec) {
    (void)rec;
    StCmdJob* job = st_snapshot_job;
    if (!job) return;
    const int lib = ch == job->lib;
    if (!lib && ch != job->host) return;
    unsigned* count = lib ? &job->lib_works : &job->host_works;
    if (*count >= ST_MAX_WORKS) {
        if (!job->work_overflow)
            LOG("selftest: %s: more than %u work calls on the %s side\n", job->name, ST_MAX_WORKS,
                lib ? "library" : "host");
        job->work_overflow = 1;
        return;
    }
    u32 (*const dst)[KEPLER_COMPUTE_METHODS] = lib ? job->lib_regs[*count] : job->host_regs[*count];
    for (unsigned e = 0; e < NVN_ST_ENGINE_COUNT; e++) {
        const NvnStEngine* const engine = &nvn_st_engines[e];
        memcpy(dst[e], st_engine_regs(ch, e), engine->count * sizeof dst[e][0]);
    }
    (*count)++;
}

/* 1 when the case's list leaves this register of this engine out: its value is a per-side pool or texture address. */
static int st_reg_ignored(const StCmdJob* job, unsigned engine, unsigned reg) {
    const uint32_t key = ST_IGNORE_REG(engine, reg);
    for (unsigned i = 0; i < job->n_ignore; i++)
        if (job->ignore[i] == key) return 1;
    return 0;
}

/* Adds one S field for each register that a side left different from the base, in engine and register order. The fields
   go to both records, with the value of each side, so a mutation shows as a difference and an equal write does not. */
static void st_add_regdiff(StCmdJob* job) {
    if (!job->base) return;
    if (job->work_overflow) {
        /* The copies are incomplete, so the record cannot be compared: a full table is a fail as well. */
        job->lib_rec.overflow |= NVN_ST_S;
        job->host_rec.overflow |= NVN_ST_S;
        return;
    }
    if (job->lib_works != job->host_works) {
        LOG("selftest: %s: %u library work calls, %u host work calls\n", job->name, job->lib_works,
            job->host_works);
        return;
    }
    u32 regs[NVN_ST_MAX_FIELDS];
    unsigned ignored = 0;
    for (unsigned w = 0; w < job->lib_works; w++) {
        for (unsigned e = 0; e < NVN_ST_ENGINE_COUNT; e++) {
            const NvnStEngine* const engine = &nvn_st_engines[e];
            int more = 0;
            const unsigned n = nvn_st_regdiff(st_engine_regs(job->base, e), job->lib_regs[w][e],
                                              job->host_regs[w][e], engine->count, regs, NVN_ST_MAX_FIELDS, &more);
            for (unsigned i = 0; i < n; i++) {
                const unsigned r = regs[i];
                if (st_reg_ignored(job, e, r)) {
                    ignored++;
                    continue;
                }
                const char* const name = nvn_st_engine_reg_name(e, r);
                nvn_st_add_state(&job->lib_rec, name, job->lib_regs[w][e][r]);
                nvn_st_add_state(&job->host_rec, name, job->host_regs[w][e][r]);
            }
            if (!more) continue;
            /* Ignored registers free fields, so a dropped difference could pass: the case fails instead. */
            LOG("selftest: %s: more than %u changed %s registers\n", job->name, NVN_ST_MAX_FIELDS, engine->name);
            job->lib_rec.overflow |= NVN_ST_S;
            job->host_rec.overflow |= NVN_ST_S;
        }
    }
    if (ignored) LOG("selftest: %s: %u registers ignored (per-side addresses)\n", job->name, ignored);
}

/* The id and the heap buffers belong to the channel: the base shares neither. */
static void st_channel_reset(GpuChannel* ch, const GpuChannel* base) {
    const u32 id = ch->id;
    u32* const inline_indices = ch->inline_indices;
    const u32 inline_cap = ch->inline_cap;
    u8* const i2m_buffer = ch->i2m.inner_buffer;
    const u32 i2m_capacity = ch->i2m.capacity;
    *ch = *base;
    ch->id = id;
    ch->inline_indices = inline_indices;
    ch->inline_cap = inline_cap;
    ch->inline_n = 0;
    ch->i2m.inner_buffer = i2m_buffer;
    ch->i2m.capacity = i2m_capacity;
    ch->i2m.copy_size = 0;
    ch->i2m.write_offset = 0;
}

typedef struct {
    const NvnStArena* arena;
} StReadCtx;

#define ST_CALL(c, p, side, ...) \
    nvn_st_call((c), (side) ? &(p)->host : &(p)->lib, &(NvnStCall){.x = {__VA_ARGS__}})

static uint32_t st_f2b(float v) {
    uint32_t b;
    memcpy(&b, &v, sizeof b);
    return b;
}

static float st_b2f(uint32_t b) {
    float v;
    memcpy(&v, &b, sizeof v);
    return v;
}

/* A call with integer and float arguments: ST_CALL passes x only, and the float values are bit patterns in s. */
static void st_call_proc(CPU* c, const NvnStProc* p, int host, const NvnStCall* a) {
    nvn_st_call(c, host ? &p->host : &p->lib, a);
}

static const void* st_guest_read(void* ctx, uint64_t addr, size_t size) {
    const StReadCtx* r = ctx;
    if (!nvn_st_arena_contains(r->arena, addr, size)) return NULL;
    return GP(addr);
}

static int st_procs_find(StCmdProcs* p) {
    if (!nvn_st_proc_find(&p->initialize, "nvnCommandBufferInitialize") ||
        !nvn_st_proc_find(&p->finalize, "nvnCommandBufferFinalize") ||
        !nvn_st_proc_find(&p->add_command_memory, "nvnCommandBufferAddCommandMemory") ||
        !nvn_st_proc_find(&p->add_control_memory, "nvnCommandBufferAddControlMemory") ||
        !nvn_st_proc_find(&p->begin, "nvnCommandBufferBeginRecording") ||
        !nvn_st_proc_find(&p->end, "nvnCommandBufferEndRecording") ||
        !nvn_st_proc_find(&p->set_viewports, "nvnCommandBufferSetViewports") ||
        !nvn_st_proc_find(&p->set_scissors, "nvnCommandBufferSetScissors") ||
        !nvn_st_proc_find(&p->clear_color, "nvnCommandBufferClearColor") ||
        !nvn_st_proc_find(&p->barrier, "nvnCommandBufferBarrier") ||
        !nvn_st_proc_find(&p->draw_arrays, "nvnCommandBufferDrawArrays"))
        return 0;
    nvn_st_proc_find(&p->free_bytes, "nvnCommandBufferGetCommandMemoryFree");
    return 1;
}

static void st_gpu_fn(void* arg) {
    StCmdJob* job = arg;
    const Renderer* prev = g_renderer;
    g_renderer = &nvn_st_renderer;
    if (!job->channels_ready) {
        /* No recording is open yet, so the init stream's work calls reach the recorder as a no-op. */
        const int failed = gpu_channel_words(job->lib, job->init_words, job->init_count) |
                           gpu_channel_words(job->host, job->init_words, job->init_count);
        job->lib->r3d[ST_CLEAR_LAYER_COUNT_REG] = ST_CLEAR_LAYER_COUNT;
        job->host->r3d[ST_CLEAR_LAYER_COUNT_REG] = ST_CLEAR_LAYER_COUNT;
        job->channels_ready = 1;
        job->init_failed = failed != 0;
        if (failed) LOG("selftest: the queue init stream ends inside a header\n");
        if (!failed) {
            job->base = malloc(sizeof *job->base);
            if (!job->base) {
                LOG("selftest: out of memory for the channel base copy\n");
                job->init_failed = 1;
            } else {
                memcpy(job->base, job->lib, sizeof *job->base);
            }
        }
    }
    if (!job->init_failed) {
        st_channel_reset(job->lib, job->base);
        st_channel_reset(job->host, job->base);
    }
    if (job->init_failed) job->status = -1;
    job->lib_works = 0;
    job->host_works = 0;
    job->work_overflow = 0;
    st_snapshot_job = job;
    nvn_st_renderer_begin(job->ref, st_snapshot_regs);
    nvn_st_renderer_set_texture_pool(job->texture_pool_gva[0]);
    for (unsigned i = 0; i < job->n_ranges && job->status == 0; i++) {
        const u32* words = (const u32*)GP(job->ranges[i].cpu_start);
        if (gpu_channel_words(job->lib, words, job->ranges[i].words)) {
            LOG("selftest: %s: library word range %u ends inside a header\n", job->name, i);
            job->status = -1;
        }
    }
    /* The front end holds a draw for merging (r3d_pending_flush): a stream that ends in a draw must give it here. */
    r3d_pending_flush();
    nvn_st_renderer_end();
    nvn_st_renderer_begin(job->got, st_snapshot_regs);
    nvn_st_renderer_set_texture_pool(job->texture_pool_gva[1]);
    NvnReplayStats stats = {0};
    if (nvn_native_selftest_replay(job->host, job->host_handle, job->window_origin_flip, &stats)) {
        LOG("selftest: %s: the host replay failed\n", job->name);
        job->status = -1;
    }
    r3d_pending_flush();
    nvn_st_renderer_end();
    st_snapshot_job = NULL;
    st_add_regdiff(job);
    g_renderer = prev;
    pm_lock(&job->lock);
    job->done = 1;
    pc_broadcast(&job->cv);
    pm_unlock(&job->lock);
}

/* 0 when both sides ran, -1 when one side failed, 1 when the GPU thread did not finish in time. */
static int st_run_gpu(StCmdJob* job) {
    gpu_call(st_gpu_fn, job, NULL, 0);
    const u64 end = plat_ns() + (u64)ST_GPU_WAIT_MS * 1000000u;
    int timed_out = 0;
    pm_lock(&job->lock);
    while (!job->done) {
        const i64 left = (i64)(end - plat_ns());
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        pc_wait(&job->cv, &job->lock, left);
    }
    const int status = job->status;
    pm_unlock(&job->lock);
    if (timed_out) return 1;
    return status ? -1 : 0;
}

static void st_skip(NvnStRun* run, const char* name, const char* args, int status, const NvnStDecodeFault* fault) {
    run->skipped++;
    switch (status) {
        case NVN_ST_DEC_REFUSED_OP:
            LOG("selftest: %s %s skipped, record op %u at %llx\n", name, args, fault->op,
                (unsigned long long)fault->addr);
            break;
        case NVN_ST_DEC_TOO_MANY_RANGES:
            LOG("selftest: %s %s skipped, more than %u ranges\n", name, args, ST_MAX_RANGES);
            break;
        case NVN_ST_DEC_TOO_MANY_JUMPS:
            LOG("selftest: %s %s skipped, a jump cycle at %llx\n", name, args, (unsigned long long)fault->addr);
            break;
        case NVN_ST_DEC_BAD_ENTRY:
            LOG("selftest: %s %s skipped, a bad record entry at %llx\n", name, args, (unsigned long long)fault->addr);
            break;
        default:
            LOG("selftest: %s %s skipped, the recording at %llx is unreadable\n", name, args,
                (unsigned long long)fault->addr);
            break;
    }
}

static void st_skip_all(NvnStRun* run, const char* why) {
    run->skipped += ST_CASE_COUNT;
    LOG("selftest: the command buffer cases are skipped, %s\n", why);
}

/* A full 0x1000 byte command chunk holds more than one case. Without the free word the chunk is replaced each case. */
static void st_refill(CPU* c, StCmdProcs* p, uint64_t lib_cb, uint64_t host_cb, uint64_t lib_pool, uint64_t host_pool) {
    uint64_t free_bytes = 0;
    if (p->free_bytes.lib.fn) {
        free_bytes = ST_CALL(c, &p->free_bytes, 0, lib_cb);
        if (free_bytes >= ST_CMD_REFILL_BYTES) return;
        LOG("selftest: command memory refilled, %llu bytes were free\n", (unsigned long long)free_bytes);
    }
    ST_CALL(c, &p->add_command_memory, 0, lib_cb, lib_pool, 0, ST_CMD_BYTES);
    ST_CALL(c, &p->add_command_memory, 1, host_cb, host_pool, 0, ST_CMD_BYTES);
}

static int st_setup_lists(CPU* c, StCmdProcs* p, NvnStResCache* cache, NvnStRun* run, int host, uint64_t cb,
                          uint64_t control, NvnStResPool* pool) {
    if (!nvn_st_res_pool_create(c, run, cache, host, pool, NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED, ST_CMD_BYTES))
        return 0;
    memset(GP(cb), 0, ST_CB_OBJ_BYTES);
    memset(GP(control), 0, ST_CONTROL_BYTES);
    if (!ST_CALL(c, &p->initialize, host, cb, nvn_st_device())) {
        LOG("selftest: a command buffer was not initialized (host=%d)\n", host);
        return 0;
    }
    ST_CALL(c, &p->add_command_memory, host, cb, pool->pool, 0, ST_CMD_BYTES);
    ST_CALL(c, &p->add_control_memory, host, cb, control, ST_CONTROL_BYTES);
    return 1;
}

/* The work call that ends the calls of a case's script. */
typedef void (*StWork)(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args);

/* The work point of every case: the clear that ends the calls of the script. */
static void st_clear_work(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args) {
    ST_CALL(c, &p->clear_color, host, cb, ST_CLEAR_INDEX, args + ST_ARG_COLOR, ST_CLEAR_MASK);
}

/* The host sends the BLEND, CHANNEL_MASK and POLYGON binds only at a draw, so a clear leaves its registers unchanged. */
static void st_draw_work(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args) {
    (void)args;
    ST_CALL(c, &p->draw_arrays, host, cb, ST_DRAW_MODE, ST_DRAW_FIRST, ST_DRAW_COUNT);
}

/* The mutation of one case. The rule is the one that the case's table row names. */
enum {
    ST_MUT_NONE,   /* the argument has no value to change (a NULL pointer) */
    ST_MUT_FLOAT,  /* the first float argument + 1.0f */
    ST_MUT_INT,    /* the first integer value argument XOR 1 */
    ST_MUT_ARRAY,  /* the first float of an array argument + 1.0f */
    ST_MUT_SETTER, /* the first setter value of an object argument XOR 1 */
};

/* Runs the calls of one side, then the calls of the other side, one recording each, and compares the two records.
   A script returns 1, or 0 when a function of its case was not resolved: the case is then skipped, not failed. */
typedef int (*StScript)(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                        uint64_t param, unsigned rule);

/* Expands the pool records of a decoded recording (decision A, part C). The decoder stays pure, so the CPU thread
   reads the pool object and calls the virtual function that gives its base, then writes the words that the library
   interpreter writes at submission. 1 when every pool item was expanded, 0 when the case must be skipped. */
static int st_expand_pools(CPU* c, NvnStRun* run, StCmdJob* job, NvnStRange* ranges, unsigned n, const char* name,
                           const char* text) {
    unsigned pools = 0;
    for (unsigned i = 0; i < n; i++) {
        NvnStRange* const r = &ranges[i];
        if (!r->pool_op) continue;
        if (pools >= ST_POOL_ITEMS_MAX) {
            run->skipped++;
            LOG("selftest: %s %s skipped, more than %u pool records\n", name, text, ST_POOL_ITEMS_MAX);
            return 0;
        }
        const uint64_t pool = r->pool;
        if (!nvn_st_arena_contains(&run->arena, pool, ST_POOL_READ_BYTES)) {
            run->skipped++;
            LOG("selftest: %s %s skipped, pool record op %u at %llx is outside the arena\n", name, text, r->pool_op,
                (unsigned long long)pool);
            return 0;
        }
        const uint64_t mp = gr64(pool);
        /* The selftest makes its memory pools in the arena: another address is not a pool of this run. */
        if (!nvn_st_arena_contains(&run->arena, mp, sizeof(uint64_t))) {
            run->skipped++;
            LOG("selftest: %s %s skipped, pool record op %u at %llx has memory pool %llx outside the arena\n", name,
                text, r->pool_op, (unsigned long long)pool, (unsigned long long)mp);
            return 0;
        }
        const uint64_t vt = gr64(mp);
        if (!vt) {
            run->skipped++;
            LOG("selftest: %s %s skipped, pool record op %u at %llx has no vtable\n", name, text, r->pool_op,
                (unsigned long long)pool);
            return 0;
        }
        const uint64_t fn = gr64(vt + ST_POOL_VT_FN_OFFSET);
        if (!fn) {
            run->skipped++;
            LOG("selftest: %s %s skipped, pool record op %u at %llx has no base function\n", name, text, r->pool_op,
                (unsigned long long)pool);
            return 0;
        }
        const NvnStCallee callee = {snx_lookup(c, fn), fn};
        const uint64_t base = nvn_st_call(c, &callee, &(NvnStCall){.x = {mp}}) + gr64(pool + ST_POOL_BASE_OFFSET);
        const uint64_t at = job->pool_scratch + (uint64_t)pools * ST_POOL_ITEM_WORDS * 4u;
        const uint32_t words = nvn_st_pool_words(r->pool_op, base, gr32(pool + ST_POOL_SIZE_OFFSET), (uint32_t*)GP(at),
                                                 ST_POOL_ITEM_WORDS);
        if (!words) {
            run->skipped++;
            LOG("selftest: %s %s skipped, pool record op %u at %llx did not expand\n", name, text, r->pool_op,
                (unsigned long long)pool);
            return 0;
        }
        r->cpu_start = at;
        r->words = words;
        r->pool_op = 0;
        pools++;
    }
    return 1;
}

static void st_case(CPU* c, NvnStRun* run, StCmdProcs* p, StCmdJob* job, uint64_t lib_cb, uint64_t host_cb,
                    uint64_t lib_pool, uint64_t host_pool, uint64_t args, const char* name, const char* text,
                    StScript script, const NvnStProc* fn, uint64_t param, unsigned rule, int expect_noop,
                    int* stop, const uint32_t* ignore, unsigned n_ignore) {
    st_refill(c, p, lib_cb, host_cb, lib_pool, host_pool);
    /* Each EndRecording uses control memory. When it runs out, the library calls the out-of-memory callback,
       which the selftest does not set, and aborts (sdk d8243b8). The records of the last case are done. */
    ST_CALL(c, &p->add_control_memory, 0, lib_cb, job->lib_control, ST_CONTROL_BYTES);
    ST_CALL(c, &p->add_control_memory, 1, host_cb, job->host_control, ST_CONTROL_BYTES);
    ST_CALL(c, &p->begin, 0, lib_cb);
    const int lib_script = script(c, p, fn, 0, lib_cb, args, param, rule);
    const uint64_t lib_handle = ST_CALL(c, &p->end, 0, lib_cb);
    ST_CALL(c, &p->begin, 1, host_cb);
    const int host_script = script(c, p, fn, 1, host_cb, args, param, rule);
    const uint64_t host_handle = ST_CALL(c, &p->end, 1, host_cb);
    if (!lib_script || !host_script) {
        run->skipped++;
        LOG("selftest: %s skipped, not resolved\n", name);
        return;
    }

    unsigned n = 0;
    NvnStDecodeFault fault;
    StReadCtx reader = {&run->arena};
    const int status =
        nvn_st_decode_recording(lib_handle, st_guest_read, (void*)&reader, job->ranges, ST_MAX_RANGES, &n, &fault);
    if (status != NVN_ST_DEC_OK) {
        st_skip(run, name, text, status, &fault);
        return;
    }
    if (!st_expand_pools(c, run, job, job->ranges, n, name, text)) return;
    for (unsigned i = 0; i < n; i++) {
        const uint64_t bytes = (uint64_t)job->ranges[i].words * 4u;
        if (nvn_st_arena_contains(&run->arena, job->ranges[i].cpu_start, bytes)) continue;
        run->skipped++;
        LOG("selftest: %s %s skipped, record range %u at %llx is outside the arena\n", name, text, i,
            (unsigned long long)job->ranges[i].cpu_start);
        return;
    }

    job->name = name;
    job->n_ranges = n;
    job->host_handle = host_handle;
    job->ignore = ignore;
    job->n_ignore = n_ignore;
    job->window_origin_flip = gr32(nvn_st_device() + NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET) == 1;
    job->ref = &job->lib_rec;
    job->got = &job->host_rec;
    memset(&job->lib_rec, 0, sizeof job->lib_rec);
    memset(&job->host_rec, 0, sizeof job->host_rec);
    job->status = 0;
    job->done = 0;
    const int gpu = st_run_gpu(job);
    if (gpu > 0) {
        /* The GPU thread may still read the job, the ranges and the records: leak them and stop the S/Y cases. */
        run->cases++;
        run->failed++;
        *stop = 1;
        LOG("selftest: %s %s failed, the GPU call did not finish within %u ms\n", name, text, ST_GPU_WAIT_MS);
        return;
    }
    if (gpu < 0) {
        run->skipped++;
        LOG("selftest: %s %s skipped, a side could not run the stream\n", name, text);
        return;
    }
    NvnStDiff diff;
    nvn_st_compare(&job->lib_rec, &job->host_rec, &diff);
    if (expect_noop)
        nvn_st_report_noop(run, name, text, &job->lib_rec, &diff);
    else
        nvn_st_report(run, name, text, &job->lib_rec, &diff);
}

static int st_script_viewports(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                               uint64_t args, uint64_t param, unsigned rule) {
    (void)fn;
    (void)param;
    (void)rule;
    ST_CALL(c, &p->set_viewports, host, cb, 0, ST_VIEWPORTS, args + (host ? ST_ARG_VP_HOST : ST_ARG_VP_LIB));
    ST_CALL(c, &p->set_scissors, host, cb, 0, ST_SCISSORS, args + (host ? ST_ARG_SC_HOST : ST_ARG_SC_LIB));
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The library bits are in the low word of param, the host's (a mutation's) in the high word. */
static int st_script_barrier(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                             uint64_t args, uint64_t param, unsigned rule) {
    (void)fn;
    (void)rule;
    const uint32_t bits = host ? (uint32_t)(param >> 32) : (uint32_t)param;
    ST_CALL(c, &p->barrier, host, cb, bits);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static void st_fill_s_args(uint64_t args, int mutate) {
    memcpy(GP(args + ST_ARG_VP_LIB), st_viewports, sizeof st_viewports);
    memcpy(GP(args + ST_ARG_VP_HOST), st_viewports, sizeof st_viewports);
    memcpy(GP(args + ST_ARG_SC_LIB), st_scissors, sizeof st_scissors);
    memcpy(GP(args + ST_ARG_SC_HOST), st_scissors, sizeof st_scissors);
    if (mutate) {
        float x;
        memcpy(&x, GP(args + ST_ARG_VP_HOST + ST_MUTATE_VIEWPORT_WORD * sizeof(float)), sizeof x);
        x += ST_MUTATE_VIEWPORT_ADD;
        memcpy(GP(args + ST_ARG_VP_HOST + ST_MUTATE_VIEWPORT_WORD * sizeof(float)), &x, sizeof x);
    }
}

/* One float argument. param holds its bit pattern. */
static int st_script_float_arg(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                               uint64_t args, uint64_t param, unsigned rule) {
    uint32_t bits = (uint32_t)param;
    if (host && rule == ST_MUT_FLOAT) bits = st_f2b(st_b2f(bits) + 1.0f);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb}, .s = {bits}, .n_s = 1});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* One integer argument. param holds its value. */
static int st_script_int_arg(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    uint32_t value = (uint32_t)param;
    if (host && rule == ST_MUT_INT) value ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, value}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetStencilMask, SetStencilRef and SetStencilValueMask: face in the low word of param, value in the high word. */
static int st_script_stencil(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    uint32_t face = (uint32_t)param;
    const uint32_t value = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_INT) face ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, face, value}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_blend_color(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    float v[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    if (host && rule == ST_MUT_ARRAY) v[0] += 1.0f;
    const uint64_t addr = args + (host ? ST_ARG_ARRAY_HOST : ST_ARG_ARRAY_LIB);
    memcpy(GP(addr), v, sizeof v);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, addr}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetDepthRange: the near value in the low word of param, the far value in the high word (bit patterns). */
static int st_script_depth_range(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    uint32_t near_bits = (uint32_t)param;
    const uint32_t far_bits = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_FLOAT) near_bits = st_f2b(st_b2f(near_bits) + 1.0f);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb}, .s = {near_bits, far_bits}, .n_s = 2});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_depth_bounds(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStCall a = {.x = {cb, 1}, .s = {st_f2b(0.25f), st_f2b(0.75f)}, .n_s = 2};
    if (host && rule == ST_MUT_FLOAT) a.s[0] = st_f2b(0.25f + 1.0f);
    st_call_proc(c, fn, host, &a);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetDepthRanges: first 0, count 2, and two pairs of floats in the array region. */
static int st_script_depth_ranges(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    float v[4] = {0.1f, 0.9f, 0.2f, 0.8f};
    if (host && rule == ST_MUT_ARRAY) v[0] += 1.0f;
    const uint64_t addr = args + (host ? ST_ARG_ARRAY_HOST : ST_ARG_ARRAY_LIB);
    memcpy(GP(addr), v, sizeof v);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0, 2, addr}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_polygon_offset(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                    uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStCall a = {.x = {cb}, .s = {st_f2b(1.0f), st_f2b(2.0f), st_f2b(0.5f)}, .n_s = 3};
    if (host && rule == ST_MUT_FLOAT) a.s[0] = st_f2b(1.0f + 1.0f);
    st_call_proc(c, fn, host, &a);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_viewport(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                              uint64_t param, unsigned rule) {
    (void)param;
    uint32_t x = 16u;
    if (host && rule == ST_MUT_INT) x ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, x, 32u, 640u, 360u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_scissor(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    (void)param;
    uint32_t x = 8u;
    if (host && rule == ST_MUT_INT) x ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, x, 8u, 100u, 50u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* A Bind case: each side builds its own state object, then binds it. The host object layout may differ from the
   library's, so the two sides never share an object. param is the NVN_ST_STATE_* kind. `work` ends the script. */
static int st_script_bind_state_work(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule, StWork work) {
    const uint64_t obj = args + (host ? ST_ARG_STATE_HOST : ST_ARG_STATE_LIB);
    if (!nvn_st_state_build(c, (unsigned)param, host, obj, host && rule == ST_MUT_SETTER ? 1u : 0u)) return 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, obj}});
    work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_state(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    return st_script_bind_state_work(c, p, fn, host, cb, args, param, rule, st_clear_work);
}

/* The host sends the BLEND, CHANNEL_MASK and POLYGON binds only at a draw (replay_host_draw in nvn_native.c), so a
   clear would leave the host registers unchanged and the case would fail. */
static int st_script_bind_state_draw(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    return st_script_bind_state_work(c, p, fn, host, cb, args, param, rule, st_draw_work);
}

/* SetStencilCullCriteria: func 8, ref 0, mask 0xFF. */
static int st_script_stencil_cull(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t func = 8u;
    if (host && rule == ST_MUT_INT) func ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, func, 0u, 0xFFu}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetColorReductionThresholds: format class 0, then the values 1 and 2. */
static int st_script_thresholds(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t cls = 0u;
    if (host && rule == ST_MUT_INT) cls ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, cls, 1u, 2u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* Two integer arguments: the first in the low word of param, the second in the high word. */
static int st_script_two_ints(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                              uint64_t param, unsigned rule) {
    uint32_t first = (uint32_t)param;
    const uint32_t second = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_INT) first ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, first, second}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* BindCoverageModulationTable: the NULL (disable) form. A table is a refusal of the host and out of scope, so a
   mutation leaves the argument NULL. */
static int st_script_coverage_table(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                    uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The per-side objects of the d2 resource cases and of the d3 texture cases, read by their scripts. copy_handle is the
   EndRecording handle of each side's CopyCommands source list; copy_ok is 0 when that list was not made. */
typedef struct {
    uint64_t sampler_pool[2], texture_pool[2];
    uint64_t texture_pool_gva[2];
    uint64_t texture[2][2];
    uint64_t copy_handle[2];
    int copy_ok;
} StBindCtx;
static const StBindCtx* st_bind_ctx;

static int st_script_render_targets(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                    uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    const uint64_t textures = args + (host ? ST_ARG_RT_HOST : ST_ARG_RT_LIB);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_RT_COUNT, textures, 0, 0, 0}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_sampler_pool(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    const uint64_t pool = st_bind_ctx ? st_bind_ctx->sampler_pool[host] : 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, pool}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_texture_pool(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    const uint64_t pool = st_bind_ctx ? st_bind_ctx->texture_pool[host] : 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, pool}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

/* BindVertexAttribState: count 2, and each element built with the state setters of its side. The host handler reads a
   record of each element, so raw words would leave that record zero. */
static int st_script_bind_vertex_attrib(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                        uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStProc defaults = {0}, format = {0}, stream_index = {0};
    if (!nvn_st_proc_find(&defaults, "nvnVertexAttribStateSetDefaults") ||
        !nvn_st_proc_find(&format, "nvnVertexAttribStateSetFormat") ||
        !nvn_st_proc_find(&stream_index, "nvnVertexAttribStateSetStreamIndex"))
        return 0;
    static const uint32_t formats[ST_ATTRIB_ELEMENTS] = {0x25u, 0x26u};
    static const uint32_t offsets[ST_ATTRIB_ELEMENTS] = {0u, 4u};
    static const uint32_t streams[ST_ATTRIB_ELEMENTS] = {0u, 1u};
    const uint64_t at = args + (host ? ST_ARG_ATTRIB_HOST : ST_ARG_ATTRIB_LIB);
    for (unsigned i = 0; i < ST_ATTRIB_ELEMENTS; i++) {
        const uint64_t elem = at + (uint64_t)i * ST_ATTRIB_ELEM_BYTES;
        const uint32_t stream = streams[i] ^ (host && rule == ST_MUT_INT && i == 0 ? 1u : 0u);
        nvn_st_call(c, host ? &defaults.host : &defaults.lib, &(NvnStCall){.x = {elem}});
        nvn_st_call(c, host ? &format.host : &format.lib, &(NvnStCall){.x = {elem, formats[i], offsets[i]}});
        nvn_st_call(c, host ? &stream_index.host : &stream_index.lib, &(NvnStCall){.x = {elem, stream}});
    }
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_ATTRIB_ELEMENTS, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

/* BindVertexStreamState: count 2, and each element built with the state setters of its side. */
static int st_script_bind_vertex_stream(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                        uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStProc defaults = {0}, stride = {0}, divisor = {0};
    if (!nvn_st_proc_find(&defaults, "nvnVertexStreamStateSetDefaults") ||
        !nvn_st_proc_find(&stride, "nvnVertexStreamStateSetStride") ||
        !nvn_st_proc_find(&divisor, "nvnVertexStreamStateSetDivisor"))
        return 0;
    static const uint32_t strides[ST_STREAM_ELEMENTS] = {0x10u, 0x20u};
    static const uint32_t divisors[ST_STREAM_ELEMENTS] = {0u, 1u};
    const uint64_t at = args + (host ? ST_ARG_STREAM_HOST : ST_ARG_STREAM_LIB);
    for (unsigned i = 0; i < ST_STREAM_ELEMENTS; i++) {
        const uint64_t elem = at + (uint64_t)i * ST_STREAM_ELEM_BYTES;
        const uint32_t value = strides[i] ^ (host && rule == ST_MUT_INT && i == 0 ? 1u : 0u);
        nvn_st_call(c, host ? &defaults.host : &defaults.lib, &(NvnStCall){.x = {elem}});
        nvn_st_call(c, host ? &stride.host : &stride.lib, &(NvnStCall){.x = {elem, value}});
        nvn_st_call(c, host ? &divisor.host : &divisor.lib, &(NvnStCall){.x = {elem, divisors[i]}});
    }
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_STREAM_ELEMENTS, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_vertex_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                        uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t index = 1u;
    if (host && rule == ST_MUT_INT) index ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, index, 0x12340000ull, 0x1000ull}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_vertex_buffers(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                         uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint64_t entries[4] = {0x12340000ull, 0x1000ull, 0x22340000ull, 0x2000ull};
    if (host && rule == ST_MUT_INT) entries[0] ^= 0x1000u;
    const uint64_t at = args + (host ? ST_ARG_VBUF_HOST : ST_ARG_VBUF_LIB);
    memcpy(GP(at), entries, sizeof entries);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0u, 2u, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_uniform_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                         uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t slot = 1u;
    if (host && rule == ST_MUT_INT) slot ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0u, slot, 0x100002000ull, 0x180ull}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_uniform_buffers(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                          uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t first = 0u;
    uint64_t entries[4] = {0x100002000ull, 0x200ull, 0x100004000ull, 0x100ull};
    if (host && rule == ST_MUT_INT) first ^= 1u;
    const uint64_t at = args + (host ? ST_ARG_UBUF_HOST : ST_ARG_UBUF_LIB);
    memcpy(GP(at), entries, sizeof entries);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0u, first, 2u, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_storage_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                         uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t slot = 0u;
    if (host && rule == ST_MUT_INT) slot ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 1u, slot, 0x100003000ull, 0x100ull}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_storage_buffers(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                          uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t first = 0u;
    uint64_t entries[4] = {0x100003000ull, 0x100ull, 0x100004000ull, 0x80ull};
    if (host && rule == ST_MUT_INT) first ^= 1u;
    const uint64_t at = args + (host ? ST_ARG_SBUF_HOST : ST_ARG_SBUF_LIB);
    memcpy(GP(at), entries, sizeof entries);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0u, first, 2u, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_texture(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t slot = 1u;
    if (host && rule == ST_MUT_INT) slot ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 1u, slot, 0x100000005ull}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_textures(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                   uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t first = 1u;
    uint64_t handles[2] = {0x100000005ull, 0x100000007ull};
    if (host && rule == ST_MUT_INT) first ^= 1u;
    const uint64_t at = args + (host ? ST_ARG_TEX_HOST : ST_ARG_TEX_LIB);
    memcpy(GP(at), handles, sizeof handles);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 2u, first, 2u, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_image(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t slot = 1u;
    if (host && rule == ST_MUT_INT) slot ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 1u, slot, 0x200000003ull}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

static int st_script_bind_images(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t first = 1u;
    uint64_t handles[2] = {0x200000003ull, 0x200000007ull};
    if (host && rule == ST_MUT_INT) first ^= 1u;
    const uint64_t at = args + (host ? ST_ARG_IMG_HOST : ST_ARG_IMG_LIB);
    memcpy(GP(at), handles, sizeof handles);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 2u, first, 2u, at}});
    st_draw_work(c, p, host, cb, args);
    return 1;
}

/* The registers that only hold a per-side pool or texture address. */
static const uint32_t st_ignore_render_targets[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x200u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x201u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x210u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x211u)};
static const uint32_t st_ignore_sampler_pool[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x557u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x558u),
    ST_IGNORE_REG(NVN_ST_ENGINE_RCOMP, 0x557u), ST_IGNORE_REG(NVN_ST_ENGINE_RCOMP, 0x558u)};
static const uint32_t st_ignore_texture_pool[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x55Du), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x55Eu),
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x8E4u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x8E5u),
    ST_IGNORE_REG(NVN_ST_ENGINE_RCOMP, 0x55Du), ST_IGNORE_REG(NVN_ST_ENGINE_RCOMP, 0x55Eu)};

/* The 3D clear of a texture writes the per-layer address into render target 0. */
static const uint32_t st_ignore_clear_texture[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x200u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x201u)};

/* A 2D copy or downsample reads the per-side texture addresses from FERMI2D_SRC and FERMI2D_DST. */
static const uint32_t st_ignore_2d_addresses[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x088u), ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x089u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x094u), ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x095u)};

/* CopyBufferToTexture (DMA route) puts the per-side texture address in the DMA output address. */
static const uint32_t st_ignore_dma_out[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_RDMA, 0x102u), ST_IGNORE_REG(NVN_ST_ENGINE_RDMA, 0x103u)};

/* CopyTextureToBuffer puts the per-side texture address in the DMA input address. */
static const uint32_t st_ignore_dma_in[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_RDMA, 0x100u), ST_IGNORE_REG(NVN_ST_ENGINE_RDMA, 0x101u)};

/* TiledDownsample: the host has no driver program and uses the 2D engine, so the 3D words of the library and the 2D
   addresses of the host are a known difference (user decision 2026-11-12, docs/graphics/nvn-unobserved.md). */
static const uint32_t st_ignore_tiled_downsample[] = {
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x049u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x811u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x813u), ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x851u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R3D, 0x853u), ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x088u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x089u), ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x094u),
    ST_IGNORE_REG(NVN_ST_ENGINE_R2D, 0x095u)};

#define ST_IGNORE_N(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))
_Static_assert(ST_IGNORE_N(st_ignore_render_targets) <= ST_IGNORE_MAX, "the render target ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_sampler_pool) <= ST_IGNORE_MAX, "the sampler pool ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_texture_pool) <= ST_IGNORE_MAX, "the texture pool ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_clear_texture) <= ST_IGNORE_MAX, "the clear texture ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_2d_addresses) <= ST_IGNORE_MAX, "the 2D address ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_dma_out) <= ST_IGNORE_MAX, "the DMA output ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_dma_in) <= ST_IGNORE_MAX, "the DMA input ignore list");
_Static_assert(ST_IGNORE_N(st_ignore_tiled_downsample) <= ST_IGNORE_MAX, "the TiledDownsample ignore list");

/* One row of the d2 and d3 case tables. needs_res marks a case that uses the per-side descriptor pools and textures.
   expect_noop marks a host handler that is a logged no-op by design: a difference is expected, not a fail. */
typedef struct {
    const char* name;
    const char* text;
    StScript script;
    unsigned rule;
    const uint32_t* ignore;
    unsigned n_ignore;
    int needs_res;
    int expect_noop;
} StCaseRow;

/* The 15 d2 cases: the binds and the resources. All compare at a draw, except that a pool and a render target write
   their methods at the op. needs_res marks the cases that use the per-side descriptor pools and textures. */
static const StCaseRow st_bind_cases[] = {
    {"nvnCommandBufferSetRenderTargets", "count=2 32x32 fmt=0x25", st_script_render_targets, ST_MUT_NONE,
     st_ignore_render_targets, ST_IGNORE_N(st_ignore_render_targets), 1, 0},
    {"nvnCommandBufferSetSamplerPool", "pool=512 entries", st_script_sampler_pool, ST_MUT_NONE,
     st_ignore_sampler_pool, ST_IGNORE_N(st_ignore_sampler_pool), 1, 0},
    {"nvnCommandBufferSetTexturePool", "pool=512 entries", st_script_texture_pool, ST_MUT_NONE,
     st_ignore_texture_pool, ST_IGNORE_N(st_ignore_texture_pool), 1, 0},
    {"nvnCommandBufferBindVertexAttribState", "count=2 fmt=0x25,0x26 off=0,4 stream=0,1",
     st_script_bind_vertex_attrib, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindVertexStreamState", "count=2 stride=0x10,0x20 divisor=0,1",
     st_script_bind_vertex_stream, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindVertexBuffer", "index=1 addr=0x12340000 size=0x1000", st_script_bind_vertex_buffer,
     ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindVertexBuffers", "first=0 count=2 addr=0x12340000,0x22340000",
     st_script_bind_vertex_buffers, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindUniformBuffer", "stage=0 slot=1 addr=0x100002000 size=0x180",
     st_script_bind_uniform_buffer, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindUniformBuffers", "stage=0 first=0 count=2", st_script_bind_uniform_buffers, ST_MUT_INT,
     NULL, 0, 0, 0},
    {"nvnCommandBufferBindStorageBuffer", "stage=1 slot=0 addr=0x100003000 size=0x100",
     st_script_bind_storage_buffer, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindStorageBuffers", "stage=0 first=0 count=2", st_script_bind_storage_buffers, ST_MUT_INT,
     NULL, 0, 0, 0},
    {"nvnCommandBufferBindTexture", "stage=1 slot=1 handle=0x100000005", st_script_bind_texture, ST_MUT_INT, NULL, 0,
     0, 0},
    {"nvnCommandBufferBindTextures", "stage=2 first=1 count=2 handles=0x100000005,0x100000007",
     st_script_bind_textures, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferBindImage", "stage=1 slot=1 handle=0x200000003", st_script_bind_image, ST_MUT_INT, NULL, 0, 0,
     0},
    {"nvnCommandBufferBindImages", "stage=2 first=1 count=2 handles=0x200000003,0x200000007",
     st_script_bind_images, ST_MUT_INT, NULL, 0, 0, 0},
};

_Static_assert(sizeof st_bind_cases / sizeof st_bind_cases[0] == ST_BIND_CASES, "the bind case count");

/* ---- the d3 cases: draws, clears, copies, downsamples and the uniform upload ---- */

static uint64_t st_d3_texture(int host, unsigned slot) {
    if (!st_bind_ctx || slot > 1u) return 0;
    return st_bind_ctx->texture[host][slot];
}

static uint64_t st_d3_region(uint64_t args, int host) {
    return args + (host ? ST_ARG_REGION_HOST : ST_ARG_REGION_LIB);
}

static void st_d3_fill_region(uint64_t at) {
    static const uint32_t region[6] = {ST_D3_REGION_X,      ST_D3_REGION_Y,      ST_D3_REGION_LAYER,
                                       ST_D3_REGION_WIDTH,  ST_D3_REGION_HEIGHT, ST_D3_REGION_LAYERS};
    memcpy(GP(at), region, sizeof region);
}

/* A DMA copy and the 2D route of CopyBufferToTexture do not read the filter bit (flags bit 0) on either side, so the
   mutation of such a copy makes the host's region one texel narrower (a wider one is outside texture A and refused). */
static void st_d3_mutate_region_width(uint64_t region, int host, unsigned rule) {
    if (host && rule == ST_MUT_INT) gw32(region + ST_D3_REGION_WIDTH_OFFSET, ST_D3_REGION_WIDTH - 1u);
}

static void st_d3_fill_color(uint64_t at, const uint32_t color[4]) {
    memcpy(GP(at), color, 4u * sizeof color[0]);
}

static void st_d3_fill_data(uint64_t at) {
    uint32_t* const data = (uint32_t*)GP(at);
    for (unsigned i = 0; i < ST_D3_DATA_WORDS; i++) data[i] = 0x1000u + i;
}

/* nvn_st_call passes x0..x5. The instance draws and CopyTextureToTexture also use x6 and x7; the caller's values are
   restored after the call. */
static void st_call_x6x7(CPU* c, const NvnStProc* p, int host, const NvnStCall* a, uint64_t x6, uint64_t x7) {
    const uint64_t saved6 = c->x[6], saved7 = c->x[7];
    c->x[6] = x6;
    c->x[7] = x7;
    nvn_st_call(c, host ? &p->host : &p->lib, a);
    c->x[6] = saved6;
    c->x[7] = saved7;
}

static const uint32_t st_d3_color_float[4] = {0x3F800000u, 0u, 0u, 0x3F800000u}; /* 1.0f, 0, 0, 1.0f */
static const uint32_t st_d3_color_int[4] = {0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u};

static int st_draw_arrays(CPU* c, const NvnStProc* fn, int host, uint64_t cb, unsigned rule) {
    uint32_t mode = ST_D3_DRAW_MODE;
    if (host && rule == ST_MUT_INT) mode ^= 1u;
    st_call_proc(c, fn, host,
                 &(NvnStCall){.x = {cb, mode, ST_D3_DRAW_FIRST, ST_D3_DRAW_COUNT}});
    return 1;
}

static int st_script_draw_arrays(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_arrays(c, fn, host, cb, rule);
}

static int st_draw_arrays_instanced(CPU* c, const NvnStProc* fn, int host, uint64_t cb, unsigned rule,
                                    uint32_t base_instance) {
    uint32_t mode = ST_D3_DRAW_MODE;
    if (host && rule == ST_MUT_INT) mode ^= 1u;
    st_call_proc(c, fn, host,
                 &(NvnStCall){.x = {cb, mode, ST_D3_DRAW_FIRST, ST_D3_DRAW_COUNT, base_instance, ST_D3_INSTANCES}});
    return 1;
}

static int st_script_draw_arrays_instanced0(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                            uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_arrays_instanced(c, fn, host, cb, rule, 0u);
}

static int st_script_draw_arrays_instanced1(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                            uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_arrays_instanced(c, fn, host, cb, rule, 1u);
}

static int st_draw_indexed(CPU* c, const NvnStProc* fn, int host, uint64_t cb, unsigned rule, uint32_t base_vertex,
                           uint32_t base_instance, uint32_t instances) {
    uint32_t mode = ST_D3_DRAW_MODE;
    if (host && rule == ST_MUT_INT) mode ^= 1u;
    NvnStCall a = {.x = {cb, mode, ST_D3_INDEX_TYPE, ST_D3_DRAW_COUNT, ST_D3_INDEX_ADDRESS, base_vertex}};
    if (instances)
        st_call_x6x7(c, fn, host, &a, base_instance, instances);
    else
        st_call_proc(c, fn, host, &a);
    return 1;
}

static int st_script_draw_elements(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                   uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_indexed(c, fn, host, cb, rule, 0u, 0u, 0u);
}

static int st_script_draw_elements_base_vertex0(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host,
                                                uint64_t cb, uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_indexed(c, fn, host, cb, rule, 0u, 0u, 0u);
}

static int st_script_draw_elements_base_vertex1(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host,
                                                uint64_t cb, uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_indexed(c, fn, host, cb, rule, 1u, 0u, 0u);
}

static int st_script_draw_elements_instanced00(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host,
                                               uint64_t cb, uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_indexed(c, fn, host, cb, rule, 0u, 0u, ST_D3_INSTANCES);
}

static int st_script_draw_elements_instanced11(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host,
                                               uint64_t cb, uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_draw_indexed(c, fn, host, cb, rule, 1u, 1u, ST_D3_INSTANCES);
}

static int st_script_clear_color_int(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)param;
    uint32_t index = 0u;
    if (host && rule == ST_MUT_INT) index ^= 1u;
    const uint64_t color = args + (host ? ST_ARG_TEXCOLOR_HOST : ST_ARG_TEXCOLOR_LIB);
    st_d3_fill_color(color, st_d3_color_int);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, index, color, ST_D3_MASK}});
    return 1;
}

static int st_clear_depth(CPU* c, const NvnStProc* fn, int host, uint64_t cb, unsigned rule, uint32_t stencil,
                          uint32_t mask) {
    uint32_t enable = ST_D3_DEPTH_ENABLE;
    if (host && rule == ST_MUT_INT) enable ^= 1u;
    NvnStCall a = {.x = {cb, enable, stencil, mask}, .s = {st_f2b(ST_D3_DEPTH)}, .n_s = 1};
    st_call_proc(c, fn, host, &a);
    return 1;
}

static int st_script_clear_depth_stencil(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                         uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_clear_depth(c, fn, host, cb, rule, ST_D3_STENCIL, ST_D3_STENCIL_MASK);
}

static int st_script_clear_depth_only(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                      uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)args;
    (void)param;
    return st_clear_depth(c, fn, host, cb, rule, 0u, 0u);
}

static int st_clear_texture(CPU* c, const NvnStProc* fn, int host, uint64_t cb, uint64_t args, unsigned rule,
                            const uint32_t color[4]) {
    const uint64_t tex = st_d3_texture(host, 0);
    if (!tex) return 0;
    const uint64_t region = st_d3_region(args, host);
    const uint64_t color_at = args + (host ? ST_ARG_TEXCOLOR_HOST : ST_ARG_TEXCOLOR_LIB);
    st_d3_fill_region(region);
    st_d3_fill_color(color_at, color);
    uint32_t mask = ST_D3_MASK;
    if (host && rule == ST_MUT_INT) mask ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, tex, 0u, region, color_at, mask}});
    return 1;
}

static int st_script_clear_texture_f(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)param;
    return st_clear_texture(c, fn, host, cb, args, rule, st_d3_color_float);
}

static int st_script_clear_texture_i(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    (void)p;
    (void)param;
    return st_clear_texture(c, fn, host, cb, args, rule, st_d3_color_int);
}

static int st_script_clear_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t value = ST_D3_CLEAR_VALUE;
    if (host && rule == ST_MUT_INT) value ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D3_BUFFER_A, ST_D3_COPY_SIZE, value}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_discard_color(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                   uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t index = ST_D3_DISCARD_INDEX;
    if (host && rule == ST_MUT_INT) index ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, index}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* DiscardDepthStencil, ResolveDepthBuffer and PopDebugGroup take the command buffer only and then compare at a clear. */
static int st_script_cb_only(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_copy_buffer_to_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                           uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t size = ST_D3_COPY_SIZE;
    if (host && rule == ST_MUT_INT) size ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D3_BUFFER_A, ST_D3_BUFFER_B, size}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_copy_buffer_to_texture(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, unsigned rule, uint32_t flags) {
    const uint64_t tex = st_d3_texture(host, 0);
    if (!tex) return 0;
    const uint64_t region = st_d3_region(args, host);
    st_d3_fill_region(region);
    st_d3_mutate_region_width(region, host, rule);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D3_BUFFER_A, tex, 0u, region, flags}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_copy_buffer_to_texture0(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                             uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    return st_copy_buffer_to_texture(c, p, fn, host, cb, args, rule, 0u);
}

static int st_script_copy_buffer_to_texture2(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                             uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    return st_copy_buffer_to_texture(c, p, fn, host, cb, args, rule, 2u);
}

static int st_script_copy_texture_to_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                            uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    const uint64_t tex = st_d3_texture(host, 0);
    if (!tex) return 0;
    const uint64_t region = st_d3_region(args, host);
    st_d3_fill_region(region);
    st_d3_mutate_region_width(region, host, rule);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, tex, 0u, region, ST_D3_BUFFER_B, 0u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_copy_texture_to_texture(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host,
                                             uint64_t cb, uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    const uint64_t src = st_d3_texture(host, 0), dst = st_d3_texture(host, 1);
    if (!src || !dst) return 0;
    const uint64_t region = st_d3_region(args, host);
    st_d3_fill_region(region);
    uint32_t flags = 0u;
    if (host && rule == ST_MUT_INT) flags ^= 1u;
    NvnStCall a = {.x = {cb, src, 0u, region, dst, 0u}};
    st_call_x6x7(c, fn, host, &a, region, flags);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_copy_commands(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                   uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    if (!st_bind_ctx || !st_bind_ctx->copy_ok) return 0;
    const uint64_t at = args + (host ? ST_ARG_HANDLE_HOST : ST_ARG_HANDLE_LIB);
    gw64(at, st_bind_ctx->copy_handle[host]);
    uint32_t count = ST_D3_COPY_COMMANDS;
    if (host && rule == ST_MUT_INT) count ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, count, at}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_downsample(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    const uint64_t src = st_d3_texture(host, 0), dst = st_d3_texture(host, 1);
    if (!src || !dst) return 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, src, dst}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_downsample_view(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    const uint64_t src = st_d3_texture(host, 0), dst = st_d3_texture(host, 1);
    if (!src || !dst) return 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, src, 0u, dst, 0u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_update_uniform_buffer(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                           uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    const uint64_t data = args + (host ? ST_ARG_DATA_HOST : ST_ARG_DATA_LIB);
    st_d3_fill_data(data);
    uint32_t size = ST_D3_UNIFORM_SIZE;
    /* The host rounds the size up to 256 bytes, so only a step of 0x100 changes the emitted words. */
    if (host && rule == ST_MUT_INT) size += 0x100u;
    st_call_proc(c, fn, host,
                 &(NvnStCall){.x = {cb, ST_D3_UNIFORM_ADDRESS, size, ST_D3_UNIFORM_OFFSET, ST_D3_UNIFORM_BYTES,
                                   data}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The 30 d3 cases: the draws, the clears, the copies, the downsamples and the uniform upload. The own work column of
   the brief is the script: a draw or a clear is its own work, every other case compares at a clear after the call.
   needs_res marks the cases that read the d2 textures. TiledDownsample is skipped today: its library control record
   op 0x15 is refused by the decoder (nvn_selftest_dec.h). */
static const StCaseRow st_d3_cases[] = {
    {"nvnCommandBufferDrawArrays", "mode=4 first=1 count=3", st_script_draw_arrays, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDrawArraysInstanced", "baseInstance=0 instances=2", st_script_draw_arrays_instanced0,
     ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDrawArraysInstanced", "baseInstance=1 instances=2", st_script_draw_arrays_instanced1,
     ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDrawElements", "mode=4 type=1 count=3 addr=0x100005000", st_script_draw_elements, ST_MUT_INT,
     NULL, 0, 0, 0},
    {"nvnCommandBufferDrawElementsBaseVertex", "baseVertex=0", st_script_draw_elements_base_vertex0, ST_MUT_INT, NULL,
     0, 0, 0},
    {"nvnCommandBufferDrawElementsBaseVertex", "baseVertex=1", st_script_draw_elements_base_vertex1, ST_MUT_INT, NULL,
     0, 0, 0},
    {"nvnCommandBufferDrawElementsInstanced", "baseVertex=0 baseInstance=0 instances=2",
     st_script_draw_elements_instanced00, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDrawElementsInstanced", "baseVertex=1 baseInstance=1 instances=2",
     st_script_draw_elements_instanced11, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferClearColori", "index=0 color=0x11223344,0x55667788,0x99aabbcc,0xddeeff00 mask=0xf",
     st_script_clear_color_int, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferClearColorui", "index=0 color=0x11223344,0x55667788,0x99aabbcc,0xddeeff00 mask=0xf",
     st_script_clear_color_int, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferClearDepthStencil", "enable=1 depth=0.25 stencil=0x5a mask=0xff",
     st_script_clear_depth_stencil, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferClearDepthStencil", "enable=1 depth=0.25 stencil=0 mask=0", st_script_clear_depth_only,
     ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferClearTexture", "texture A view=0 region=0,0,0,32,32,1 color=1,0,0,1 mask=0xf",
     st_script_clear_texture_f, ST_MUT_INT, st_ignore_clear_texture, ST_IGNORE_N(st_ignore_clear_texture), 1, 0},
    {"nvnCommandBufferClearTexturei", "texture A view=0 region=0,0,0,32,32,1 color=int mask=0xf",
     st_script_clear_texture_i, ST_MUT_INT, st_ignore_clear_texture, ST_IGNORE_N(st_ignore_clear_texture), 1, 0},
    {"nvnCommandBufferClearTextureui", "texture A view=0 region=0,0,0,32,32,1 color=uint mask=0xf",
     st_script_clear_texture_i, ST_MUT_INT, st_ignore_clear_texture, ST_IGNORE_N(st_ignore_clear_texture), 1, 0},
    {"nvnCommandBufferClearBuffer", "dst=0x100005000 size=0x100 value=0xdeadbeef", st_script_clear_buffer,
     ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDiscardColor", "index=3", st_script_discard_color, ST_MUT_INT, NULL, 0, 0, 1},
    {"nvnCommandBufferDiscardDepthStencil", "none", st_script_cb_only, ST_MUT_NONE, NULL, 0, 0, 1},
    {"nvnCommandBufferResolveDepthBuffer", "none", st_script_cb_only, ST_MUT_NONE, NULL, 0, 0, 1},
    {"nvnCommandBufferCopyBufferToBuffer", "src=0x100005000 dst=0x100006000 size=0x100",
     st_script_copy_buffer_to_buffer, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferCopyBufferToTexture", "src=0x100005000 texture A flags=0", st_script_copy_buffer_to_texture0,
     ST_MUT_INT, st_ignore_dma_out, ST_IGNORE_N(st_ignore_dma_out), 1, 0},
    {"nvnCommandBufferCopyBufferToTexture", "src=0x100005000 texture A flags=2", st_script_copy_buffer_to_texture2,
     ST_MUT_INT, st_ignore_2d_addresses, ST_IGNORE_N(st_ignore_2d_addresses), 1, 0},
    {"nvnCommandBufferCopyTextureToBuffer", "texture A dst=0x100006000 flags=0", st_script_copy_texture_to_buffer,
     ST_MUT_INT, st_ignore_dma_in, ST_IGNORE_N(st_ignore_dma_in), 1, 0},
    {"nvnCommandBufferCopyTextureToTexture", "src A dst B region=32x32x1 flags=0",
     st_script_copy_texture_to_texture, ST_MUT_INT, st_ignore_2d_addresses, ST_IGNORE_N(st_ignore_2d_addresses), 1, 0},
    {"nvnCommandBufferCopyCommands", "count=1 source list", st_script_copy_commands, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferDownsample", "src A dst B", st_script_downsample, ST_MUT_NONE, st_ignore_2d_addresses,
     ST_IGNORE_N(st_ignore_2d_addresses), 1, 0},
    {"nvnCommandBufferDownsampleTextureView", "src A view=0 dst B view=0", st_script_downsample_view, ST_MUT_NONE,
     st_ignore_2d_addresses, ST_IGNORE_N(st_ignore_2d_addresses), 1, 0},
    {"nvnCommandBufferTiledDownsample", "src A dst B", st_script_downsample, ST_MUT_NONE, st_ignore_tiled_downsample,
     ST_IGNORE_N(st_ignore_tiled_downsample), 1, 0},
    {"nvnCommandBufferTiledDownsampleTextureView", "src A view=0 dst B view=0", st_script_downsample_view, ST_MUT_NONE,
     st_ignore_tiled_downsample, ST_IGNORE_N(st_ignore_tiled_downsample), 1, 0},
    {"nvnCommandBufferUpdateUniformBuffer", "addr=0x100007000 size=0x180 offset=0x40 bytes=0x40 data=16 words",
     st_script_update_uniform_buffer, ST_MUT_INT, NULL, 0, 0, 0},
};

_Static_assert(sizeof st_d3_cases / sizeof st_d3_cases[0] == ST_D3_CASES, "the d3 case count");

/* ---- the d4 cases: the call reference, the debug markers and groups, and the ZCull ranges ---- */

/* One u64 argument: the description literal of a debug marker or group. */
static int st_script_description(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D4_DESCRIPTION}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The two Dynamic forms: the id, then the same "abc" text for both sides. */
static int st_script_id_text(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D4_MARKER_ID, args + ST_ARG_TEXT}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The two Static forms: the length, then the description literal. */
static int st_script_len_description(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                     uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D4_DESC_LENGTH, ST_D4_DESCRIPTION}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_group_id(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                              uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D4_MARKER_ID}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The two ZCull ranges: the address and the size in bytes. */
static int st_script_zcull(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                           uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, ST_D4_ZCULL_ADDRESS, ST_D4_ZCULL_SIZE}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The 11 d4 cases. The markers compare at a clear: whether their macro words change a register is what the run shows.
   The ZCull rows expect their difference: the host has no ZCull and answers with a logged no-op. CallCommands takes
   the handle of its own side's source list, made by st_d3_copy_source_setup. */
static const StCaseRow st_d4_cases[] = {
    {"nvnCommandBufferCallCommands", "count=1 src=SetLineWidth", st_script_copy_commands, ST_MUT_INT, NULL, 0, 0, 0},
    {"nvnCommandBufferInsertDebugMarker", "desc=0x1122334455667788", st_script_description, ST_MUT_NONE, NULL, 0, 0,
     0},
    {"nvnCommandBufferInsertDebugMarkerDynamic", "id=7 text=abc", st_script_id_text, ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferInsertDebugMarkerStatic", "len=16 desc=0x1122334455667788", st_script_len_description,
     ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferPushDebugGroup", "desc=0x1122334455667788", st_script_description, ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferPushDebugGroupDynamic", "id=7 text=abc", st_script_id_text, ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferPushDebugGroupStatic", "len=16 desc=0x1122334455667788", st_script_len_description, ST_MUT_NONE,
     NULL, 0, 0, 0},
    {"nvnCommandBufferPopDebugGroup", "", st_script_cb_only, ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferPopDebugGroupId", "id=7", st_script_group_id, ST_MUT_NONE, NULL, 0, 0, 0},
    {"nvnCommandBufferSaveZCullData", "addr=0x12340000 size=0x1000", st_script_zcull, ST_MUT_NONE, NULL, 0, 0, 1},
    {"nvnCommandBufferRestoreZCullData", "addr=0x12340000 size=0x1000", st_script_zcull, ST_MUT_NONE, NULL, 0, 0, 1},
};

_Static_assert(sizeof st_d4_cases / sizeof st_d4_cases[0] == ST_D4_CASES, "the d4 case count");

/* The 29 cases of decision 3. `rule` is the mutation rule of the row. `expect_noop` marks a host handler that is a
   logged no-op by design (COMMAND_NO_OP_HANDLER: SetSampleMask, SetStencilCullCriteria, SetColorReductionEnable,
   SetColorReductionThresholds, SetSubpixelPrecisionBias, SetTiledCacheAction, SetTiledCacheTileSize) and
   BindMultisampleState, whose replay returns at NVN_STATE_MULTISAMPLE. A difference there is expected, not a fail. */
static const struct {
    const char* name;
    const char* text;
    StScript script;
    uint64_t param;
    unsigned rule;
    int expect_noop;
} st_state_cases[] = {
    {"nvnCommandBufferSetAlphaRef", "value=0.5", st_script_float_arg, 0x3F000000u, ST_MUT_FLOAT, 0},
    {"nvnCommandBufferSetBlendColor", "color=0.1,0.2,0.3,0.4", st_script_blend_color, 0, ST_MUT_ARRAY, 0},
    {"nvnCommandBufferSetDepthBounds", "enable=1 near=0.25 far=0.75", st_script_depth_bounds, 0, ST_MUT_FLOAT, 0},
    {"nvnCommandBufferSetDepthClamp", "enable=1", st_script_int_arg, 1, ST_MUT_INT, 0},
    {"nvnCommandBufferSetDepthRange", "near=0.1 far=0.9", st_script_depth_range,
     (uint64_t)0x3DCCCCCDu | ((uint64_t)0x3F666666u << 32), ST_MUT_FLOAT, 0},
    {"nvnCommandBufferSetDepthRanges", "first=0 count=2 ranges=0.1,0.9,0.2,0.8", st_script_depth_ranges, 0,
     ST_MUT_ARRAY, 0},
    {"nvnCommandBufferSetLineWidth", "width=2.0", st_script_float_arg, 0x40000000u, ST_MUT_FLOAT, 0},
    {"nvnCommandBufferSetPolygonOffsetClamp", "factor=1.0 units=2.0 clamp=0.5", st_script_polygon_offset, 0,
     ST_MUT_FLOAT, 0},
    {"nvnCommandBufferSetRasterizerDiscard", "discard=1", st_script_int_arg, 1, ST_MUT_INT, 0},
    {"nvnCommandBufferSetStencilMask", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT, 0},
    {"nvnCommandBufferSetStencilRef", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT, 0},
    {"nvnCommandBufferSetStencilValueMask", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT, 0},
    {"nvnCommandBufferSetViewport", "x=16 y=32 w=640 h=360", st_script_viewport, 0, ST_MUT_INT, 0},
    {"nvnCommandBufferSetScissor", "x=8 y=8 w=100 h=50", st_script_scissor, 0, ST_MUT_INT, 0},
    {"nvnCommandBufferBindBlendState", "state=blend func=8,4,10,6", st_script_bind_state_draw, NVN_ST_STATE_BLEND,
     ST_MUT_SETTER, 0},
    {"nvnCommandBufferBindChannelMaskState", "state=channel mask target=3 0,1,0,1", st_script_bind_state_draw,
     NVN_ST_STATE_CHANNEL_MASK, ST_MUT_SETTER, 0},
    {"nvnCommandBufferBindColorState", "state=color logicOp=3", st_script_bind_state, NVN_ST_STATE_COLOR,
     ST_MUT_SETTER, 0},
    {"nvnCommandBufferBindDepthStencilState", "state=depth stencil depthFunc=2", st_script_bind_state,
     NVN_ST_STATE_DEPTH_STENCIL, ST_MUT_SETTER, 0},
    {"nvnCommandBufferBindMultisampleState", "state=multisample samples=4", st_script_bind_state,
     NVN_ST_STATE_MULTISAMPLE, ST_MUT_SETTER, 1},
    {"nvnCommandBufferBindPolygonState", "state=polygon cullFace=2", st_script_bind_state_draw, NVN_ST_STATE_POLYGON,
     ST_MUT_SETTER, 0},
    {"nvnCommandBufferSetSampleMask", "mask=0x1", st_script_int_arg, 1, ST_MUT_INT, 1},
    {"nvnCommandBufferSetStencilCullCriteria", "func=8 ref=0 mask=0xff", st_script_stencil_cull, 0, ST_MUT_INT, 1},
    {"nvnCommandBufferSetColorReductionEnable", "enable=1", st_script_int_arg, 1, ST_MUT_INT, 1},
    {"nvnCommandBufferSetColorReductionThresholds", "class=0 t0=1 t1=2", st_script_thresholds, 0, ST_MUT_INT, 1},
    {"nvnCommandBufferSetSubpixelPrecisionBias", "x=2 y=3", st_script_two_ints,
     (uint64_t)2u | ((uint64_t)3u << 32), ST_MUT_INT, 1},
    {"nvnCommandBufferSetTiledCacheAction", "action=1", st_script_int_arg, 1, ST_MUT_INT, 1},
    {"nvnCommandBufferSetTiledCacheTileSize", "width=64 height=64", st_script_two_ints,
     (uint64_t)64u | ((uint64_t)64u << 32), ST_MUT_INT, 1},
    {"nvnCommandBufferSetConservativeRasterEnable", "enable=0", st_script_int_arg, 0, ST_MUT_INT, 0},
    {"nvnCommandBufferBindCoverageModulationTable", "table=null", st_script_coverage_table, 0, ST_MUT_NONE, 0},
};

_Static_assert(sizeof st_state_cases / sizeof st_state_cases[0] == ST_STATE_CASES, "the state case count");

/* The descriptor pools and the two textures of the three d2 resource cases, one set for each side. */
typedef struct {
    NvnStResPool memory[2];
    uint64_t sampler_pool[2], texture_pool[2];
    uint64_t texture_pool_gva[2];
    uint64_t texture[2][2];
} StBindRes;

/* Builds one side's memory pool, its two descriptor pools and its two render target textures. The texture pool table
   is at 0, the sampler pool table at 0x4000 and the textures at 0x8000 and 0x9000. */
static int st_bind_res_setup(CPU* c, NvnStRun* run, NvnStResCache* cache, StBindRes* res, uint64_t args) {
    NvnStProc get_address = {0};
    if (!nvn_st_proc_find(&get_address, "nvnMemoryPoolGetBufferAddress")) return 0;
    for (int host = 0; host < 2; host++) {
        if (!nvn_st_res_pool_create(c, run, cache, host, &res->memory[host], NVN_ST_RES_POOL_CPU_CACHED,
                                    ST_BIND_POOL_BYTES))
            return 0;
        const uint64_t pool_gva =
            nvn_st_call(c, host ? &get_address.host : &get_address.lib, &(NvnStCall){.x = {res->memory[host].pool}});
        if (!pool_gva) return 0;
        res->texture_pool_gva[host] = pool_gva + ST_BIND_TEXTURE_POOL_OFFSET;
        res->texture_pool[host] = nvn_st_arena_alloc(&run->arena, ST_BIND_OBJ_BYTES, 16);
        res->sampler_pool[host] = nvn_st_arena_alloc(&run->arena, ST_BIND_OBJ_BYTES, 16);
        const uint64_t builder = nvn_st_arena_alloc(&run->arena, ST_BIND_OBJ_BYTES, 16);
        res->texture[host][0] = nvn_st_arena_alloc(&run->arena, ST_BIND_OBJ_BYTES, 16);
        res->texture[host][1] = nvn_st_arena_alloc(&run->arena, ST_BIND_OBJ_BYTES, 16);
        if (!res->texture_pool[host] || !res->sampler_pool[host] || !builder || !res->texture[host][0] ||
            !res->texture[host][1])
            return 0;
        memset(GP(res->texture_pool[host]), 0, ST_BIND_OBJ_BYTES);
        memset(GP(res->sampler_pool[host]), 0, ST_BIND_OBJ_BYTES);
        if (!nvn_st_descriptor_pool_build(c, cache, host, res->texture_pool[host], &res->memory[host],
                                          ST_BIND_TEXTURE_POOL_OFFSET, ST_BIND_ENTRIES, 0) ||
            !nvn_st_descriptor_pool_build(c, cache, host, res->sampler_pool[host], &res->memory[host],
                                          ST_BIND_SAMPLER_POOL_OFFSET, ST_BIND_ENTRIES, 1))
            return 0;
        memset(GP(builder), 0, ST_BIND_OBJ_BYTES);
        if (!nvn_st_texture_build(c, cache, host, builder, res->texture[host][0], &res->memory[host],
                                  ST_BIND_TEX_OFFSET_A, ST_BIND_TEX_SIDE, ST_BIND_TEX_SIDE, ST_BIND_TEX_FORMAT))
            return 0;
        memset(GP(builder), 0, ST_BIND_OBJ_BYTES);
        if (!nvn_st_texture_build(c, cache, host, builder, res->texture[host][1], &res->memory[host],
                                  ST_BIND_TEX_OFFSET_B, ST_BIND_TEX_SIDE, ST_BIND_TEX_SIDE, ST_BIND_TEX_FORMAT))
            return 0;
        const uint64_t at = args + (host ? ST_ARG_RT_HOST : ST_ARG_RT_LIB);
        gw64(at, res->texture[host][0]);
        gw64(at + 8u, res->texture[host][1]);
    }
    return 1;
}

static void st_bind_res_destroy(CPU* c, NvnStResCache* cache, StBindRes* res) {
    NvnStProc texture_final = {0}, texture_pool_final = {0}, sampler_pool_final = {0};
    nvn_st_proc_find(&texture_final, "nvnTextureFinalize");
    nvn_st_proc_find(&texture_pool_final, "nvnTexturePoolFinalize");
    nvn_st_proc_find(&sampler_pool_final, "nvnSamplerPoolFinalize");
    for (int host = 0; host < 2; host++) {
        const NvnStCallee* texture_fin = host ? &texture_final.host : &texture_final.lib;
        const NvnStCallee* texture_pool_fin = host ? &texture_pool_final.host : &texture_pool_final.lib;
        const NvnStCallee* sampler_pool_fin = host ? &sampler_pool_final.host : &sampler_pool_final.lib;
        if (texture_fin->fn) {
            nvn_st_call(c, texture_fin, &(NvnStCall){.x = {res->texture[host][0]}});
            nvn_st_call(c, texture_fin, &(NvnStCall){.x = {res->texture[host][1]}});
        }
        if (texture_pool_fin->fn) nvn_st_call(c, texture_pool_fin, &(NvnStCall){.x = {res->texture_pool[host]}});
        if (sampler_pool_fin->fn) nvn_st_call(c, sampler_pool_fin, &(NvnStCall){.x = {res->sampler_pool[host]}});
    }
    nvn_st_res_pool_destroy(c, cache, 0, &res->memory[0]);
    nvn_st_res_pool_destroy(c, cache, 1, &res->memory[1]);
}

static void st_run_case_table(CPU* c, NvnStRun* run, StCmdProcs* p, StCmdJob* job, uint64_t lib_cb,
                              uint64_t host_cb, uint64_t lib_pool, uint64_t host_pool, uint64_t args,
                              const char* mutate, const StBindCtx* ctx, int res_ok, int* stop,
                              const StCaseRow* cases, unsigned n_cases) {
    st_bind_ctx = ctx;
    for (int h = 0; h < 2; h++) job->texture_pool_gva[h] = ctx ? ctx->texture_pool_gva[h] : 0;
    for (unsigned i = 0; i < n_cases && !*stop; i++) {
        const StCaseRow* const row = &cases[i];
        if (row->needs_res && !res_ok) {
            run->skipped++;
            LOG("selftest: %s skipped, the descriptor pool or texture was not made\n", row->name);
            continue;
        }
        NvnStProc fn = {0};
        if (!nvn_st_proc_find(&fn, row->name)) {
            run->skipped++;
            LOG("selftest: %s skipped, not resolved\n", row->name);
            continue;
        }
        const unsigned rule = mutate[0] && !strcmp(mutate, row->name) ? row->rule : (unsigned)ST_MUT_NONE;
        st_case(c, run, p, job, lib_cb, host_cb, lib_pool, host_pool, args, row->name, row->text, row->script, &fn, 0,
                rule, row->expect_noop, stop, row->ignore, row->n_ignore);
    }
    st_bind_ctx = NULL;
    job->texture_pool_gva[0] = 0;
    job->texture_pool_gva[1] = 0;
}

/* The source command buffer of CopyCommands, one for each side: its own command memory pool and control memory, and
   the handle that EndRecording returned. */
typedef struct {
    NvnStResPool memory[2];
    uint64_t cb[2], control[2];
    int ready[2];
} StD3CopySource;

static int st_d3_copy_source_setup(CPU* c, StCmdProcs* p, NvnStResCache* cache, NvnStRun* run, StBindCtx* ctx,
                                   StD3CopySource* src) {
    NvnStProc line_width = {0};
    if (!nvn_st_proc_find(&line_width, "nvnCommandBufferSetLineWidth")) {
        LOG("selftest: nvnCommandBufferCopyCommands skipped, nvnCommandBufferSetLineWidth was not resolved\n");
        return 0;
    }
    for (int host = 0; host < 2; host++) {
        src->cb[host] = nvn_st_arena_alloc(&run->arena, ST_CB_OBJ_BYTES, 16);
        src->control[host] = nvn_st_arena_alloc(&run->arena, ST_CONTROL_BYTES, 0x1000);
        if (!src->cb[host] || !src->control[host]) {
            LOG("selftest: nvnCommandBufferCopyCommands skipped, the arena is full\n");
            return 0;
        }
        if (!st_setup_lists(c, p, cache, run, host, src->cb[host], src->control[host], &src->memory[host])) {
            LOG("selftest: nvnCommandBufferCopyCommands skipped, a source command buffer was not made\n");
            return 0;
        }
        src->ready[host] = 1;
        ST_CALL(c, &p->begin, host, src->cb[host]);
        st_call_proc(c, &line_width, host,
                     &(NvnStCall){.x = {src->cb[host]}, .s = {st_f2b(2.0f)}, .n_s = 1});
        ctx->copy_handle[host] = ST_CALL(c, &p->end, host, src->cb[host]);
        if (!ctx->copy_handle[host]) {
            LOG("selftest: nvnCommandBufferCopyCommands skipped, a source list has no handle\n");
            return 0;
        }
    }
    ctx->copy_ok = 1;
    return 1;
}

static void st_d3_copy_source_destroy(CPU* c, StCmdProcs* p, NvnStResCache* cache, StD3CopySource* src) {
    for (int host = 0; host < 2; host++) {
        if (src->ready[host]) ST_CALL(c, &p->finalize, host, src->cb[host]);
        if (src->memory[host].pool) nvn_st_res_pool_destroy(c, cache, host, &src->memory[host]);
    }
}

/* With nvn_native=0 the game's depth mode went to the library, so the host device record still reads 0. The depth
   range handlers read that record: copy the library value into the host. The library setter is not called, because
   it would change the game device. */
static void st_sync_host_depth_mode(CPU* c) {
    NvnStProc get_depth, set_depth;
    if (!nvn_st_proc_find(&get_depth, "nvnDeviceGetDepthMode")) {
        LOG("selftest: depth mode sync skipped, nvnDeviceGetDepthMode not resolved\n");
        return;
    }
    if (!nvn_st_proc_find(&set_depth, "nvnDeviceSetDepthMode")) {
        LOG("selftest: depth mode sync skipped, nvnDeviceSetDepthMode not resolved\n");
        return;
    }
    const uint64_t mode = nvn_st_call(c, &get_depth.lib, &(NvnStCall){.x = {nvn_st_device()}});
    nvn_st_call(c, &set_depth.host, &(NvnStCall){.x = {nvn_st_device(), mode}});
}

void nvn_selftest_command_cases(CPU* c, NvnStRun* run) {
    StCmdProcs procs = {0};
    if (!g_renderer_up) {
        st_skip_all(run, "the renderer is not up, so no clear reaches the recorder");
        return;
    }
    if (!st_procs_find(&procs)) {
        st_skip_all(run, "a command buffer function was not resolved, or the host has no handler");
        return;
    }
    const char* mutate = g_settings.nvn_selftest_mutation;
    if (mutate[0]) LOG("selftest: mutation %s, the host side gets a changed argument for it\n", mutate);
    if (!procs.free_bytes.lib.fn)
        LOG("selftest: no nvnCommandBufferGetCommandMemoryFree, the command memory is refilled before each case\n");

    StCmdJob* job = calloc(1, sizeof *job);
    if (!job) {
        st_skip_all(run, "out of memory for the GPU job");
        return;
    }
    pm_init(&job->lock);
    pc_init(&job->cv);
    NvnStResCache cache = {0};

    const uint64_t lib_cb = nvn_st_arena_alloc(&run->arena, ST_CB_OBJ_BYTES, 16);
    const uint64_t host_cb = nvn_st_arena_alloc(&run->arena, ST_CB_OBJ_BYTES, 16);
    const uint64_t lib_control = nvn_st_arena_alloc(&run->arena, ST_CONTROL_BYTES, 0x1000);
    const uint64_t host_control = nvn_st_arena_alloc(&run->arena, ST_CONTROL_BYTES, 0x1000);
    const uint64_t args = nvn_st_arena_alloc(&run->arena, ST_ARG_BYTES, 16);
    const uint64_t pool_scratch =
        nvn_st_arena_alloc(&run->arena, ST_POOL_ITEMS_MAX * ST_POOL_ITEM_WORDS * 4u, 16);
    if (!lib_cb || !host_cb || !lib_control || !host_control || !args || !pool_scratch) {
        st_skip_all(run, "the arena is full");
        pm_destroy(&job->lock);
        free(job);
        return;
    }
    job->lib_control = lib_control;
    job->host_control = host_control;
    job->pool_scratch = pool_scratch;
    NvnStResPool lib_pool = {0}, host_pool = {0};
    const int lib_ok = st_setup_lists(c, &procs, &cache, run, 0, lib_cb, lib_control, &lib_pool);
    const int host_ok = st_setup_lists(c, &procs, &cache, run, 1, host_cb, host_control, &host_pool);
    if (!lib_ok || !host_ok) {
        st_skip_all(run, "a command memory pool or a command buffer was not made");
        if (lib_pool.pool) nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        if (host_pool.pool) nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        pm_destroy(&job->lock);
        free(job);
        return;
    }

    uint32_t ids[2];
    ids[0] = syncpt_alloc();
    ids[1] = syncpt_alloc();
    job->lib = gpu_channel_open(ids[0]);
    job->host = gpu_channel_open(ids[1]);
    if (!job->lib || !job->host) {
        if (job->lib) gpu_channel_close(ids[0]);
        if (job->host) gpu_channel_close(ids[1]);
        syncpt_free(ids[0]);
        syncpt_free(ids[1]);
        st_skip_all(run, "a private channel was not made");
        nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        pm_destroy(&job->lock);
        free(job);
        return;
    }

    uint32_t* init_words = NULL;
    uint32_t init_count = 0;
    CPU saved = *c;
    const uint64_t device = nvn_st_device();
    const int init_ok = nvn_native_selftest_init_words(
        c, gr32(device + NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET), gr32(device + ST_DEVICE_DEPTH_MODE_OFFSET), &init_words,
        &init_count);
    *c = saved;
    if (!init_ok) {
        st_skip_all(run, "the queue init stream was not built, so the clear macro has no code");
        ST_CALL(c, &procs.finalize, 0, lib_cb);
        ST_CALL(c, &procs.finalize, 1, host_cb);
        nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        gpu_channel_close(ids[0]);
        gpu_channel_close(ids[1]);
        syncpt_free(ids[0]);
        syncpt_free(ids[1]);
        pm_destroy(&job->lock);
        free(job);
        return;
    }
    job->init_words = init_words;
    job->init_count = init_count;

    memcpy(GP(args + ST_ARG_COLOR), st_clear, sizeof st_clear);
    memset(GP(args + ST_ARG_TEXT), 0, 8);
    memcpy(GP(args + ST_ARG_TEXT), ST_D4_TEXT, sizeof ST_D4_TEXT);
    st_fill_s_args(args, mutate[0] && !strcmp(mutate, "nvnCommandBufferSetViewports"));
    int stop = 0;
    st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, "nvnCommandBufferSetViewports",
            "viewports=0,2 scissors=0,2 clear=0", st_script_viewports, NULL, 0, ST_MUT_NONE, 0, &stop, NULL, 0);
    for (unsigned k = 0; k < ST_BARRIERS && !stop; k++) {
        const uint32_t bits = 1u << k;
        const uint32_t host_bits = mutate[0] && !strcmp(mutate, "nvnCommandBufferBarrier") ? bits ^ 1u : bits;
        char text[32];
        snprintf(text, sizeof text, "bits=0x%x", bits);
        st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, "nvnCommandBufferBarrier",
                text, st_script_barrier, NULL, (uint64_t)bits | ((uint64_t)host_bits << 32), ST_MUT_NONE, 0, &stop,
                NULL, 0);
    }
    st_sync_host_depth_mode(c);
    for (unsigned i = 0; i < ST_STATE_CASES && !stop; i++) {
        const char* const name = st_state_cases[i].name;
        NvnStProc fn = {0};
        if (!nvn_st_proc_find(&fn, name)) {
            run->skipped++;
            LOG("selftest: %s skipped, not resolved\n", name);
            continue;
        }
        const unsigned rule =
            mutate[0] && !strcmp(mutate, name) ? st_state_cases[i].rule : (unsigned)ST_MUT_NONE;
        st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, name,
                st_state_cases[i].text, st_state_cases[i].script, &fn, st_state_cases[i].param, rule,
                st_state_cases[i].expect_noop, &stop, NULL, 0);
    }

    StBindCtx bind_ctx = {0};
    StBindRes bind_res = {0};
    StD3CopySource copy_source = {0};
    int bind_res_ok = 0;
    if (!stop) {
        bind_res_ok = st_bind_res_setup(c, run, &cache, &bind_res, args);
        if (!bind_res_ok)
            LOG("selftest: the descriptor pool and texture cases are skipped, a pool or a texture was not made\n");
        if (bind_res_ok) {
            for (int host = 0; host < 2; host++) {
                bind_ctx.sampler_pool[host] = bind_res.sampler_pool[host];
                bind_ctx.texture_pool[host] = bind_res.texture_pool[host];
                bind_ctx.texture_pool_gva[host] = bind_res.texture_pool_gva[host];
                bind_ctx.texture[host][0] = bind_res.texture[host][0];
                bind_ctx.texture[host][1] = bind_res.texture[host][1];
            }
        }
        st_run_case_table(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, mutate, &bind_ctx,
                          bind_res_ok, &stop, st_bind_cases, ST_BIND_CASES);
        if (!stop) {
            st_d3_copy_source_setup(c, &procs, &cache, run, &bind_ctx, &copy_source);
            st_run_case_table(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, mutate,
                              &bind_ctx, bind_res_ok, &stop, st_d3_cases, ST_D3_CASES);
            st_run_case_table(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, mutate,
                              &bind_ctx, bind_res_ok, &stop, st_d4_cases, ST_D4_CASES);
        }
    }

    if (stop) {
        LOG("selftest: the command cases stopped, the channels and the job stay (the GPU thread may still use them)\n");
        return;
    }
    if (bind_res_ok) st_bind_res_destroy(c, &cache, &bind_res);
    st_d3_copy_source_destroy(c, &procs, &cache, &copy_source);
    ST_CALL(c, &procs.finalize, 0, lib_cb);
    ST_CALL(c, &procs.finalize, 1, host_cb);
    nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
    nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
    gpu_channel_close(ids[0]);
    gpu_channel_close(ids[1]);
    syncpt_free(ids[0]);
    syncpt_free(ids[1]);
    free(init_words);
    free(job->base);
    pm_destroy(&job->lock);
    free(job);
}
