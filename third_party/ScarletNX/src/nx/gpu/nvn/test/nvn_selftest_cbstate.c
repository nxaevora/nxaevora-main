/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The R cases of the d4 command buffer group (nvn-unobserved.md, "The selftest"): the CPU side of a command buffer,
   where the library writes the object and the control memory and the host keeps a record instead. Only the return
   values are compared: the getters, the exact EndRecording result outside a recording, the checkpoints and the
   refused memory getters. The object bytes and the control memory bytes differ by design, so no case compares them. */
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
#include "nx/gpu/nvn/test/nvn_selftest_mem.h"
#include "nx/memory/mem.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* The NVN part of a game command buffer is at most 0xE0 (docs/graphics/nvn-design.md); 0x200 is safe. */
#define CB_OBJ_BYTES 0x200u
#define CB_COMMAND_BYTES 0x1000u
#define CB_CONTROL_BYTES 0x800u
#define CB_CONTROL_ALIGN 0x1000u

#define CB_MEMORY_CALLBACK 0x1122334455667788ull
#define CB_MEMORY_CALLBACK_DATA 0x8877665544332211ull
#define CB_COPY_ROW_STRIDE 0x1400u
#define CB_COPY_IMAGE_STRIDE 0x96000u
#define CB_LINE_WIDTH_BITS 0x40000000u /* the bit pattern of 2.0f */
/* What a refused memory getter answers on the host (COMMAND_REFUSE_VALUE_HANDLER in nvn_native.c). */
#define CB_HOST_REFUSED_VALUE 0u

/* The functions of the group, resolved once. A missing name skips the cases that need it. */
enum {
    CBP_INITIALIZE,
    CBP_FINALIZE,
    CBP_ADD_COMMAND_MEMORY,
    CBP_ADD_CONTROL_MEMORY,
    CBP_BEGIN,
    CBP_END,
    CBP_SET_LINE_WIDTH,
    CBP_SET_MEMORY_CALLBACK,
    CBP_SET_MEMORY_CALLBACK_DATA,
    CBP_SET_COPY_ROW_STRIDE,
    CBP_SET_COPY_IMAGE_STRIDE,
    CBP_GET_MEMORY_CALLBACK,
    CBP_GET_MEMORY_CALLBACK_DATA,
    CBP_GET_COPY_ROW_STRIDE,
    CBP_GET_COPY_IMAGE_STRIDE,
    CBP_IS_RECORDING,
    CBP_GET_COMMAND_MEMORY_SIZE,
    CBP_GET_COMMAND_MEMORY_USED,
    CBP_GET_COMMAND_MEMORY_FREE,
    CBP_GET_CONTROL_MEMORY_USED,
    CBP_GET_CONTROL_MEMORY_FREE,
    CBP_INSERT_CHECKPOINT_DYNAMIC,
    CBP_INSERT_CHECKPOINT_STATIC,
    CBP_SET_DEBUG_LABEL,
    CBP_COUNT
};

#define CB_NEED(name) (1u << (name))

typedef struct {
    NvnStProc f[CBP_COUNT];
} CbProcs;

#define CB_SIDE(procs, host, name) ((host) ? &(procs)->f[name].host : &(procs)->f[name].lib)

/* The objects of one side: its command buffer, and when the case needs memory, its control block and memory pool. */
typedef struct {
    uint64_t cb, control;
    NvnStResPool pool;
    int initialized; /* Initialize answered 1, so Finalize must run */
} CbSide;

/* The steps of one side after Initialize. `change` is 1 on the host when the mutation names the case's setter; `param`
   is the getter or the checkpoint that a row selects. */
typedef void (*CbSteps)(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init, uint64_t change,
                        uint64_t param, NvnStRec* rec);

typedef struct {
    const char* name;
    const char* args;
    const char* setter; /* the setter whose mutation the case checks, NULL when the case has none */
    unsigned need;
    int needs_memory;
    int expect_noop;
    uint64_t param;
    CbSteps steps;
} CbCase;

/* Makes one side's command buffer, zeroed, with its memory pool and control block when the case needs them, then calls
   Initialize. 0 when the arena is full or a pool was not made. The Initialize result is in *init: the caller compares it. */
static int cb_side_make(CPU* c, NvnStRun* run, NvnStResCache* cache, const CbProcs* p, int host, int memory,
                        CbSide* s, uint64_t* init) {
    s->cb = nvn_st_arena_alloc(&run->arena, CB_OBJ_BYTES, 16);
    if (!s->cb) return 0;
    memset(GP(s->cb), 0, CB_OBJ_BYTES);
    if (memory) {
        s->control = nvn_st_arena_alloc(&run->arena, CB_CONTROL_BYTES, CB_CONTROL_ALIGN);
        if (!s->control) return 0;
        memset(GP(s->control), 0, CB_CONTROL_BYTES);
        if (!nvn_st_res_pool_create(c, run, cache, host, &s->pool, NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED,
                                    CB_COMMAND_BYTES))
            return 0;
    }
    *init = nvn_st_call(c, CB_SIDE(p, host, CBP_INITIALIZE), &(NvnStCall){.x = {s->cb, nvn_st_device()}});
    s->initialized = *init != 0;
    return 1;
}

/* Finalizes the command buffer and destroys its pool, also after a step that failed. */
static void cb_side_destroy(CPU* c, NvnStResCache* cache, const CbProcs* p, int host, int memory, const CbSide* s) {
    if (s->initialized) nvn_st_call(c, CB_SIDE(p, host, CBP_FINALIZE), &(NvnStCall){.x = {s->cb}});
    if (memory && s->pool.pool) nvn_st_res_pool_destroy(c, cache, host, &s->pool);
}

/* Both sides add their memory and start a recording. The command memory is the side's pool at offset 0. */
static void cb_open_recording(CPU* c, const CbProcs* p, int host, const CbSide* s) {
    nvn_st_call(c, CB_SIDE(p, host, CBP_ADD_COMMAND_MEMORY),
                &(NvnStCall){.x = {s->cb, s->pool.pool, 0, CB_COMMAND_BYTES}});
    nvn_st_call(c, CB_SIDE(p, host, CBP_ADD_CONTROL_MEMORY),
                &(NvnStCall){.x = {s->cb, s->control, CB_CONTROL_BYTES}});
    nvn_st_call(c, CB_SIDE(p, host, CBP_BEGIN), &(NvnStCall){.x = {s->cb}});
}

/* 2.0f to SetLineWidth, with the float in s0. */
static void cb_set_line_width(CPU* c, const CbProcs* p, int host, const CbSide* s) {
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_LINE_WIDTH),
                &(NvnStCall){.x = {s->cb}, .s = {CB_LINE_WIDTH_BITS}, .n_s = 1});
}

/* Row 1: the Initialize result and the five getters that read the object words. */
static void cb_step_initialize(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init, uint64_t change,
                               uint64_t param, NvnStRec* rec) {
    (void)change;
    (void)param;
    nvn_st_add_ret(rec, "nvnCommandBufferInitialize", init);
    nvn_st_add_ret(rec, "nvnCommandBufferGetMemoryCallback",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_MEMORY_CALLBACK), &(NvnStCall){.x = {s->cb}}));
    nvn_st_add_ret(rec, "nvnCommandBufferGetMemoryCallbackData",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_MEMORY_CALLBACK_DATA), &(NvnStCall){.x = {s->cb}}));
    nvn_st_add_ret(rec, "nvnCommandBufferGetCopyRowStride",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_COPY_ROW_STRIDE), &(NvnStCall){.x = {s->cb}}));
    nvn_st_add_ret(rec, "nvnCommandBufferGetCopyImageStride",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_COPY_IMAGE_STRIDE), &(NvnStCall){.x = {s->cb}}));
    nvn_st_add_ret(rec, "nvnCommandBufferIsRecording",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_IS_RECORDING), &(NvnStCall){.x = {s->cb}}));
}

/* Rows 2 to 5: one setter, then the getter that reads the same word. The mutation changes the host's setter value. */
static void cb_step_set_memory_callback(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                        uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_MEMORY_CALLBACK),
                &(NvnStCall){.x = {s->cb, CB_MEMORY_CALLBACK ^ change}});
    nvn_st_add_ret(rec, "nvnCommandBufferGetMemoryCallback",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_MEMORY_CALLBACK), &(NvnStCall){.x = {s->cb}}));
}

static void cb_step_set_memory_callback_data(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                             uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_MEMORY_CALLBACK_DATA),
                &(NvnStCall){.x = {s->cb, CB_MEMORY_CALLBACK_DATA ^ change}});
    nvn_st_add_ret(rec, "nvnCommandBufferGetMemoryCallbackData",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_MEMORY_CALLBACK_DATA), &(NvnStCall){.x = {s->cb}}));
}

static void cb_step_set_copy_row_stride(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                        uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_COPY_ROW_STRIDE),
                &(NvnStCall){.x = {s->cb, (uint32_t)(CB_COPY_ROW_STRIDE ^ change)}});
    nvn_st_add_ret(rec, "nvnCommandBufferGetCopyRowStride",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_COPY_ROW_STRIDE), &(NvnStCall){.x = {s->cb}}));
}

static void cb_step_set_copy_image_stride(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                          uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_COPY_IMAGE_STRIDE),
                &(NvnStCall){.x = {s->cb, (uint32_t)(CB_COPY_IMAGE_STRIDE ^ change)}});
    nvn_st_add_ret(rec, "nvnCommandBufferGetCopyImageStride",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_COPY_IMAGE_STRIDE), &(NvnStCall){.x = {s->cb}}));
}

/* Row 6: a recording is open, and IsRecording reads 1. */
static void cb_step_begin_recording(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                    uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    (void)param;
    cb_open_recording(c, p, host, s);
    nvn_st_add_ret(rec, "nvnCommandBufferIsRecording",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_IS_RECORDING), &(NvnStCall){.x = {s->cb}}));
}

/* Row 7: a recording with one call ends; its handle is nonzero, and IsRecording reads 0. */
static void cb_step_end_recording(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init, uint64_t change,
                                  uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    (void)param;
    cb_open_recording(c, p, host, s);
    cb_set_line_width(c, p, host, s);
    const uint64_t handle = nvn_st_call(c, CB_SIDE(p, host, CBP_END), &(NvnStCall){.x = {s->cb}});
    nvn_st_add_ret(rec, "nvnCommandBufferEndRecording", handle != 0 ? 1u : 0u);
    nvn_st_add_ret(rec, "nvnCommandBufferIsRecording",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_IS_RECORDING), &(NvnStCall){.x = {s->cb}}));
}

/* Row 8: EndRecording outside a recording. Both sides return the exact value 0. */
static void cb_step_end_not_recording(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                      uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_ADD_COMMAND_MEMORY),
                &(NvnStCall){.x = {s->cb, s->pool.pool, 0, CB_COMMAND_BYTES}});
    nvn_st_call(c, CB_SIDE(p, host, CBP_ADD_CONTROL_MEMORY),
                &(NvnStCall){.x = {s->cb, s->control, CB_CONTROL_BYTES}});
    nvn_st_add_ret(rec, "nvnCommandBufferEndRecording",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_END), &(NvnStCall){.x = {s->cb}}));
}

/* Rows 9 and 10: the checkpoint returns 1. The library body is `mov w0, #1; ret` and does not read the object. */
static void cb_step_checkpoint(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init, uint64_t change,
                               uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    nvn_st_add_ret(rec, p->f[param].name,
                   nvn_st_call(c, CB_SIDE(p, host, (unsigned)param), &(NvnStCall){.x = {s->cb, 0, 0}}));
}

/* Row 11: SetDebugLabel changes no state, so the row stride that was set before it is still there. */
static void cb_step_debug_label(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init, uint64_t change,
                                uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    (void)param;
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_COPY_ROW_STRIDE), &(NvnStCall){.x = {s->cb, CB_COPY_ROW_STRIDE}});
    nvn_st_call(c, CB_SIDE(p, host, CBP_SET_DEBUG_LABEL), &(NvnStCall){.x = {s->cb, 0}});
    nvn_st_add_ret(rec, "nvnCommandBufferGetCopyRowStride",
                   nvn_st_call(c, CB_SIDE(p, host, CBP_GET_COPY_ROW_STRIDE), &(NvnStCall){.x = {s->cb}}));
}

/* Row 12: a refused memory getter, read during an open recording. The host answers 0 by design. */
static void cb_step_refused_getter(CPU* c, const CbProcs* p, int host, const CbSide* s, uint64_t init,
                                   uint64_t change, uint64_t param, NvnStRec* rec) {
    (void)init;
    (void)change;
    cb_open_recording(c, p, host, s);
    cb_set_line_width(c, p, host, s);
    nvn_st_add_ret(rec, p->f[param].name,
                   nvn_st_call(c, CB_SIDE(p, host, (unsigned)param), &(NvnStCall){.x = {s->cb}}));
    nvn_st_call(c, CB_SIDE(p, host, CBP_END), &(NvnStCall){.x = {s->cb}});
}

/* The 16 cases. The setter rows 2 to 5 are the only ones with a mutation. The five refused getters expect their
   difference: a case with no difference would mean that the host changed. */
static const CbCase cb_cases[] = {
    {.name = "nvnCommandBufferInitialize",
     .args = "",
     .need = CB_NEED(CBP_GET_MEMORY_CALLBACK) | CB_NEED(CBP_GET_MEMORY_CALLBACK_DATA) |
             CB_NEED(CBP_GET_COPY_ROW_STRIDE) | CB_NEED(CBP_GET_COPY_IMAGE_STRIDE) | CB_NEED(CBP_IS_RECORDING),
     .steps = cb_step_initialize},
    {.name = "nvnCommandBufferSetMemoryCallback",
     .args = "",
     .setter = "nvnCommandBufferSetMemoryCallback",
     .need = CB_NEED(CBP_SET_MEMORY_CALLBACK) | CB_NEED(CBP_GET_MEMORY_CALLBACK),
     .steps = cb_step_set_memory_callback},
    {.name = "nvnCommandBufferSetMemoryCallbackData",
     .args = "",
     .setter = "nvnCommandBufferSetMemoryCallbackData",
     .need = CB_NEED(CBP_SET_MEMORY_CALLBACK_DATA) | CB_NEED(CBP_GET_MEMORY_CALLBACK_DATA),
     .steps = cb_step_set_memory_callback_data},
    {.name = "nvnCommandBufferSetCopyRowStride",
     .args = "",
     .setter = "nvnCommandBufferSetCopyRowStride",
     .need = CB_NEED(CBP_SET_COPY_ROW_STRIDE) | CB_NEED(CBP_GET_COPY_ROW_STRIDE),
     .steps = cb_step_set_copy_row_stride},
    {.name = "nvnCommandBufferSetCopyImageStride",
     .args = "",
     .setter = "nvnCommandBufferSetCopyImageStride",
     .need = CB_NEED(CBP_SET_COPY_IMAGE_STRIDE) | CB_NEED(CBP_GET_COPY_IMAGE_STRIDE),
     .steps = cb_step_set_copy_image_stride},
    {.name = "nvnCommandBufferBeginRecording",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_IS_RECORDING),
     .needs_memory = 1,
     .steps = cb_step_begin_recording},
    {.name = "nvnCommandBufferEndRecording",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_END) | CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_IS_RECORDING),
     .needs_memory = 1,
     .steps = cb_step_end_recording},
    {.name = "nvnCommandBufferEndRecording",
     .args = "not recording",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_END),
     .needs_memory = 1,
     .steps = cb_step_end_not_recording},
    {.name = "nvnCommandBufferInsertCheckpointDynamic",
     .args = "",
     .need = CB_NEED(CBP_INSERT_CHECKPOINT_DYNAMIC),
     .param = CBP_INSERT_CHECKPOINT_DYNAMIC,
     .steps = cb_step_checkpoint},
    {.name = "nvnCommandBufferInsertCheckpointStatic",
     .args = "",
     .need = CB_NEED(CBP_INSERT_CHECKPOINT_STATIC),
     .param = CBP_INSERT_CHECKPOINT_STATIC,
     .steps = cb_step_checkpoint},
    {.name = "nvnCommandBufferSetDebugLabel",
     .args = "",
     .need = CB_NEED(CBP_SET_COPY_ROW_STRIDE) | CB_NEED(CBP_SET_DEBUG_LABEL) | CB_NEED(CBP_GET_COPY_ROW_STRIDE),
     .steps = cb_step_debug_label},
    {.name = "nvnCommandBufferGetCommandMemorySize",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_END) | CB_NEED(CBP_GET_COMMAND_MEMORY_SIZE),
     .needs_memory = 1,
     .expect_noop = 1,
     .param = CBP_GET_COMMAND_MEMORY_SIZE,
     .steps = cb_step_refused_getter},
    {.name = "nvnCommandBufferGetCommandMemoryUsed",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_END) | CB_NEED(CBP_GET_COMMAND_MEMORY_USED),
     .needs_memory = 1,
     .expect_noop = 1,
     .param = CBP_GET_COMMAND_MEMORY_USED,
     .steps = cb_step_refused_getter},
    {.name = "nvnCommandBufferGetCommandMemoryFree",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_END) | CB_NEED(CBP_GET_COMMAND_MEMORY_FREE),
     .needs_memory = 1,
     .expect_noop = 1,
     .param = CBP_GET_COMMAND_MEMORY_FREE,
     .steps = cb_step_refused_getter},
    {.name = "nvnCommandBufferGetControlMemoryUsed",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_END) | CB_NEED(CBP_GET_CONTROL_MEMORY_USED),
     .needs_memory = 1,
     .expect_noop = 1,
     .param = CBP_GET_CONTROL_MEMORY_USED,
     .steps = cb_step_refused_getter},
    {.name = "nvnCommandBufferGetControlMemoryFree",
     .args = "",
     .need = CB_NEED(CBP_ADD_COMMAND_MEMORY) | CB_NEED(CBP_ADD_CONTROL_MEMORY) | CB_NEED(CBP_BEGIN) |
             CB_NEED(CBP_SET_LINE_WIDTH) | CB_NEED(CBP_END) | CB_NEED(CBP_GET_CONTROL_MEMORY_FREE),
     .needs_memory = 1,
     .expect_noop = 1,
     .param = CBP_GET_CONTROL_MEMORY_FREE,
     .steps = cb_step_refused_getter},
};

/* Initialize answered differently on the two sides: the case fails on the Initialize result, and its steps do not
   run, because they need an initialized command buffer on both sides. */
static void cb_report_init_mismatch(NvnStRun* run, const CbCase* row, const uint64_t init[2]) {
    NvnStRec ref = {0}, got = {0};
    nvn_st_add_ret(&ref, "nvnCommandBufferInitialize", init[0]);
    nvn_st_add_ret(&got, "nvnCommandBufferInitialize", init[1]);
    NvnStDiff diff;
    nvn_st_compare(&ref, &got, &diff);
    nvn_st_report(run, row->name, row->args, &ref, &diff);
}

/* A refused getter must still answer the refusal value on the host. Another host value is a fail even though it
   differs from the library, because the host handler then changed and the row must be checked again. */
static int cb_refusal_kept(NvnStRun* run, const CbCase* row, const NvnStRec* got) {
    if (got->n_ret == 1 && got->ret[0].value == CB_HOST_REFUSED_VALUE) return 1;
    run->cases++;
    run->failed++;
    LOG("selftest: %s expected the host refusal value %#x, host gave %#llx\n", row->name, CB_HOST_REFUSED_VALUE,
        got->n_ret ? (unsigned long long)got->ret[0].value : 0ull);
    return 0;
}

/* Runs the steps of one case on both sides and compares the two records. */
static void cb_run_steps(CPU* c, NvnStRun* run, const CbProcs* p, const CbCase* row, const CbSide side[2],
                         const uint64_t init[2]) {
    const int mutation = row->setter && !strcmp(g_settings.nvn_selftest_mutation, row->setter);
    NvnStRec ref = {0}, got = {0};
    for (int host = 0; host < 2; host++) {
        NvnStRec* const rec = host ? &got : &ref;
        const uint64_t change = mutation && host ? 1u : 0u;
        row->steps(c, p, host, &side[host], init[host], change, row->param, rec);
    }
    NvnStDiff diff;
    nvn_st_compare(&ref, &got, &diff);
    if (!row->expect_noop) {
        nvn_st_report(run, row->name, row->args, &ref, &diff);
        return;
    }
    if (cb_refusal_kept(run, row, &got)) nvn_st_report_noop(run, row->name, row->args, &ref, &diff);
}

/* Runs one case: a fresh command buffer on each side, the same steps, and the comparison of the two records. */
static void cb_case(CPU* c, NvnStRun* run, NvnStResCache* cache, const CbProcs* p, const CbCase* row) {
    CbSide side[2] = {{0}};
    uint64_t init[2] = {0};
    int ok = 1;
    for (int host = 0; host < 2 && ok; host++)
        ok = cb_side_make(c, run, cache, p, host, row->needs_memory, &side[host], &init[host]);
    if (!ok) {
        run->skipped++;
        LOG("selftest: %s skipped, a command buffer or its memory was not made\n", row->name);
    } else if (init[0] != init[1]) {
        cb_report_init_mismatch(run, row, init);
    } else if (!init[0]) {
        run->skipped++;
        LOG("selftest: %s skipped, Initialize answered 0 on both sides\n", row->name);
    } else {
        cb_run_steps(c, run, p, row, side, init);
    }
    for (int host = 0; host < 2; host++)
        cb_side_destroy(c, cache, p, host, row->needs_memory, &side[host]);
}

void nvn_selftest_command_state_cases(CPU* c, NvnStRun* run) {
    static const char* const names[CBP_COUNT] = {
        "nvnCommandBufferInitialize",
        "nvnCommandBufferFinalize",
        "nvnCommandBufferAddCommandMemory",
        "nvnCommandBufferAddControlMemory",
        "nvnCommandBufferBeginRecording",
        "nvnCommandBufferEndRecording",
        "nvnCommandBufferSetLineWidth",
        "nvnCommandBufferSetMemoryCallback",
        "nvnCommandBufferSetMemoryCallbackData",
        "nvnCommandBufferSetCopyRowStride",
        "nvnCommandBufferSetCopyImageStride",
        "nvnCommandBufferGetMemoryCallback",
        "nvnCommandBufferGetMemoryCallbackData",
        "nvnCommandBufferGetCopyRowStride",
        "nvnCommandBufferGetCopyImageStride",
        "nvnCommandBufferIsRecording",
        "nvnCommandBufferGetCommandMemorySize",
        "nvnCommandBufferGetCommandMemoryUsed",
        "nvnCommandBufferGetCommandMemoryFree",
        "nvnCommandBufferGetControlMemoryUsed",
        "nvnCommandBufferGetControlMemoryFree",
        "nvnCommandBufferInsertCheckpointDynamic",
        "nvnCommandBufferInsertCheckpointStatic",
        "nvnCommandBufferSetDebugLabel",
    };
    NvnStResCache cache = {0};
    CbProcs procs = {0};
    for (unsigned i = 0; i < CBP_COUNT; i++) nvn_st_proc_find(&procs.f[i], names[i]);
    for (size_t i = 0; i < ARRAY_LEN(cb_cases); i++) {
        const CbCase* const row = &cb_cases[i];
        const unsigned need = row->need | CB_NEED(CBP_INITIALIZE) | CB_NEED(CBP_FINALIZE);
        const char* missing = NULL;
        for (unsigned k = 0; k < CBP_COUNT && !missing; k++)
            if ((need & (1u << k)) && (!procs.f[k].lib.fn || !procs.f[k].host.fn)) missing = names[k];
        if (missing) {
            run->skipped++;
            LOG("selftest: %s skipped, %s was not resolved\n", row->name, missing);
            continue;
        }
        cb_case(c, run, &cache, &procs, row);
    }
}
