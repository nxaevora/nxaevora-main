/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The NVN execution test (nvn-unobserved.md, "Design of 4-u3-4"). It runs real work on the host path and checks
   the result in memory: fences and waits, clears, copies, the CPU read and write, a timeout, events and reports,
   two queues, a fence flush inside a command buffer, and a queue that is initialized again. Its own queues, pools,
   buffers, textures, events, syncs and command buffers live in one arena, and it writes no object of the game. */
#include "nx/gpu/nvn/test/nvn_exectest.h"
#include "nx/gpu/nvn/nvn_cmdlist.h"
#include "nx/gpu/nvn/nvn_native.h"
#include "nx/gpu/nvn/nvn_params.h"
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/memory/mem.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* 2 MiB, as the selftest: the objects of 16 lists and three pools, all page-aligned. */
#define NVN_EX_ARENA_BYTES 0x200000u
#define NVN_EX_OBJ_BYTES 0x1000u
#define NVN_EX_CONTROL_BYTES 0x800u
#define NVN_EX_COMMAND_BYTES 0x1000u
#define NVN_EX_POOL_ALIGN 0x1000u
#define NVN_EX_OBJ_ALIGN 16u
#define NVN_EX_CONTROL_ALIGN 0x100u
#define NVN_EX_TEX_POOL_BYTES 0x10000u
#define NVN_EX_DATA_POOL_BYTES 0x4000u
#define NVN_EX_CMD_POOL_BYTES 0x1000u
#define NVN_EX_QUEUE_MEMORY_MAX 0x100000u
#define NVN_EX_LISTS_MAX 16u
#define NVN_EX_DETAIL_MAX 128u

/* The waits: the limit of every SyncWait, and the short limit of E4 only. */
#define NVN_EX_WAIT_NS 2000000000ull
#define NVN_EX_SHORT_WAIT_NS 1000000ull

/* NvnOpFenceSync flags bit 0 (nvn_native.c, NVN_QUEUE_FENCE_FLUSH_FOR_CPU): the fence also posts the pending frame. */
#define NVN_EX_FENCE_FLUSH_FOR_CPU 1u

/* One 32x32 RGBA8 unorm texture and the buffer region of its copy: 4096 bytes. The clear color is exact in this format
   (0.0 and 1.0 only), so the CPU bytes are exact too. */
#define NVN_EX_TEXTURE_SIDE 32u
#define NVN_EX_TEXTURE_FORMAT 0x25u /* RGBA8 unorm (nvn_params.c's format table) */
#define NVN_EX_TEXTURE_LAYERS 1u
#define NVN_EX_TEXTURE_BYTES (NVN_EX_TEXTURE_SIDE * NVN_EX_TEXTURE_SIDE * 4u)
#define NVN_EX_CLEAR_MASK 0xFu
#define NVN_EX_TEX_OFFSET_A 0x0000u
#define NVN_EX_TEX_OFFSET_B 0x4000u
#define NVN_EX_TEX_OFFSET_C 0x8000u

/* The data pool: two buffers of the copy size, the reports and the event words. */
#define NVN_EX_BUF_OFFSET_A 0x0000u
#define NVN_EX_BUF_OFFSET_B 0x1000u
#define NVN_EX_REPORT_VALUE_OFFSET 0x2000u
#define NVN_EX_REPORT_COUNTER_OFFSET 0x2010u
#define NVN_EX_EVENT_OFFSET_A 0x2020u
#define NVN_EX_EVENT_OFFSET_B 0x2024u

/* The event values of E5 and the report value. */
#define NVN_EX_EVENT_WRITE_VALUE 0x10u
#define NVN_EX_EVENT_ADD_VALUE 0x20u
#define NVN_EX_SIDE_EVENT_WRITE_VALUE 0x100u
#define NVN_EX_SIDE_EVENT_ADD_VALUE 0x200u
#define NVN_EX_REPORT_VALUE 0x12345678u

typedef struct {
    NvnStProc defaults, device, priority, memory, memory_size, initialize;
} ExQueueProcs;

typedef struct {
    NvnStProc submit, fence, flush, wait, wait_sync, finish;
} ExQueueOps;

typedef struct {
    NvnStProc initialize, finalize, wait;
} ExSyncProcs;

typedef struct {
    NvnStProc initialize, finalize, add_command, add_control, begin, end, fence;
} ExCbProcs;

typedef struct {
    NvnStProc clear, copy_tex_buf, copy_buf_buf, signal_event, report_value, report_counter;
} ExCmdProcs;

typedef struct {
    NvnStProc builder_defaults, builder_device, builder_storage, initialize, finalize, map, address, flush, invalidate;
} ExBufProcs;

typedef struct {
    NvnStProc builder_defaults, builder_storage, initialize, finalize, signal, value, pool, offset;
} ExEventProcs;

typedef struct {
    CPU* c;
    NvnStRun* run;
    NvnStResCache cache;
    uint64_t device;
    unsigned mutate; /* NvnExCase, or NVN_EX_CASE_COUNT for no mutation */
} ExCtx;

typedef struct {
    ExCtx ctx;
    ExQueueProcs q;
    ExQueueOps qops;
    ExSyncProcs sync;
    ExCbProcs cb;
    ExCmdProcs cmd;
    ExBufProcs buf;
    ExEventProcs ev;
    NvnStProc pool_address, pool_flush, pool_invalidate, texture_finalize;
    const char *missing_queue, *missing_qops, *missing_qcross, *missing_sync, *missing_cb, *missing_cb_fence;
    const char *missing_clear, *missing_copy_tex, *missing_copy_buf;
    const char *missing_event, *missing_report, *missing_buf, *missing_pool;
    uint64_t q1, q2;
    uint64_t q2_builder; /* the builder of q2, for the new initialize of E8 */
    NvnStResPool tex_pool, data_pool, cmd_pool;
    uint64_t tex_a, tex_b, tex_c;
    uint64_t buf_a, buf_b;
    uint64_t ev_a, ev_b;
    uint64_t data_gpu;
    uint64_t cb_list[NVN_EX_LISTS_MAX];
    unsigned cb_count;
    uint64_t sync_list[NVN_EX_LISTS_MAX];
    unsigned sync_count;
    int stalled; /* a bounded wait that must signal ran out: a GPU job may still use the objects */
} ExState;

typedef struct {
    int failed;
    char text[NVN_EX_DETAIL_MAX];
} ExCheck;

#define EX_CALL(s, proc, ...) \
    nvn_st_call((s)->ctx.c, &((proc).host), &(NvnStCall){.x = {__VA_ARGS__}})

static const char* ex_queue_procs_find(ExQueueProcs* p) {
    if (!nvn_st_proc_find(&p->defaults, "nvnQueueBuilderSetDefaults")) return "nvnQueueBuilderSetDefaults";
    if (!nvn_st_proc_find(&p->device, "nvnQueueBuilderSetDevice")) return "nvnQueueBuilderSetDevice";
    if (!nvn_st_proc_find(&p->priority, "nvnQueueBuilderSetQueuePriority")) return "nvnQueueBuilderSetQueuePriority";
    if (!nvn_st_proc_find(&p->memory, "nvnQueueBuilderSetQueueMemory")) return "nvnQueueBuilderSetQueueMemory";
    if (!nvn_st_proc_find(&p->memory_size, "nvnQueueBuilderGetQueueMemorySize"))
        return "nvnQueueBuilderGetQueueMemorySize";
    if (!nvn_st_proc_find(&p->initialize, "nvnQueueInitialize")) return "nvnQueueInitialize";
    return NULL;
}

static const char* ex_queue_ops_find(ExQueueOps* p) {
    if (!nvn_st_proc_find(&p->submit, "nvnQueueSubmitCommands")) return "nvnQueueSubmitCommands";
    if (!nvn_st_proc_find(&p->fence, "nvnQueueFenceSync")) return "nvnQueueFenceSync";
    if (!nvn_st_proc_find(&p->flush, "nvnQueueFlush")) return "nvnQueueFlush";
    if (!nvn_st_proc_find(&p->wait, "nvnSyncWait")) return "nvnSyncWait";
    return NULL;
}

/* Only E6 uses these two, so a game that never resolved them skips E6 alone. */
static const char* ex_queue_cross_find(ExQueueOps* p) {
    if (!nvn_st_proc_find(&p->wait_sync, "nvnQueueWaitSync")) return "nvnQueueWaitSync";
    if (!nvn_st_proc_find(&p->finish, "nvnQueueFinish")) return "nvnQueueFinish";
    return NULL;
}

static const char* ex_sync_procs_find(ExSyncProcs* p) {
    if (!nvn_st_proc_find(&p->initialize, "nvnSyncInitialize")) return "nvnSyncInitialize";
    if (!nvn_st_proc_find(&p->finalize, "nvnSyncFinalize")) return "nvnSyncFinalize";
    return NULL;
}

static const char* ex_cb_procs_find(ExCbProcs* p) {
    if (!nvn_st_proc_find(&p->initialize, "nvnCommandBufferInitialize")) return "nvnCommandBufferInitialize";
    if (!nvn_st_proc_find(&p->finalize, "nvnCommandBufferFinalize")) return "nvnCommandBufferFinalize";
    if (!nvn_st_proc_find(&p->add_command, "nvnCommandBufferAddCommandMemory"))
        return "nvnCommandBufferAddCommandMemory";
    if (!nvn_st_proc_find(&p->add_control, "nvnCommandBufferAddControlMemory"))
        return "nvnCommandBufferAddControlMemory";
    if (!nvn_st_proc_find(&p->begin, "nvnCommandBufferBeginRecording")) return "nvnCommandBufferBeginRecording";
    if (!nvn_st_proc_find(&p->end, "nvnCommandBufferEndRecording")) return "nvnCommandBufferEndRecording";
    return NULL;
}

/* Only E7 uses this one, so a game that never resolved it skips E7 alone. */
static const char* ex_cb_fence_find(ExCbProcs* p) {
    if (!nvn_st_proc_find(&p->fence, "nvnCommandBufferFenceSync")) return "nvnCommandBufferFenceSync";
    return NULL;
}

static const char* ex_cmd_clear_find(ExCmdProcs* p) {
    if (!nvn_st_proc_find(&p->clear, "nvnCommandBufferClearTexture")) return "nvnCommandBufferClearTexture";
    return NULL;
}

static const char* ex_cmd_copy_tex_find(ExCmdProcs* p) {
    if (!nvn_st_proc_find(&p->copy_tex_buf, "nvnCommandBufferCopyTextureToBuffer"))
        return "nvnCommandBufferCopyTextureToBuffer";
    return NULL;
}

static const char* ex_cmd_copy_buf_find(ExCmdProcs* p) {
    if (!nvn_st_proc_find(&p->copy_buf_buf, "nvnCommandBufferCopyBufferToBuffer"))
        return "nvnCommandBufferCopyBufferToBuffer";
    return NULL;
}

static const char* ex_cmd_event_find(ExCmdProcs* p) {
    if (!nvn_st_proc_find(&p->signal_event, "nvnCommandBufferSignalEvent")) return "nvnCommandBufferSignalEvent";
    return NULL;
}

static const char* ex_cmd_report_find(ExCmdProcs* p) {
    if (!nvn_st_proc_find(&p->report_value, "nvnCommandBufferReportValue")) return "nvnCommandBufferReportValue";
    if (!nvn_st_proc_find(&p->report_counter, "nvnCommandBufferReportCounter"))
        return "nvnCommandBufferReportCounter";
    return NULL;
}

static const char* ex_buf_procs_find(ExBufProcs* p) {
    if (!nvn_st_proc_find(&p->builder_defaults, "nvnBufferBuilderSetDefaults"))
        return "nvnBufferBuilderSetDefaults";
    if (!nvn_st_proc_find(&p->builder_device, "nvnBufferBuilderSetDevice")) return "nvnBufferBuilderSetDevice";
    if (!nvn_st_proc_find(&p->builder_storage, "nvnBufferBuilderSetStorage")) return "nvnBufferBuilderSetStorage";
    if (!nvn_st_proc_find(&p->initialize, "nvnBufferInitialize")) return "nvnBufferInitialize";
    if (!nvn_st_proc_find(&p->finalize, "nvnBufferFinalize")) return "nvnBufferFinalize";
    if (!nvn_st_proc_find(&p->map, "nvnBufferMap")) return "nvnBufferMap";
    if (!nvn_st_proc_find(&p->address, "nvnBufferGetAddress")) return "nvnBufferGetAddress";
    if (!nvn_st_proc_find(&p->flush, "nvnBufferFlushMappedRange")) return "nvnBufferFlushMappedRange";
    if (!nvn_st_proc_find(&p->invalidate, "nvnBufferInvalidateMappedRange"))
        return "nvnBufferInvalidateMappedRange";
    return NULL;
}

static const char* ex_event_procs_find(ExEventProcs* p) {
    if (!nvn_st_proc_find(&p->builder_defaults, "nvnEventBuilderSetDefaults"))
        return "nvnEventBuilderSetDefaults";
    if (!nvn_st_proc_find(&p->builder_storage, "nvnEventBuilderSetStorage")) return "nvnEventBuilderSetStorage";
    if (!nvn_st_proc_find(&p->initialize, "nvnEventInitialize")) return "nvnEventInitialize";
    if (!nvn_st_proc_find(&p->finalize, "nvnEventFinalize")) return "nvnEventFinalize";
    if (!nvn_st_proc_find(&p->signal, "nvnEventSignal")) return "nvnEventSignal";
    if (!nvn_st_proc_find(&p->value, "nvnEventGetValue")) return "nvnEventGetValue";
    if (!nvn_st_proc_find(&p->pool, "nvnEventGetMemoryPool")) return "nvnEventGetMemoryPool";
    if (!nvn_st_proc_find(&p->offset, "nvnEventGetMemoryOffset")) return "nvnEventGetMemoryOffset";
    return NULL;
}

static const char* ex_pool_procs_find(NvnStProc* address, NvnStProc* flush, NvnStProc* invalidate) {
    if (!nvn_st_proc_find(address, "nvnMemoryPoolGetBufferAddress")) return "nvnMemoryPoolGetBufferAddress";
    if (!nvn_st_proc_find(flush, "nvnMemoryPoolFlushMappedRange")) return "nvnMemoryPoolFlushMappedRange";
    if (!nvn_st_proc_find(invalidate, "nvnMemoryPoolInvalidateMappedRange"))
        return "nvnMemoryPoolInvalidateMappedRange";
    return NULL;
}

static void ex_skip(NvnStRun* run, const char* name, const char* why) {
    run->skipped++;
    LOG("exectest: %s skipped %s\n", name, why);
}

static void ex_report(NvnStRun* run, const char* name, int ok, const char* detail) {
    run->cases++;
    if (!ok) run->failed++;
    LOG("exectest: %s %s %s\n", name, ok ? "pass" : "fail", detail);
}

static int ex_need(ExState* s, const char* name, const char* missing) {
    if (!missing) return 1;
    ex_skip(s->ctx.run, name, missing);
    return 0;
}

/* 1 when no earlier bounded wait ran out, else a skip line for this case. A case must not use objects that a GPU job
   may still read. */
static int ex_not_stalled(ExState* s, const char* name) {
    if (!s->stalled) return 1;
    ex_skip(s->ctx.run, name, "an earlier GPU job did not end");
    return 0;
}

/* A bounded wait that must signal. A 2 (TIMEOUT) sets the stall flag, so later cases and the cleanup keep the objects.
   E4's never-fenced wait and E8's wait do not use this: a non-signal result is their expectation. */
static uint64_t ex_wait(ExState* s, const char* name, const char* what, uint64_t sync) {
    const uint64_t result = EX_CALL(s, s->qops.wait, sync, NVN_EX_WAIT_NS);
    if (nvn_ex_wait_stalled(result)) {
        s->stalled = 1;
        LOG("exectest: %s wait %s sync=%" PRIx64 " gave %" PRIu64 ", an earlier GPU job did not end\n", name, what,
            sync, result);
    }
    return result;
}

static uint64_t ex_obj(ExCtx* x) {
    uint64_t at = nvn_st_arena_alloc(&x->run->arena, NVN_EX_OBJ_BYTES, NVN_EX_OBJ_ALIGN);
    if (at) memset(GP(at), 0, NVN_EX_OBJ_BYTES);
    return at;
}

static uint64_t ex_alloc(ExCtx* x, uint64_t bytes, uint64_t align) {
    return nvn_st_arena_alloc(&x->run->arena, bytes, align);
}

static int ex_case_mutated(const ExCtx* x, NvnExCase kase) { return x->mutate == (unsigned)kase; }

static void ex_check_u64(ExCheck* k, const char* field, uint64_t expected, uint64_t actual) {
    if (k->failed) return;
    if (!nvn_ex_compare_u64(field, expected, actual, k->text, sizeof k->text)) k->failed = 1;
}

static void ex_check_bytes(ExCheck* k, const uint8_t* expected, const uint8_t* actual, uint32_t size) {
    if (k->failed) return;
    if (!nvn_ex_compare_bytes(expected, actual, size, k->text, sizeof k->text)) k->failed = 1;
}

/* 0 or 1 is the result of a fence that was reached, either at once or after a wait. */
static void ex_check_signal(ExCheck* k, const char* field, uint64_t result) {
    if (k->failed) return;
    if (result == NVN_EX_SYNC_SIGNALED || result == NVN_EX_SYNC_SATISFIED) return;
    snprintf(k->text, sizeof k->text, "%s: expected 0 or 1, actual %" PRIu64, field, result);
    k->failed = 1;
}

/* Makes a queue and its builder in the arena. *builder_out, when not NULL, keeps the builder for a new initialize. */
static int ex_queue_new(ExState* s, uint64_t* queue_out, uint64_t* builder_out) {
    const uint64_t builder = ex_obj(&s->ctx), queue = ex_obj(&s->ctx);
    if (!builder || !queue) return 0;
    EX_CALL(s, s->q.defaults, builder);
    EX_CALL(s, s->q.device, builder, s->ctx.device);
    EX_CALL(s, s->q.priority, builder, NVN_QUEUE_PRIORITY_DEFAULT);
    const uint64_t bytes = EX_CALL(s, s->q.memory_size, builder);
    if (!bytes || bytes > NVN_EX_QUEUE_MEMORY_MAX) return 0;
    const uint64_t memory = ex_alloc(&s->ctx, bytes, NVN_EX_POOL_ALIGN);
    if (!memory) return 0;
    EX_CALL(s, s->q.memory, builder, memory, bytes);
    if (!EX_CALL(s, s->q.initialize, queue, builder)) return 0;
    *queue_out = queue;
    if (builder_out) *builder_out = builder;
    return 1;
}

static uint64_t ex_sync_new(ExState* s) {
    if (s->sync_count >= NVN_EX_LISTS_MAX) return 0;
    const uint64_t sync = ex_obj(&s->ctx);
    if (!sync) return 0;
    if (!EX_CALL(s, s->sync.initialize, sync, s->ctx.device)) return 0;
    s->sync_list[s->sync_count++] = sync;
    return sync;
}

static void ex_fence(ExState* s, uint64_t queue, uint64_t sync, uint32_t flags) {
    EX_CALL(s, s->qops.fence, queue, sync, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, flags);
}

static int ex_list_begin(ExState* s, const char* name, uint64_t* cb_out, uint64_t* control_out) {
    if (s->cb_count >= NVN_EX_LISTS_MAX) {
        ex_skip(s->ctx.run, name, "too many command buffers");
        return 0;
    }
    const uint64_t cb = ex_obj(&s->ctx), control = ex_alloc(&s->ctx, NVN_EX_CONTROL_BYTES, NVN_EX_CONTROL_ALIGN);
    if (!cb || !control) {
        ex_skip(s->ctx.run, name, "the arena is full");
        return 0;
    }
    if (!EX_CALL(s, s->cb.initialize, cb, s->ctx.device)) {
        ex_skip(s->ctx.run, name, "nvnCommandBufferInitialize failed");
        return 0;
    }
    EX_CALL(s, s->cb.add_command, cb, s->cmd_pool.pool, 0, NVN_EX_COMMAND_BYTES);
    EX_CALL(s, s->cb.add_control, cb, control, NVN_EX_CONTROL_BYTES);
    EX_CALL(s, s->cb.begin, cb);
    s->cb_list[s->cb_count++] = cb;
    *cb_out = cb;
    *control_out = control;
    return 1;
}

static int ex_submit(ExState* s, const char* name, uint64_t queue, uint64_t handle) {
    const uint64_t handles = ex_alloc(&s->ctx, 8, 8);
    if (!handles) {
        ex_skip(s->ctx.run, name, "the arena is full");
        return 0;
    }
    gw64(handles, handle);
    EX_CALL(s, s->qops.submit, queue, 1, handles);
    return 1;
}

static int ex_region_color(ExState* s, const char* name, uint64_t* region_out, uint64_t* color_out) {
    const uint64_t region = ex_alloc(&s->ctx, 6 * 4, NVN_EX_OBJ_ALIGN);
    const uint64_t color = ex_alloc(&s->ctx, 4 * 4, NVN_EX_OBJ_ALIGN);
    if (!region || !color) {
        ex_skip(s->ctx.run, name, "the arena is full");
        return 0;
    }
    const uint32_t words[6] = {0, 0, 0, NVN_EX_TEXTURE_SIDE, NVN_EX_TEXTURE_SIDE, NVN_EX_TEXTURE_LAYERS};
    memcpy(GP(region), words, sizeof words);
    *region_out = region;
    *color_out = color;
    return 1;
}

static int ex_clear_list(ExState* s, const char* name, uint64_t texture, const float color[4],
                         uint64_t* handle_out) {
    uint64_t region, color_at, cb, control;
    if (!ex_region_color(s, name, &region, &color_at)) return 0;
    memcpy(GP(color_at), color, 4 * sizeof color[0]);
    if (!ex_list_begin(s, name, &cb, &control)) return 0;
    EX_CALL(s, s->cmd.clear, cb, texture, 0, region, color_at, NVN_EX_CLEAR_MASK);
    *handle_out = EX_CALL(s, s->cb.end, cb);
    if (!*handle_out) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return 0;
    }
    return 1;
}

/* E1: two lists, a fence after each, one flush, then the two waits. Under the mutation no flush comes, so the second
   wait runs out (2) and the case fails. */
static void case_fence_order(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_FENCE_ORDER);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_clear))
        return;
    if (!s->q1 || !s->tex_a || !s->tex_b) {
        ex_skip(s->ctx.run, name, "a queue or a texture was not made");
        return;
    }
    const float color[4] = {1.0f, 0.0f, 1.0f, 0.0f};
    uint64_t h1, h2;
    if (!ex_clear_list(s, name, s->tex_a, color, &h1)) return;
    if (!ex_submit(s, name, s->q1, h1)) return;
    const uint64_t first = ex_sync_new(s);
    if (!first) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, first, 0);
    if (!ex_clear_list(s, name, s->tex_b, color, &h2)) return;
    if (!ex_submit(s, name, s->q1, h2)) return;
    const uint64_t second = ex_sync_new(s);
    if (!second) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, second, 0);
    if (!ex_case_mutated(&s->ctx, NVN_EX_CASE_FENCE_ORDER)) EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t wait_second = ex_wait(s, name, "second fence", second);
    const uint64_t wait_first = EX_CALL(s, s->qops.wait, first, 0);
    char text[NVN_EX_DETAIL_MAX];
    const int ok = nvn_ex_e1_ok(wait_second, wait_first);
    if (!ok) {
        if (wait_second != NVN_EX_SYNC_SIGNALED && wait_second != NVN_EX_SYNC_SATISFIED)
            snprintf(text, sizeof text, "wait2: expected 0 or 1, actual %" PRIu64, wait_second);
        else
            snprintf(text, sizeof text, "wait1: expected %u, actual %" PRIu64, NVN_EX_SYNC_SIGNALED, wait_first);
    } else {
        snprintf(text, sizeof text, "wait2=%" PRIu64 " wait1=%" PRIu64, wait_second, wait_first);
    }
    ex_report(s->ctx.run, name, ok, text);
}

/* E2: clear, copy to a mapped buffer, fence, flush, wait, invalidate, then the CPU bytes. Under the mutation one clear
   channel is 0, so the bytes differ. */
static void case_gpu_to_cpu(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_GPU_TO_CPU);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_clear) || !ex_need(s, name, s->missing_copy_tex) ||
        !ex_need(s, name, s->missing_buf) || !ex_need(s, name, s->missing_pool))
        return;
    if (!s->q1 || !s->tex_a || !s->buf_a) {
        ex_skip(s->ctx.run, name, "a queue or an object was not made");
        return;
    }
    uint64_t region, color_at, cb, control;
    if (!ex_region_color(s, name, &region, &color_at)) return;
    float color[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    if (ex_case_mutated(&s->ctx, NVN_EX_CASE_GPU_TO_CPU)) color[1] = 0.0f;
    memcpy(GP(color_at), color, sizeof color);
    if (!ex_list_begin(s, name, &cb, &control)) return;
    const uint64_t buffer_gpu = EX_CALL(s, s->buf.address, s->buf_a);
    if (!buffer_gpu) {
        ex_skip(s->ctx.run, name, "the buffer has no GPU address");
        return;
    }
    EX_CALL(s, s->cmd.clear, cb, s->tex_a, 0, region, color_at, NVN_EX_CLEAR_MASK);
    EX_CALL(s, s->cmd.copy_tex_buf, cb, s->tex_a, 0, region, buffer_gpu, 0);
    const uint64_t handle = EX_CALL(s, s->cb.end, cb);
    if (!handle) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return;
    }
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, fence, 0);
    EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t wait = ex_wait(s, name, "fence", fence);
    EX_CALL(s, s->buf.invalidate, s->buf_a, 0, NVN_EX_TEXTURE_BYTES);
    EX_CALL(s, s->pool_invalidate, s->data_pool.pool, NVN_EX_BUF_OFFSET_A, NVN_EX_TEXTURE_BYTES);
    const uint64_t cpu = EX_CALL(s, s->buf.map, s->buf_a);
    if (!cpu) {
        ex_skip(s->ctx.run, name, "the buffer has no CPU address");
        return;
    }
    const float exact[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    uint8_t pixel[4], expected[NVN_EX_TEXTURE_BYTES];
    nvn_ex_rgba8_bytes(exact, pixel);
    for (uint32_t i = 0; i < NVN_EX_TEXTURE_BYTES; i++) expected[i] = pixel[i & 3u];
    ExCheck check = {0};
    ex_check_signal(&check, "wait", wait);
    ex_check_bytes(&check, expected, GP(cpu), NVN_EX_TEXTURE_BYTES);
    ex_report(s->ctx.run, name, !check.failed, check.failed ? check.text : "the copied bytes match");
}

/* E3: a CPU pattern in a mapped buffer, the two flushes, a buffer copy, fence, flush, wait, invalidate, then the second
   buffer's bytes. Under the mutation the copy is half, so its second half differs. */
static void case_cpu_to_gpu(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_CPU_TO_GPU);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_copy_buf) || !ex_need(s, name, s->missing_buf) ||
        !ex_need(s, name, s->missing_pool))
        return;
    if (!s->q1 || !s->buf_a || !s->buf_b) {
        ex_skip(s->ctx.run, name, "a queue or a buffer was not made");
        return;
    }
    const uint64_t cpu = EX_CALL(s, s->buf.map, s->buf_a);
    if (!cpu) {
        ex_skip(s->ctx.run, name, "the buffer has no CPU address");
        return;
    }
    uint8_t expected[NVN_EX_TEXTURE_BYTES];
    for (uint32_t i = 0; i < NVN_EX_TEXTURE_BYTES; i++) expected[i] = (uint8_t)(i * 31u + 7u);
    memcpy(GP(cpu), expected, sizeof expected);
    EX_CALL(s, s->buf.flush, s->buf_a, 0, NVN_EX_TEXTURE_BYTES);
    EX_CALL(s, s->pool_flush, s->data_pool.pool, NVN_EX_BUF_OFFSET_A, NVN_EX_TEXTURE_BYTES);
    uint64_t cb, control;
    if (!ex_list_begin(s, name, &cb, &control)) return;
    const uint64_t src = EX_CALL(s, s->buf.address, s->buf_a);
    const uint64_t dst = EX_CALL(s, s->buf.address, s->buf_b);
    if (!src || !dst) {
        ex_skip(s->ctx.run, name, "a buffer has no GPU address");
        return;
    }
    const uint64_t size = ex_case_mutated(&s->ctx, NVN_EX_CASE_CPU_TO_GPU) ? NVN_EX_TEXTURE_BYTES / 2u
                                                                           : NVN_EX_TEXTURE_BYTES;
    EX_CALL(s, s->cmd.copy_buf_buf, cb, src, dst, size);
    const uint64_t handle = EX_CALL(s, s->cb.end, cb);
    if (!handle) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return;
    }
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, fence, 0);
    EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t wait = ex_wait(s, name, "fence", fence);
    EX_CALL(s, s->buf.invalidate, s->buf_b, 0, NVN_EX_TEXTURE_BYTES);
    EX_CALL(s, s->pool_invalidate, s->data_pool.pool, NVN_EX_BUF_OFFSET_B, NVN_EX_TEXTURE_BYTES);
    const uint64_t dst_cpu = EX_CALL(s, s->buf.map, s->buf_b);
    if (!dst_cpu) {
        ex_skip(s->ctx.run, name, "the buffer has no CPU address");
        return;
    }
    ExCheck check = {0};
    ex_check_signal(&check, "wait", wait);
    ex_check_bytes(&check, expected, GP(dst_cpu), NVN_EX_TEXTURE_BYTES);
    ex_report(s->ctx.run, name, !check.failed, check.failed ? check.text : "the copied pattern matches");
}

/* E4: a fence behind a pending list, a short wait (2), a flush, a wait (0 or 1), and a never fenced sync (3). Under
   the mutation the flush comes before the short wait, so it is not 2. */
static void case_timeout(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_TIMEOUT);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_clear))
        return;
    if (!s->q1 || !s->tex_a) {
        ex_skip(s->ctx.run, name, "a queue or a texture was not made");
        return;
    }
    const float color[4] = {1.0f, 0.0f, 1.0f, 0.0f};
    uint64_t handle;
    if (!ex_clear_list(s, name, s->tex_a, color, &handle)) return;
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, fence, 0);
    if (ex_case_mutated(&s->ctx, NVN_EX_CASE_TIMEOUT)) EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t short_wait = EX_CALL(s, s->qops.wait, fence, NVN_EX_SHORT_WAIT_NS);
    EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t after_flush = ex_wait(s, name, "fence after the flush", fence);
    const uint64_t never_fenced = ex_sync_new(s);
    if (!never_fenced) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    const uint64_t never_wait = EX_CALL(s, s->qops.wait, never_fenced, NVN_EX_WAIT_NS); /* must fail, not a stall */
    ExCheck check = {0};
    const int ok = nvn_ex_e4_ok(short_wait, after_flush, never_wait);
    if (!ok) {
        if (short_wait != NVN_EX_SYNC_TIMEOUT)
            ex_check_u64(&check, "short wait", NVN_EX_SYNC_TIMEOUT, short_wait);
        else if (after_flush != NVN_EX_SYNC_SIGNALED && after_flush != NVN_EX_SYNC_SATISFIED)
            ex_check_signal(&check, "wait after the flush", after_flush);
        else
            ex_check_u64(&check, "wait of the unfenced sync", NVN_EX_SYNC_FAILED, never_wait);
    }
    if (ok) {
        char text[NVN_EX_DETAIL_MAX];
        snprintf(text, sizeof text, "short=%" PRIu64 " after=%" PRIu64 " never=%" PRIu64, short_wait, after_flush,
                 never_wait);
        ex_report(s->ctx.run, name, 1, text);
    } else {
        ex_report(s->ctx.run, name, 0, check.text);
    }
}

/* E5: the two events and the two reports in one list, a fence and a wait, then the event word, the report words and the
   CPU event side. Under the mutation the add value is one higher, so the event sum differs. */
static void case_event_report(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_EVENT_REPORT);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_event) || !ex_need(s, name, s->missing_report) ||
        !ex_need(s, name, s->missing_pool))
        return;
    if (!s->q1 || !s->ev_a || !s->ev_b || !s->data_gpu) {
        ex_skip(s->ctx.run, name, "a queue, an event or the pool address was not made");
        return;
    }
    uint64_t cb, control;
    if (!ex_list_begin(s, name, &cb, &control)) return;
    const uint32_t add = NVN_EX_EVENT_ADD_VALUE + (ex_case_mutated(&s->ctx, NVN_EX_CASE_EVENT_REPORT) ? 1u : 0u);
    EX_CALL(s, s->cmd.signal_event, cb, s->ev_a, NVN_EVENT_SIGNAL_WRITE, 0, 0, NVN_EX_EVENT_WRITE_VALUE);
    EX_CALL(s, s->cmd.signal_event, cb, s->ev_a, NVN_EVENT_SIGNAL_ADD, 0, 0, add);
    EX_CALL(s, s->cmd.report_value, cb, NVN_EX_REPORT_VALUE, s->data_gpu + NVN_EX_REPORT_VALUE_OFFSET);
    EX_CALL(s, s->cmd.report_counter, cb, NVN_REPORT_TYPE_MAX, s->data_gpu + NVN_EX_REPORT_COUNTER_OFFSET);
    const uint64_t handle = EX_CALL(s, s->cb.end, cb);
    if (!handle) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return;
    }
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, fence, 0);
    EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t wait = ex_wait(s, name, "fence", fence);
    ExCheck check = {0};
    ex_check_signal(&check, "wait", wait);
    const uint64_t event = EX_CALL(s, s->ev.value, s->ev_a);
    ex_check_u64(&check, "event sum", NVN_EX_EVENT_WRITE_VALUE + NVN_EX_EVENT_ADD_VALUE, event);
    const uint64_t report_value = gr64(s->data_pool.storage + NVN_EX_REPORT_VALUE_OFFSET);
    const uint64_t report_value_time = gr64(s->data_pool.storage + NVN_EX_REPORT_VALUE_OFFSET + 8u);
    ex_check_u64(&check, "report value", NVN_EX_REPORT_VALUE, report_value);
    ex_check_u64(&check, "report value timestamp != 0", 1, report_value_time != 0);
    const uint64_t report_counter_time = gr64(s->data_pool.storage + NVN_EX_REPORT_COUNTER_OFFSET + 8u);
    ex_check_u64(&check, "report counter timestamp != 0", 1, report_counter_time != 0);
    EX_CALL(s, s->ev.signal, s->ev_b, NVN_EVENT_SIGNAL_WRITE, NVN_EX_SIDE_EVENT_WRITE_VALUE);
    EX_CALL(s, s->ev.signal, s->ev_b, NVN_EVENT_SIGNAL_ADD, NVN_EX_SIDE_EVENT_ADD_VALUE);
    const uint64_t side = EX_CALL(s, s->ev.value, s->ev_b);
    ex_check_u64(&check, "CPU event sum", NVN_EX_SIDE_EVENT_WRITE_VALUE + NVN_EX_SIDE_EVENT_ADD_VALUE, side);
    ex_check_u64(&check, "event memory pool", s->data_pool.pool, EX_CALL(s, s->ev.pool, s->ev_b));
    ex_check_u64(&check, "event memory offset", NVN_EX_EVENT_OFFSET_B, EX_CALL(s, s->ev.offset, s->ev_b));
    ex_report(s->ctx.run, name, !check.failed, check.failed ? check.text : "the events, reports and order match");
}

/* E6: queue 1 clears and fences, queue 2 waits for that fence, copies, fences with FLUSH_FOR_CPU and finishes; the
   bytes are read with no SyncWait. Under the mutation queue 2 copies a texture that was never cleared. The queue
   records stay in the host: the host has no QueueFinalize handler, so the two queues live to the end of the process. */
static void case_two_queues(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_TWO_QUEUES);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_qcross) || !ex_need(s, name, s->missing_sync) ||
        !ex_need(s, name, s->missing_cb) || !ex_need(s, name, s->missing_clear) ||
        !ex_need(s, name, s->missing_copy_tex) || !ex_need(s, name, s->missing_buf) ||
        !ex_need(s, name, s->missing_pool))
        return;
    if (!s->q1 || !s->q2 || !s->tex_a || !s->tex_c || !s->buf_b) {
        ex_skip(s->ctx.run, name, "a queue or an object was not made");
        return;
    }
    const float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    uint64_t handle;
    if (!ex_clear_list(s, name, s->tex_a, color, &handle)) return;
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q1, fence, 0);
    EX_CALL(s, s->qops.flush, s->q1);
    const uint64_t wait_sync = EX_CALL(s, s->qops.wait_sync, s->q2, fence);
    uint64_t region, color_at, cb, control;
    if (!ex_region_color(s, name, &region, &color_at)) return;
    memcpy(GP(color_at), color, sizeof color);
    if (!ex_list_begin(s, name, &cb, &control)) return;
    const uint64_t buffer_gpu = EX_CALL(s, s->buf.address, s->buf_b);
    if (!buffer_gpu) {
        ex_skip(s->ctx.run, name, "the buffer has no GPU address");
        return;
    }
    const uint64_t source = ex_case_mutated(&s->ctx, NVN_EX_CASE_TWO_QUEUES) ? s->tex_c : s->tex_a;
    EX_CALL(s, s->cmd.copy_tex_buf, cb, source, 0, region, buffer_gpu, 0);
    const uint64_t copy = EX_CALL(s, s->cb.end, cb);
    if (!copy) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return;
    }
    if (!ex_submit(s, name, s->q2, copy)) return;
    const uint64_t copy_sync = ex_sync_new(s);
    if (!copy_sync) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q2, copy_sync, NVN_EX_FENCE_FLUSH_FOR_CPU);
    const uint64_t copy_wait = ex_wait(s, name, "copy fence", copy_sync);
    if (copy_wait != NVN_EX_SYNC_SIGNALED && copy_wait != NVN_EX_SYNC_SATISFIED) {
        char text[NVN_EX_DETAIL_MAX];
        snprintf(text, sizeof text, "wait before QueueFinish: expected 0 or 1, actual %" PRIu64, copy_wait);
        ex_report(s->ctx.run, name, 0, text);
        return;
    }
    EX_CALL(s, s->qops.finish, s->q2); /* the last posted value is reached, so it returns at once */
    const uint64_t cpu = EX_CALL(s, s->buf.map, s->buf_b);
    if (!cpu) {
        ex_skip(s->ctx.run, name, "the buffer has no CPU address");
        return;
    }
    const float exact[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    uint8_t pixel[4], expected[NVN_EX_TEXTURE_BYTES];
    nvn_ex_rgba8_bytes(exact, pixel);
    for (uint32_t i = 0; i < NVN_EX_TEXTURE_BYTES; i++) expected[i] = pixel[i & 3u];
    ExCheck check = {0};
    ex_check_u64(&check, "QueueWaitSync", 1, wait_sync);
    ex_check_bytes(&check, expected, GP(cpu), NVN_EX_TEXTURE_BYTES);
    ex_report(s->ctx.run, name, !check.failed, check.failed ? check.text : "the second queue's copy matches");
}

/* E7: one list clears texture B and fences a new sync inside the list with FLUSH_FOR_CPU, without QueueFlush. The
   submit posts the frame through the fence flag, so the wait is reached (0 or 1). Under the mutation the flag is 0, so
   nothing posts the frame and the wait runs out (2). */
static void case_cb_fence_flush(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_CB_FENCE_FLUSH);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_cb_fence) || !ex_need(s, name, s->missing_clear))
        return;
    if (!s->q1 || !s->tex_b) {
        ex_skip(s->ctx.run, name, "a queue or a texture was not made");
        return;
    }
    const float color[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    uint64_t region, color_at, cb, control;
    if (!ex_region_color(s, name, &region, &color_at)) return;
    memcpy(GP(color_at), color, sizeof color);
    if (!ex_list_begin(s, name, &cb, &control)) return;
    EX_CALL(s, s->cmd.clear, cb, s->tex_b, 0, region, color_at, NVN_EX_CLEAR_MASK);
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    const uint64_t flags = ex_case_mutated(&s->ctx, NVN_EX_CASE_CB_FENCE_FLUSH) ? 0u : NVN_EX_FENCE_FLUSH_FOR_CPU;
    EX_CALL(s, s->cb.fence, cb, fence, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, flags);
    const uint64_t handle = EX_CALL(s, s->cb.end, cb);
    if (!handle) {
        ex_skip(s->ctx.run, name, "the command list was not published");
        return;
    }
    if (!ex_submit(s, name, s->q1, handle)) return;
    const uint64_t wait = ex_wait(s, name, "command buffer fence", fence);
    const int ok = nvn_ex_e7_ok(wait);
    char text[NVN_EX_DETAIL_MAX];
    if (ok)
        snprintf(text, sizeof text, "wait=%" PRIu64, wait);
    else
        snprintf(text, sizeof text, "wait: expected 0 or 1, actual %" PRIu64, wait);
    ex_report(s->ctx.run, name, ok, text);
}

/* E8: a list on queue 2 and a fence with flags 0, then the queue is initialized again. The dropped frame leaves the
   fence UNSIGNALED, so the wait fails (3). Under the mutation no initialize comes, so the wait runs out (2). That wait
   is not the stall helper: a timeout is the expected result there, not a stalled job of the host. */
static void case_queue_reinit(ExState* s) {
    const char* const name = nvn_ex_case_name(NVN_EX_CASE_QUEUE_REINIT);
    if (!ex_not_stalled(s, name)) return;
    if (!ex_need(s, name, s->missing_queue) || !ex_need(s, name, s->missing_qops) ||
        !ex_need(s, name, s->missing_sync) || !ex_need(s, name, s->missing_cb) ||
        !ex_need(s, name, s->missing_clear))
        return;
    if (!s->q2 || !s->tex_c || !s->q2_builder) {
        ex_skip(s->ctx.run, name, "a queue or a texture was not made");
        return;
    }
    const float color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    uint64_t handle;
    if (!ex_clear_list(s, name, s->tex_c, color, &handle)) return;
    if (!ex_submit(s, name, s->q2, handle)) return;
    const uint64_t fence = ex_sync_new(s);
    if (!fence) {
        ex_skip(s->ctx.run, name, "a sync was not made");
        return;
    }
    ex_fence(s, s->q2, fence, 0);
    if (!ex_case_mutated(&s->ctx, NVN_EX_CASE_QUEUE_REINIT))
        EX_CALL(s, s->q.initialize, s->q2, s->q2_builder);
    const uint64_t wait = EX_CALL(s, s->qops.wait, fence, NVN_EX_WAIT_NS);
    const int ok = nvn_ex_e8_ok(wait);
    char text[NVN_EX_DETAIL_MAX];
    if (ok)
        snprintf(text, sizeof text, "wait=%" PRIu64, wait);
    else
        snprintf(text, sizeof text, "wait of the dropped fence: expected %u, actual %" PRIu64, NVN_EX_SYNC_FAILED,
                 wait);
    ex_report(s->ctx.run, name, ok, text);
}

static int ex_pool_new(ExState* s, NvnStResPool* pool, uint32_t flags, uint64_t bytes) {
    return nvn_st_res_pool_create(s->ctx.c, s->ctx.run, &s->ctx.cache, 1, pool, flags, bytes);
}

static int ex_texture_new(ExState* s, uint64_t* texture_out, const NvnStResPool* pool, uint64_t offset) {
    const uint64_t builder = ex_obj(&s->ctx), texture = ex_obj(&s->ctx);
    if (!builder || !texture) return 0;
    if (!nvn_st_texture_build(s->ctx.c, &s->ctx.cache, 1, builder, texture, pool, offset, NVN_EX_TEXTURE_SIDE,
                              NVN_EX_TEXTURE_SIDE, NVN_EX_TEXTURE_FORMAT))
        return 0;
    *texture_out = texture;
    return 1;
}

static int ex_buffer_new(ExState* s, uint64_t* buffer_out, const NvnStResPool* pool, uint64_t offset) {
    const uint64_t builder = ex_obj(&s->ctx), buffer = ex_obj(&s->ctx);
    if (!builder || !buffer) return 0;
    EX_CALL(s, s->buf.builder_defaults, builder);
    EX_CALL(s, s->buf.builder_device, builder, s->ctx.device);
    EX_CALL(s, s->buf.builder_storage, builder, pool->pool, offset, NVN_EX_TEXTURE_BYTES);
    if (!EX_CALL(s, s->buf.initialize, buffer, builder)) return 0;
    *buffer_out = buffer;
    return 1;
}

static int ex_event_new(ExState* s, uint64_t* event_out, const NvnStResPool* pool, uint64_t offset) {
    const uint64_t builder = ex_obj(&s->ctx), event = ex_obj(&s->ctx);
    if (!builder || !event) return 0;
    EX_CALL(s, s->ev.builder_defaults, builder);
    EX_CALL(s, s->ev.builder_storage, builder, pool->pool, offset);
    if (!EX_CALL(s, s->ev.initialize, event, builder)) return 0;
    *event_out = event;
    return 1;
}

static void ex_setup(ExState* s) {
    s->missing_queue = ex_queue_procs_find(&s->q);
    s->missing_qops = ex_queue_ops_find(&s->qops);
    s->missing_qcross = ex_queue_cross_find(&s->qops);
    s->missing_sync = ex_sync_procs_find(&s->sync);
    s->missing_cb = ex_cb_procs_find(&s->cb);
    s->missing_cb_fence = ex_cb_fence_find(&s->cb);
    s->missing_clear = ex_cmd_clear_find(&s->cmd);
    s->missing_copy_tex = ex_cmd_copy_tex_find(&s->cmd);
    s->missing_copy_buf = ex_cmd_copy_buf_find(&s->cmd);
    s->missing_event = ex_cmd_event_find(&s->cmd);
    if (!s->missing_event) s->missing_event = ex_event_procs_find(&s->ev);
    s->missing_report = ex_cmd_report_find(&s->cmd);
    s->missing_buf = ex_buf_procs_find(&s->buf);
    s->missing_pool = ex_pool_procs_find(&s->pool_address, &s->pool_flush, &s->pool_invalidate);
    nvn_st_proc_find(&s->texture_finalize, "nvnTextureFinalize");

    if (s->missing_queue) {
        LOG("exectest: the queues are not made, %s is missing\n", s->missing_queue);
    } else if (!ex_queue_new(s, &s->q1, NULL) || !ex_queue_new(s, &s->q2, &s->q2_builder)) {
        LOG("exectest: a queue was not made\n");
    }
    if (!ex_pool_new(s, &s->tex_pool, NVN_ST_RES_POOL_CPU_CACHED, NVN_EX_TEX_POOL_BYTES)) {
        LOG("exectest: the texture pool was not made\n");
        return;
    }
    if (!ex_pool_new(s, &s->data_pool, NVN_ST_RES_POOL_CPU_CACHED, NVN_EX_DATA_POOL_BYTES)) {
        LOG("exectest: the data pool was not made\n");
        return;
    }
    if (!ex_pool_new(s, &s->cmd_pool, NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED, NVN_EX_CMD_POOL_BYTES)) {
        LOG("exectest: the command pool was not made\n");
        return;
    }
    if (!s->missing_buf) {
        if (!ex_buffer_new(s, &s->buf_a, &s->data_pool, NVN_EX_BUF_OFFSET_A) ||
            !ex_buffer_new(s, &s->buf_b, &s->data_pool, NVN_EX_BUF_OFFSET_B))
            LOG("exectest: a buffer was not made\n");
    }
    if (!s->missing_event) {
        if (!ex_event_new(s, &s->ev_a, &s->data_pool, NVN_EX_EVENT_OFFSET_A) ||
            !ex_event_new(s, &s->ev_b, &s->data_pool, NVN_EX_EVENT_OFFSET_B))
            LOG("exectest: an event was not made\n");
    }
    if (!ex_texture_new(s, &s->tex_a, &s->tex_pool, NVN_EX_TEX_OFFSET_A) ||
        !ex_texture_new(s, &s->tex_b, &s->tex_pool, NVN_EX_TEX_OFFSET_B) ||
        !ex_texture_new(s, &s->tex_c, &s->tex_pool, NVN_EX_TEX_OFFSET_C))
        LOG("exectest: a texture was not made\n");
    if (!s->missing_pool) s->data_gpu = EX_CALL(s, s->pool_address, s->data_pool.pool);
}

/* One drain step before the finalize. A fence with FLUSH_FOR_CPU posts every pending frame of the queue, and an empty
   frame takes the last posted value, so the wait covers all work submitted to the queue. A queue that cannot drain
   keeps its objects: a missing handler, a refused fence or a wait that runs out sets the stall flag. */
static void ex_drain(ExState* s) {
    if (s->stalled) return;
    const uint64_t queues[2] = {s->q1, s->q2};
    const char* const names[2] = {"q1 drain", "q2 drain"};
    for (unsigned i = 0; i < 2; i++) {
        if (!queues[i]) continue;
        if (!nvn_ex_drain_ok(s->missing_qops, s->missing_sync, queues[i])) {
            s->stalled = 1;
            LOG("exectest: cleanup cannot drain queue=%" PRIx64 ": %s is missing\n", queues[i],
                s->missing_qops ? s->missing_qops : s->missing_sync);
            continue;
        }
        const uint64_t sync = ex_sync_new(s);
        if (!sync) {
            s->stalled = 1;
            LOG("exectest: cleanup cannot drain queue=%" PRIx64 ": a sync was not made\n", queues[i]);
            continue;
        }
        ex_fence(s, queues[i], sync, NVN_EX_FENCE_FLUSH_FOR_CPU);
        const uint64_t wait = ex_wait(s, "cleanup", names[i], sync);
        if (nvn_ex_drain_done(wait) || nvn_ex_wait_stalled(wait)) continue; /* ex_wait logs a timeout */
        s->stalled = 1;
        LOG("exectest: cleanup drain of queue=%" PRIx64 " sync=%" PRIx64 " gave %" PRIu64 ", the fence was refused\n",
            queues[i], sync, wait);
    }
}

static void ex_cleanup(ExState* s) {
    ex_drain(s);
    for (unsigned i = 0; i < s->sync_count; i++)
        if (s->sync.finalize.host.fn) EX_CALL(s, s->sync.finalize, s->sync_list[i]);
    if (s->stalled) {
        /* The job that ran out of time replays its lists and reads its textures, buffers and events. The arena is
           never freed, so those objects stay valid without a finalize. */
        LOG("exectest: objects and pools kept, a GPU job did not end\n");
        return;
    }
    for (unsigned i = 0; i < s->cb_count; i++)
        if (s->cb.finalize.host.fn) EX_CALL(s, s->cb.finalize, s->cb_list[i]);
    if (s->texture_finalize.host.fn) {
        if (s->tex_a) EX_CALL(s, s->texture_finalize, s->tex_a);
        if (s->tex_b) EX_CALL(s, s->texture_finalize, s->tex_b);
        if (s->tex_c) EX_CALL(s, s->texture_finalize, s->tex_c);
    }
    if (s->buf.finalize.host.fn) {
        if (s->buf_a) EX_CALL(s, s->buf.finalize, s->buf_a);
        if (s->buf_b) EX_CALL(s, s->buf.finalize, s->buf_b);
    }
    if (s->ev.finalize.host.fn) {
        if (s->ev_a) EX_CALL(s, s->ev.finalize, s->ev_a);
        if (s->ev_b) EX_CALL(s, s->ev.finalize, s->ev_b);
    }
    if (s->cmd_pool.pool) nvn_st_res_pool_destroy(s->ctx.c, &s->ctx.cache, 1, &s->cmd_pool);
    if (s->data_pool.pool) nvn_st_res_pool_destroy(s->ctx.c, &s->ctx.cache, 1, &s->data_pool);
    if (s->tex_pool.pool) nvn_st_res_pool_destroy(s->ctx.c, &s->ctx.cache, 1, &s->tex_pool);
}

static int g_ran;

void nvn_exectest_run(CPU* c, uint64_t game_queue) {
    if (__atomic_exchange_n(&g_ran, 1, __ATOMIC_ACQ_REL)) return;
    if (!nvn_native_active()) {
        LOG("exectest: not run, nvn_native=0\n");
        return;
    }
    const uint64_t device = nvn_native_queue_device(game_queue);
    if (!device) {
        LOG("exectest: not run, the game queue has no host record\n");
        return;
    }
    const uint64_t base = kmem_alloc(NVN_EX_ARENA_BYTES, MS_NORMAL, PERM_RW);
    if (!base) {
        LOG("exectest: not run, the arena cannot be allocated\n");
        return;
    }
    memset(GP(base), 0, NVN_EX_ARENA_BYTES); /* the uncleared texture of the E6 mutation must be 0 */
    NvnStRun run = {0};
    nvn_st_arena_init(&run.arena, base, NVN_EX_ARENA_BYTES);
    ExState state = {0};
    state.ctx.c = c;
    state.ctx.run = &run;
    state.ctx.device = device;
    state.ctx.mutate = nvn_ex_case_from_name(g_settings.nvn_selftest_mutation);
    LOG("exectest: start, device=%" PRIx64 " game_queue=%" PRIx64 " arena=%" PRIx64 "+%x\n", state.ctx.device,
        game_queue, base, NVN_EX_ARENA_BYTES);
    ex_setup(&state);
    case_fence_order(&state);
    case_gpu_to_cpu(&state);
    case_cpu_to_gpu(&state);
    case_timeout(&state);
    case_event_report(&state);
    case_two_queues(&state);
    case_cb_fence_flush(&state);
    case_queue_reinit(&state);
    ex_cleanup(&state);
    LOG("exectest: %u cases, %u failed, %u skipped\n", run.cases, run.failed, run.skipped);
}
