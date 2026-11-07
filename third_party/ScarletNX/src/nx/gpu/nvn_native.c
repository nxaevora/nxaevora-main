/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* nvn_native=1: the host answers the NVN functions that the game resolves through nvnBootstrapLoader and
   nvnDeviceGetProcAddress. Each resolved address gets a host handler in its dispatch slot. A function without a
   handler logs its first call and returns 0. The hook order and the trace are in docs/nvn-design.md, 2-3 and 2-6. */
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/nvn_cmdlist.h"
#include "nx/gpu/nvn_frame.h"
#include "nx/gpu/nvn_objtab.h"
#include "nx/gpu/nvn_params.h"
#include "nx/gpu/nvn_rt.h"
#include "nx/gpu/nvn_trace.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include "nx/gpu/renderer.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
#include "nx/runtime/cheats.h"
#include "nx/runtime/options.h"
#include "nx/runtime/rtapi.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/tracy.h"
#include <stdlib.h>
#include <string.h>

/* The host's records of the NVN objects. The game's object memory is never written (docs/nvn-design.md, 2-1). Handlers
   take objs_lock for the whole call, and their pointers into a record end with the call. */
static NvnObjTab objs;
static PMutex objs_lock = PMUTEX_INIT;
/* The frame job tells the guest waits that it freed an image and updated the syncs. */
static PCond frame_done_cv;
static NvnListStore lists; /* the command lists of the game, guarded by objs_lock */
/* The frame line counters (3c decision 12). The three guarded by objs_lock are read and written with it; the other two
   are atomic because missing_handler and the recorder run without it. */
static uint32_t frame_acquires, frame_submitted_lists, frame_skipped_lists;
static uint32_t missing_calls, dropped_ops;

typedef struct {
    uint32_t flags;
} DeviceBuilderRec;
typedef struct {
    uint32_t flags, window_origin, depth_mode;
} DeviceRec;
typedef struct {
    NvnQueueBuilder builder; /* the builder as it was at QueueInitialize */
    NvnPendingFrame pending; /* the frame that Present closes */
    uint32_t channel_id;     /* the syncpoint id of the private channel of the frame jobs */
    GpuChannel* channel;     /* the private channel of the frame jobs, made at QueueInitialize */
    uint32_t* init_words;    /* the macro code that the library builder wrote at QueueInitialize, malloc'd */
    uint32_t init_word_count;
    uint8_t init_replayed; /* a frame job took the words: the record hands them out once */
} QueueRec;
typedef struct {
    uint64_t device, native_window, textures[NVN_WINDOW_MAX_TEXTURES];
    uint32_t texture_count, interval;
    NvnWindowCrop crop;
    uint8_t states[NVN_WINDOW_MAX_TEXTURES];
    int32_t last_index;
} WindowRec;
typedef struct { uint64_t device; NvnSyncState state; uint32_t fences; } SyncRec;
/* The program records: the shader header stays in the game's memory, and the host copies the small control block.
   SetShadersExt validates all entries before it replaces the table. */
typedef struct {
    uint64_t header;     /* the guest address of the shader header */
    uint64_t entry_word; /* the +0 word of the entry that named the shader */
    uint8_t control[0x18];
    uint8_t has_control;
} ProgramStageRec;
typedef struct {
    uint64_t device;
    uint32_t device_generation;
    uint32_t stage_mask;
    ProgramStageRec stage[NVN_PROGRAM_STAGES];
    uint32_t word_count;
    uint32_t words[NVN_PROGRAM_WORDS_MAX];
} ProgramRec;

/* the record of kind at addr, made zeroed (with the defaults of that kind) when the game never called SetDefaults */
static void* rec_get(u64 addr, NvnObjKind kind, size_t size) {
    NvnObj* o = nvn_objtab_find(&objs, addr);
    if (o && o->kind == kind) return o->data;
    o = nvn_objtab_put(&objs, addr, kind, size);
    if (!o) fatal("nvn-native: out of memory for the record of an NVN object (%llx)\n", (unsigned long long)addr);
    if (kind == NVN_OBJ_QUEUE_BUILDER) nvn_queue_builder_defaults(o->data);
    if (kind == NVN_OBJ_MEMORY_POOL_BUILDER) nvn_memory_pool_builder_defaults(o->data);
    if (kind == NVN_OBJ_SAMPLER_BUILDER) nvn_sampler_defaults(o->data);
    if (kind == NVN_OBJ_TEXTURE_BUILDER) nvn_texture_builder_defaults(o->data);
    if (kind == NVN_OBJ_WINDOW_BUILDER) nvn_window_builder_defaults(o->data);
    /* a state object that the game never gave to SetDefaults starts with the library's defaults */
    if (kind == NVN_OBJ_COLOR_STATE) nvn_color_state_defaults(o->data);
    if (kind == NVN_OBJ_DEPTH_STENCIL_STATE) nvn_depth_stencil_state_defaults(o->data);
    if (kind == NVN_OBJ_BLEND_STATE) nvn_blend_state_defaults(o->data);
    if (kind == NVN_OBJ_POLYGON_STATE) nvn_polygon_state_defaults(o->data);
    if (kind == NVN_OBJ_MULTISAMPLE_STATE) nvn_multisample_state_defaults(o->data);
    if (kind == NVN_OBJ_CHANNEL_MASK_STATE) nvn_channel_mask_state_defaults(o->data);
    return o->data;
}
static void* known_record(u64 addr, NvnObjKind kind);
static int native_guest_readable(u64 addr, u64 size);
static int native_guest_writable(u64 addr, u64 size);
static NvnQueueBuilder* queue_builder(u64 addr) { return rec_get(addr, NVN_OBJ_QUEUE_BUILDER, sizeof(NvnQueueBuilder)); }

/* G1: device builder and device (docs/nvn-design.md, 2-6). The library's functions are plain field accesses. */
static void h_device_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    DeviceBuilderRec* b = rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof *b);
    b->flags = 0;
    pm_unlock(&objs_lock);
}
static void h_device_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    ((DeviceBuilderRec*)rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof(DeviceBuilderRec)))->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_builder_get_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = ((DeviceBuilderRec*)rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof(DeviceBuilderRec)))->flags;
    pm_unlock(&objs_lock);
}
/* nvnDeviceInitialize(device, builder): the host has no device to set up, so it records the flags and says yes */
static void h_device_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    DeviceBuilderRec* b = rec_get(c->x[1], NVN_OBJ_DEVICE_BUILDER, sizeof *b);
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_DEVICE, sizeof(DeviceRec));
    if (!o) fatal("nvn-native: out of memory for the record of the device\n");
    ((DeviceRec*)o->data)->flags = b->flags;
    pm_unlock(&objs_lock);
    c->x[0] = 1;
}
static DeviceRec* device_rec(u64 addr) { return rec_get(addr, NVN_OBJ_DEVICE, sizeof(DeviceRec)); }
static void h_device_set_window_origin_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    device_rec(c->x[0])->window_origin = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_get_window_origin_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = device_rec(c->x[0])->window_origin;
    pm_unlock(&objs_lock);
}
static void h_device_set_depth_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    device_rec(c->x[0])->depth_mode = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_get_depth_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = device_rec(c->x[0])->depth_mode;
    pm_unlock(&objs_lock);
}

/* nvnDeviceGetInteger(device, what, int* out). A `what` that the library answers but no run asked for writes nothing. */
static void h_device_get_integer(CPU* c, u64 entry) {
    (void)entry;
    u32 what = (u32)c->x[1];
    int32_t value;
    if (!nvn_device_integer(what, &value)) {
        LOG_LIMIT(32, "nvn-native: nvnDeviceGetInteger what=%u has no known value, nothing written\n", what);
        return;
    }
    gw32(c->x[2], (u32)value);
}
/* nvnDeviceGetTimestampInNanoseconds(device, counter): the counter's tick count is at +8 */
static void h_device_get_timestamp_ns(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_timestamp_ns(gr64(c->x[1] + 8));
}
/* nvnDeviceGetTextureHandle(device, texture index, sampler index) and nvnDeviceGetImageHandle(device, image index): pure. */
static void h_device_get_texture_handle(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_texture_handle((u32)c->x[1], (u32)c->x[2]);
}
static void h_device_get_image_handle(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_image_handle((u32)c->x[1]);
}

/* G1: queue builder and queue */
static void h_queue_builder_set_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->device = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->device;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    nvn_queue_builder_defaults(queue_builder(c->x[0]));
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->flags;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_priority(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->priority = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_priority(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->priority;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_queue_memory(CPU* c, u64 entry) { /* (builder, memory, size) */
    (void)entry;
    pm_lock(&objs_lock);
    NvnQueueBuilder* b = queue_builder(c->x[0]);
    b->memory = c->x[1];
    b->memory_size = (u32)c->x[2];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_memory(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->memory;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_memory_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->memory_size;
    pm_unlock(&objs_lock);
}
/* The three memory sizes and the flush threshold have a setter that marks the value as set, and a getter that writes
   the value through a pointer and returns whether it was set. */
#define QUEUE_BUILDER_SIZE_HANDLERS(name, field, has)                                  \
    static void h_queue_builder_set_##name(CPU* c, u64 entry) {                        \
        (void)entry;                                                                   \
        pm_lock(&objs_lock);                                                           \
        NvnQueueBuilder* b = queue_builder(c->x[0]);                                   \
        b->has = 1;                                                                    \
        b->field = (int32_t)c->x[1];                                                   \
        pm_unlock(&objs_lock);                                                         \
    }                                                                                  \
    static void h_queue_builder_get_##name(CPU* c, u64 entry) {                        \
        (void)entry;                                                                   \
        pm_lock(&objs_lock);                                                           \
        NvnQueueBuilder* b = queue_builder(c->x[0]);                                   \
        gw32(c->x[1], (u32)b->field);                                                  \
        c->x[0] = b->has;                                                              \
        pm_unlock(&objs_lock);                                                         \
    }
QUEUE_BUILDER_SIZE_HANDLERS(command_memory_size, command, has_command)
QUEUE_BUILDER_SIZE_HANDLERS(compute_memory_size, compute, has_compute)
QUEUE_BUILDER_SIZE_HANDLERS(control_memory_size, control, has_control)
QUEUE_BUILDER_SIZE_HANDLERS(command_flush_threshold, flush_threshold, has_flush_threshold)

static void h_queue_builder_get_queue_memory_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = nvn_queue_memory_size(queue_builder(c->x[0]), &nvn_queue_device_words);
    pm_unlock(&objs_lock);
}
/* The library builder of QueueInitialize writes the macro code of the private channel. The host replaces
   QueueInitialize, so it runs the builder on a scratch image below the game thread's stack and keeps the words for
   the first frame job. The address is the sdk address of this game version. */
#define NVN_QUEUE_BUILDER_ADDRESS 0xD84F800ull
#define NVN_BUILDER_ALIGN 16u
#define NVN_BUILDER_STACK_BYTES 0x90u    /* the builder's stack use */
#define NVN_BUILDER_QUEUE_BYTES 0x1740u  /* the queue image: the reserve granularity at +0x1734 is its last field */
#define NVN_BUILDER_DEVICE_BYTES 0x100u  /* the device image: the form byte is at +0xB0 */
#define NVN_BUILDER_COMMAND_BYTES 0x1000u
#define NVN_BUILDER_RED_ZONE 0x200u /* the gap between the scratch layout and the old stack pointer */
#define NVN_BUILDER_RESERVE_WORDS 12u
#define NVN_BUILDER_DEVICE_FORM 0u /* bit 4 = 0 is proven by the run hash, bit 0 = 0 is not proven */
#define NVN_BUILDER_RETURN_LR 0x4E5640ull /* a known, word-aligned return address outside the code */
#define NVN_QUEUE_DEVICE_OFFSET 0x00u
#define NVN_QUEUE_CURSOR_OFFSET 0x16D8u
#define NVN_QUEUE_END_OFFSET 0x16E0u
#define NVN_QUEUE_RESERVE_OFFSET 0x1734u
#define NVN_BUILDER_DEVICE_FORM_OFFSET 0xB0u
#define NVN_BUILDER_PARTS_BYTES                                                                            \
    (NVN_BUILDER_STACK_BYTES + NVN_BUILDER_QUEUE_BYTES + NVN_BUILDER_DEVICE_BYTES + NVN_BUILDER_COMMAND_BYTES)
#define NVN_BUILDER_LAYOUT_BYTES (NVN_BUILDER_RED_ZONE + NVN_BUILDER_PARTS_BYTES)

/* The scratch image of the builder below the game thread's stack pointer. */
typedef struct {
    u64 low, queue, device, command;
} BuilderScratch;

static int builder_scratch(u64 sp, BuilderScratch* s) {
    const u64 total = NVN_BUILDER_LAYOUT_BYTES;
    if (sp < total) {
        LOG("nvn-native: queue builder scratch: sp=%llx is below the %llu byte layout\n", (unsigned long long)sp,
            (unsigned long long)total);
        return 0;
    }
    u64 low = (sp - total) & ~(u64)(NVN_BUILDER_ALIGN - 1u);
    u64 size = NVN_BUILDER_PARTS_BYTES;
    if (!native_guest_readable(low, size) || !native_guest_writable(low, size)) {
        LOG("nvn-native: queue builder scratch: %llx+%llu is not readable and writable\n", (unsigned long long)low,
            (unsigned long long)size);
        return 0;
    }
    s->low = low;
    s->queue = low + NVN_BUILDER_STACK_BYTES; /* the builder's stack is [low, queue) */
    s->device = s->queue + NVN_BUILDER_QUEUE_BYTES;
    s->command = s->device + NVN_BUILDER_DEVICE_BYTES;
    return 1;
}

/* Zeroes a guest range in 8-byte writes; the images here are multiples of 16 bytes. */
static void builder_zero(u64 addr, u64 size) {
    for (u64 at = 0; at < size; at += 8u) gw64(addr + at, 0);
}

static void builder_images(const BuilderScratch* s) {
    builder_zero(s->queue, NVN_BUILDER_QUEUE_BYTES);
    builder_zero(s->device, NVN_BUILDER_DEVICE_BYTES);
    gw64(s->queue + NVN_QUEUE_DEVICE_OFFSET, s->device);
    gw64(s->queue + NVN_QUEUE_CURSOR_OFFSET, s->command);
    gw64(s->queue + NVN_QUEUE_END_OFFSET, s->command + NVN_BUILDER_COMMAND_BYTES);
    gw32(s->queue + NVN_QUEUE_RESERVE_OFFSET, NVN_BUILDER_RESERVE_WORDS);
    gw16(s->device + NVN_BUILDER_DEVICE_FORM_OFFSET, NVN_BUILDER_DEVICE_FORM);
}

/* Calls the builder with x0 = queue image and w1 = 0. The callee keeps x19..x29 and returns 1 in w0. */
static int builder_call(CPU* c, const BuilderScratch* s) {
    if (NVN_QUEUE_BUILDER_ADDRESS < g_code_lo || NVN_QUEUE_BUILDER_ADDRESS >= g_code_hi ||
        (NVN_QUEUE_BUILDER_ADDRESS & 3u)) {
        LOG("nvn-native: queue builder address=%llx is outside [%llx, %llx)\n",
            (unsigned long long)NVN_QUEUE_BUILDER_ADDRESS, (unsigned long long)g_code_lo,
            (unsigned long long)g_code_hi);
        return 0;
    }
    GuestFn fn = g_disp[(NVN_QUEUE_BUILDER_ADDRESS - g_code_lo) / 4u];
    if (!fn) fn = g_rtapi.miss; /* an empty slot, as trace_proc uses */
    const u64 old_sp = c->x[31], old_lr = c->x[30];
    c->x[0] = s->queue;
    c->x[1] = 0;
    c->x[30] = NVN_BUILDER_RETURN_LR;
    c->x[31] = s->queue; /* the builder's stack grows down into [low, queue) */
    fn(c, NVN_QUEUE_BUILDER_ADDRESS);
    const int ok = c->x[0] == 1 && c->pc == NVN_BUILDER_RETURN_LR;
    c->x[30] = old_lr;
    c->x[31] = old_sp;
    if (!ok)
        LOG("nvn-native: queue builder returned w0=%llx pc=%llx\n", (unsigned long long)c->x[0],
            (unsigned long long)c->pc);
    return ok;
}

/* Copies the command words that the builder left at command up to cursor into a host array. */
static int builder_read_words(u64 command, u64 cursor, uint32_t** out, uint32_t* out_count) {
    *out = NULL;
    *out_count = 0;
    if (cursor < command || cursor > command + NVN_BUILDER_COMMAND_BYTES || ((cursor - command) & 3u)) {
        LOG("nvn-native: queue builder cursor=%llx is outside the command buffer=%llx\n",
            (unsigned long long)cursor, (unsigned long long)command);
        return 0;
    }
    const uint32_t count = (uint32_t)((cursor - command) / 4u);
    if (!count) return 1;
    uint32_t* words = malloc((size_t)count * sizeof *words);
    if (!words) fatal("nvn-native: out of memory for the macro code of a queue\n");
    for (uint32_t i = 0; i < count; i++) words[i] = gr32(command + (u64)i * 4u);
    *out = words;
    *out_count = count;
    return 1;
}

/* Runs the builder on the scratch image. Returns its words (malloc'd), or NULL with *count 0 after a log line. */
static uint32_t* queue_builder_words(CPU* c, uint32_t* count) {
    *count = 0;
    BuilderScratch s;
    if (!builder_scratch(c->x[31], &s)) return NULL;
    builder_images(&s);
    if (!builder_call(c, &s)) return NULL;
    const u64 cursor = gr64(s.queue + NVN_QUEUE_CURSOR_OFFSET);
    uint32_t* words = NULL;
    if (!builder_read_words(s.command, cursor, &words, count)) return NULL;
    LOG("nvn-native: queue builder words=%u cursor=%llx\n", *count, (unsigned long long)cursor);
    return words;
}

/* nvnQueueInitialize(queue, builder): runs the library builder, records the builder and opens the private channel of
   the frame jobs. A failed builder stage leaves the macro code empty. */
static void h_queue_initialize(CPU* c, u64 entry) {
    (void)entry;
    const u64 queue_addr = c->x[0]; /* the builder call may change x0 */
    pm_lock(&objs_lock);
    NvnQueueBuilder b = *queue_builder(c->x[1]);
    pm_unlock(&objs_lock);
    uint32_t word_count = 0;
    uint32_t* words = queue_builder_words(c, &word_count); /* runs guest code, so no objs_lock */
    pm_lock(&objs_lock);
    QueueRec* old = known_record(queue_addr, NVN_OBJ_QUEUE);
    uint32_t channel_id = 0;
    GpuChannel* channel = NULL;
    if (old) { /* initialized again without Finalize */
        nvn_frame_release(&old->pending, &lists);
        channel_id = old->channel_id;
        channel = old->channel;
        free(old->init_words);
    }
    /* QueueFinalize is not observed and has no handler: the channel lives to the end of the process. */
    if (!channel) {
        channel_id = syncpt_alloc();
        channel = gpu_channel_open(channel_id);
    }
    NvnObj* o = nvn_objtab_put(&objs, queue_addr, NVN_OBJ_QUEUE, sizeof(QueueRec));
    if (!o) fatal("nvn-native: out of memory for the record of a queue\n");
    QueueRec* q = o->data;
    q->builder = b;
    q->pending = (NvnPendingFrame){0};
    q->channel_id = channel_id;
    q->channel = channel;
    q->init_words = words;
    q->init_word_count = word_count;
    pm_unlock(&objs_lock);
    c->x[0] = 1;
}
#define QUEUE_LOG_LIMIT 32u /* log lines for faults of the game in the queue functions */
/* Caller holds objs_lock. */
static QueueRec* queue_rec(u64 addr, const char* operation) {
    QueueRec* q = known_record(addr, NVN_OBJ_QUEUE);
    if (!q) LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown queue=%llx\n", operation, (unsigned long long)addr);
    return q;
}

/* G2 and G5: pool state stays in host records. A pool maps its storage at its GPU address from Initialize to
   Finalize (nvn_gpu_map_range), so the renderer reads the pool by GPU address. */
typedef struct {
    uint64_t device, storage, size, gpu_address;
    uint32_t flags;
} MemoryPoolRec;
typedef struct {
    uint64_t memory_pool, offset;
    uint32_t count;
} DescriptorPoolRec;
static uint64_t pool_gpu_cursor = NVN_POOL_GPU_BASE;

static NvnMemoryPoolBuilder* memory_pool_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL_BUILDER, sizeof(NvnMemoryPoolBuilder));
}
static MemoryPoolRec* memory_pool_rec(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL, sizeof(MemoryPoolRec));
}
static void h_memory_pool_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    nvn_memory_pool_builder_defaults(memory_pool_builder(c->x[0]));
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    memory_pool_builder(c->x[0])->device = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    memory_pool_builder(c->x[0])->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_storage(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnMemoryPoolBuilder* b = memory_pool_builder(c->x[0]);
    b->storage = c->x[1];
    b->size = c->x[2];
    b->handle = 0;
    pm_unlock(&objs_lock);
}
static void pool_initialize_error(NvnPoolResult result, u64 pool, const NvnMemoryPoolBuilder* b) {
    if (result == NVN_POOL_UNSUPPORTED_VIRTUAL)
        LOG("nvn-native: virtual memory pool not supported\n");
    else if (result == NVN_POOL_UNSUPPORTED_IMPORT)
        LOG("nvn-native: imported memory pool not supported\n");
    else
        LOG("nvn-native: MemoryPoolInitialize pool=%llx storage=%llx size=%llx flags=%x check=%u\n",
            (unsigned long long)pool, (unsigned long long)b->storage, (unsigned long long)b->size,
            b->flags, (unsigned)result);
}
static void h_memory_pool_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnMemoryPoolBuilder b = *memory_pool_builder(c->x[1]);
    uint32_t flags;
    NvnPoolResult result = nvn_memory_pool_params(&b, NVN_POOL_PAGE_SIZE, &flags);
    /* A failed initialization removes the record, so later calls find no pool. */
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_MEMORY_POOL, sizeof(MemoryPoolRec));
    if (!o) fatal("nvn-native: out of memory for the record of memory pool %llx\n", (unsigned long long)c->x[0]);
    if (result != NVN_POOL_OK) {
        pool_initialize_error(result, c->x[0], &b);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    uint64_t address, next;
    if (!nvn_pool_gpu_allocate(pool_gpu_cursor, b.size, NVN_POOL_PAGE_SIZE, &address, &next)) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx size=%llx GPU address allocation failed\n",
            (unsigned long long)c->x[0], (unsigned long long)b.size);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    if (!nvn_gpu_map_range(address, b.storage, b.size)) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx GPU range %llx size=%llx storage=%llx cannot be mapped\n",
            (unsigned long long)c->x[0], (unsigned long long)address, (unsigned long long)b.size,
            (unsigned long long)b.storage);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    MemoryPoolRec* p = o->data;
    *p = (MemoryPoolRec){.device = b.device, .storage = b.storage, .size = b.size,
                         .gpu_address = address, .flags = flags};
    pool_gpu_cursor = next;
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}

static void h_memory_pool_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    if (o && o->kind == NVN_OBJ_MEMORY_POOL) {
        const MemoryPoolRec* p = o->data;
        nvn_gpu_unmap_range(p->gpu_address, p->storage, p->size);
        nvn_objtab_remove(&objs, c->x[0]);
    }
    pm_unlock(&objs_lock);
}
#define MEMORY_POOL_GETTER(name, expression)                                        \
    static void h_memory_pool_##name(CPU* c, u64 entry) {                           \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        MemoryPoolRec* p = memory_pool_rec(c->x[0]);                                \
        c->x[0] = (expression);                                                     \
        pm_unlock(&objs_lock);                                                      \
    }
MEMORY_POOL_GETTER(map, nvn_memory_pool_map(p->flags, p->storage))
MEMORY_POOL_GETTER(get_flags, nvn_memory_pool_flags(p->flags))
MEMORY_POOL_GETTER(get_buffer_address, p->gpu_address)

#define DESCRIPTOR_POOL_HANDLERS(name, kind)                                        \
    static void h_##name##_pool_initialize(CPU* c, u64 entry) {                     \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        DescriptorPoolRec* p = rec_get(c->x[0], kind, sizeof *p);                   \
        *p = (DescriptorPoolRec){c->x[1], c->x[2], (u32)c->x[3]};                   \
        c->x[0] = 1;                                                                \
        pm_unlock(&objs_lock);                                                      \
    }                                                                               \
    static void h_##name##_pool_get_size(CPU* c, u64 entry) {                       \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        DescriptorPoolRec* p = rec_get(c->x[0], kind, sizeof *p);                   \
        c->x[0] = p->count;                                                         \
        pm_unlock(&objs_lock);                                                      \
    }
DESCRIPTOR_POOL_HANDLERS(texture, NVN_OBJ_TEXTURE_POOL)
DESCRIPTOR_POOL_HANDLERS(sampler, NVN_OBJ_SAMPLER_POOL)

/* Buffers (G7, decision 1 of the 3c design): a buffer holds its pool (address and generation), offset and size. */
typedef struct { uint64_t device, pool, offset, size; } BufferBuilderRec;
typedef struct { uint64_t pool, offset, size; uint32_t pool_generation; } BufferRec;
#define BUFFER_LOSS_LOG_LIMIT 8u /* a buffer used after its pool or itself ended is a game fault: log the first calls */
static uint32_t buffer_losses;

static BufferBuilderRec* buffer_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_BUFFER_BUILDER, sizeof(BufferBuilderRec));
}
static void buffer_lost(const char* operation, u64 buffer) {
    if (buffer_losses++ < BUFFER_LOSS_LOG_LIMIT)
        LOG("nvn-native: %s buffer=%llx has no record or its pool ended, returns 0\n", operation,
            (unsigned long long)buffer);
}
/* the pool of the buffer at addr, with the buffer in *out; NULL (and a log line) when either is gone */
static MemoryPoolRec* buffer_pool(u64 addr, const char* operation, const BufferRec** out) {
    const BufferRec* b = known_record(addr, NVN_OBJ_BUFFER);
    NvnObj* o = b ? nvn_objtab_find(&objs, b->pool) : NULL;
    if (!o || o->kind != NVN_OBJ_MEMORY_POOL || o->generation != b->pool_generation) {
        buffer_lost(operation, addr);
        return NULL;
    }
    *out = b;
    return o->data;
}
static void h_buffer_builder_set_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    buffer_builder(c->x[0])->device = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_buffer_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    BufferBuilderRec* b = buffer_builder(c->x[0]);
    b->pool = b->offset = b->size = 0; /* the device stays, as in the library */
    pm_unlock(&objs_lock);
}
static void h_buffer_builder_set_storage(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    BufferBuilderRec* b = buffer_builder(c->x[0]);
    b->pool = c->x[1];
    b->offset = c->x[2];
    b->size = c->x[3];
    pm_unlock(&objs_lock);
}
static void h_buffer_builder_get_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = buffer_builder(c->x[0])->size;
    pm_unlock(&objs_lock);
}
static int buffer_initialize(BufferRec* r, const BufferBuilderRec* b) {
    NvnObj* o = nvn_objtab_find(&objs, b->pool);
    if (!o || o->kind != NVN_OBJ_MEMORY_POOL) {
        LOG("nvn-native: buffer initialization %s pool=%llx\n", b->pool ? "unknown" : "null",
            (unsigned long long)b->pool);
        return 0;
    }
    const MemoryPoolRec* pool = o->data;
    NvnBufferResult check = nvn_buffer_check(pool->flags, pool->size, b->offset, b->size);
    if (check == NVN_BUFFER_PHYSICAL_POOL) {
        LOG("nvn-native: buffer initialization physical pool=%llx flags=%x\n", (unsigned long long)b->pool,
            pool->flags);
        return 0;
    }
    if (check == NVN_BUFFER_OUT_OF_POOL) {
        LOG("nvn-native: buffer initialization pool=%llx size=%llx: offset=%llx size=%llx does not fit\n",
            (unsigned long long)b->pool, (unsigned long long)pool->size, (unsigned long long)b->offset,
            (unsigned long long)b->size);
        return 0;
    }
    *r = (BufferRec){.pool = b->pool, .offset = b->offset, .size = b->size, .pool_generation = o->generation};
    return 1;
}
static void h_buffer_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    BufferBuilderRec b = *buffer_builder(c->x[1]);
    u64 addr = c->x[0];
    BufferRec* r = rec_get(addr, NVN_OBJ_BUFFER, sizeof *r);
    memset(r, 0, sizeof *r);
    int ok = buffer_initialize(r, &b);
    if (!ok) nvn_objtab_remove(&objs, addr); /* later calls must not find a half-made buffer */
    c->x[0] = ok;
    pm_unlock(&objs_lock);
}
static void h_buffer_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    if (o && o->kind == NVN_OBJ_BUFFER) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
static void h_buffer_map(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const BufferRec* b = NULL;
    const MemoryPoolRec* pool = buffer_pool(c->x[0], "BufferMap", &b);
    u64 base = pool ? nvn_memory_pool_map(pool->flags, pool->storage) : 0;
    c->x[0] = base ? base + b->offset : 0; /* 0 for a pool without CPU access, as in the library */
    pm_unlock(&objs_lock);
}
static void h_buffer_get_address(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const BufferRec* b = NULL;
    const MemoryPoolRec* pool = buffer_pool(c->x[0], "BufferGetAddress", &b);
    c->x[0] = pool ? pool->gpu_address + b->offset : 0;
    pm_unlock(&objs_lock);
}
static void h_buffer_get_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const BufferRec* b = known_record(c->x[0], NVN_OBJ_BUFFER);
    if (!b) buffer_lost("BufferGetSize", c->x[0]);
    c->x[0] = b ? b->size : 0;
    pm_unlock(&objs_lock);
}
static void h_buffer_set_debug_label(CPU* c, u64 entry) {
    (void)c;
    (void)entry; /* the library stores no label */
}
/* Every other SetDebugLabel is one `ret` in the library. */
static void h_set_debug_label(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}

/* G3 and G5: the sampler and texture records are independent of the guest allocation sizes. */
typedef NvnTexture TextureRec;
static NvnSampler* sampler_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_SAMPLER_BUILDER, sizeof(NvnSampler));
}
static NvnTextureBuilder* texture_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_TEXTURE_BUILDER, sizeof(NvnTextureBuilder));
}
static NvnTextureView* texture_view(u64 addr) {
    return rec_get(addr, NVN_OBJ_TEXTURE_VIEW, sizeof(NvnTextureView));
}
/* Check each memory region for the permission before using the guest access helpers. */
static int native_guest_access(u64 addr, u64 size, int perm) {
    if (!addr || addr >= AS_SIZE || size > AS_SIZE - addr) return 0;
    u64 end = addr + size;
    while (addr < end) {
        MemBlock block;
        mem_query(addr, &block);
        if (!(block.perm & perm) || !block.size || block.addr > addr ||
            block.size > AS_SIZE - block.addr || block.addr + block.size <= addr) return 0;
        addr = block.addr + block.size;
    }
    return 1;
}
static int native_guest_readable(u64 addr, u64 size) { return native_guest_access(addr, size, PERM_R); }
static int native_guest_writable(u64 addr, u64 size) { return native_guest_access(addr, size, PERM_W); }
static float native_float_arg(const CPU* c, unsigned index) {
    uint32_t bits = SNX_VS(c->v[index], 0);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
#define RESOURCE_BUILDER_HANDLER(name, type, get, statements) \
    static void h_##name(CPU* c, u64 entry) {                 \
        (void)entry;                                         \
        pm_lock(&objs_lock);                                 \
        type* b = get(c->x[0]);                              \
        statements;                                          \
        pm_unlock(&objs_lock);                               \
    }
RESOURCE_BUILDER_HANDLER(sampler_builder_set_device, NvnSampler, sampler_builder,
    b->device = c->x[1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_defaults, NvnSampler, sampler_builder,
    nvn_sampler_defaults(b))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_min_mag_filter, NvnSampler, sampler_builder,
    b->min_filter = (u32)c->x[1]; b->mag_filter = (u32)c->x[2])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_wrap_mode, NvnSampler, sampler_builder,
    for (unsigned i = 0; i < 3; i++) b->wrap[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_clamp, NvnSampler, sampler_builder,
    b->min_lod = native_float_arg(c, 0); b->max_lod = native_float_arg(c, 1))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_bias, NvnSampler, sampler_builder,
    b->lod_bias = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_compare, NvnSampler, sampler_builder,
    b->compare_enable = (u32)c->x[1]; b->compare = (u32)c->x[2])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_border_color, NvnSampler, sampler_builder,
    for (unsigned i = 0; i < 4; i++) b->border[i] = gr32(c->x[1] + i * 4))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_max_anisotropy, NvnSampler, sampler_builder,
    b->anisotropy = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_reduction_filter, NvnSampler, sampler_builder,
    b->reduction = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_snap, NvnSampler, sampler_builder,
    b->lod_snap = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(texture_builder_set_device, NvnTextureBuilder, texture_builder,
    b->device = c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_defaults, NvnTextureBuilder, texture_builder,
    nvn_texture_builder_defaults(b))
RESOURCE_BUILDER_HANDLER(texture_builder_set_flags, NvnTextureBuilder, texture_builder,
    b->flags = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_target, NvnTextureBuilder, texture_builder,
    b->target = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_size_2d, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1]; b->height = (u32)c->x[2]; b->depth = 1)
RESOURCE_BUILDER_HANDLER(texture_builder_set_size_3d, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1]; b->height = (u32)c->x[2]; b->depth = (u32)c->x[3])
RESOURCE_BUILDER_HANDLER(texture_builder_set_levels, NvnTextureBuilder, texture_builder,
    b->levels = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_format, NvnTextureBuilder, texture_builder,
    b->format = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_swizzle, NvnTextureBuilder, texture_builder,
    for (unsigned i = 0; i < 4; i++) b->swizzle[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_depth_stencil_mode, NvnTextureBuilder, texture_builder,
    b->depth_stencil_mode = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_storage, NvnTextureBuilder, texture_builder,
    b->pool = c->x[1]; b->offset = c->x[2])
RESOURCE_BUILDER_HANDLER(texture_builder_set_stride, NvnTextureBuilder, texture_builder,
    b->stride = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_get_width, NvnTextureBuilder, texture_builder,
    c->x[0] = b->width)
RESOURCE_BUILDER_HANDLER(texture_builder_get_height, NvnTextureBuilder, texture_builder,
    c->x[0] = b->height)
RESOURCE_BUILDER_HANDLER(texture_builder_get_storage_alignment, NvnTextureBuilder, texture_builder,
    c->x[0] = nvn_texture_storage_alignment(b, &nvn_texture_device_words))
RESOURCE_BUILDER_HANDLER(texture_builder_get_storage_size, NvnTextureBuilder, texture_builder,
    NvnTextureLayout l; nvn_texture_layout(b, &nvn_texture_device_words, &l); c->x[0] = l.size)

RESOURCE_BUILDER_HANDLER(texture_builder_set_width, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_height, NvnTextureBuilder, texture_builder,
    b->height = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_depth, NvnTextureBuilder, texture_builder,
    b->depth = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_defaults, NvnTextureView, texture_view,
    memset(b, 0, sizeof *b))
RESOURCE_BUILDER_HANDLER(texture_view_set_levels, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_LEVELS; b->base_level = (uint8_t)c->x[1]; b->level_count = (uint8_t)c->x[2])
RESOURCE_BUILDER_HANDLER(texture_view_set_layers, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_LAYERS; b->base_layer = (uint16_t)c->x[1]; b->layer_count = (uint16_t)c->x[2])
RESOURCE_BUILDER_HANDLER(texture_view_set_format, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_FORMAT; b->format = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_swizzle, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_SWIZZLE; for (unsigned i = 0; i < 4; i++) b->swizzle[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(texture_view_set_depth_stencil_mode, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_DEPTH_STENCIL_MODE; b->depth_stencil_mode = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_target, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_TARGET; b->target = (u32)c->x[1])

static void h_texture_builder_set_packaged_texture_layout(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (native_guest_readable(c->x[1], sizeof(uint32_t)))
        texture_builder(c->x[0])->tile = nvn_texture_packaged_tile(gr32(c->x[1]));
    else
        LOG("nvn-native: packaged texture layout unreadable address=%llx\n", (unsigned long long)c->x[1]);
    pm_unlock(&objs_lock);
}

#undef RESOURCE_BUILDER_HANDLER

static void h_sampler_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnSampler b = *sampler_builder(c->x[1]);
    *(NvnSampler*)rec_get(c->x[0], NVN_OBJ_SAMPLER, sizeof b) = b;
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}
/* The library's function is one `ret` (d83a5b0): the object stays readable, so the host keeps its record too. */
static void h_sampler_finalize(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}
static void h_sampler_compare(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const NvnSampler* a = known_record(c->x[0], NVN_OBJ_SAMPLER);
    const NvnSampler* b = known_record(c->x[1], NVN_OBJ_SAMPLER);
    if (!a || !b)
        LOG("nvn-native: nvnSamplerCompare unknown sampler=%llx\n",
            (unsigned long long)(a ? c->x[1] : c->x[0]));
    c->x[0] = a && b ? nvn_sampler_equal(a, b) : 0;
    pm_unlock(&objs_lock);
}
/* Find only: a failed link must not create a pool record. Caller holds objs_lock. */
static void* known_record(u64 addr, NvnObjKind kind) {
    NvnObj* o = nvn_objtab_find(&objs, addr);
    return o && o->kind == (uint32_t)kind ? o->data : NULL;
}

/* G8: the program records. The library links the shaders and checks each header; the host keeps the same checks and
   copies the control block at set time. No draw uses the record in this step. */
#define PROGRAM_LOG_LIMIT 64u           /* log lines for the faults of the program functions */
#define PROGRAM_ENTRY_BYTES 0x18u       /* one SetShadersExt entry: word, header pointer, control pointer */
#define PROGRAM_HEADER_READ_BYTES (NVN_PROGRAM_STAGE_OFFSET + 4u) /* the header words up to the stage word */
#define PROGRAM_CONTROL_BYTES 0x18u     /* the control block that the host copies */
#define PROGRAM_HEADER_ALIGNMENT 8u

#define PROGRAM_REFUSAL_ENTRY_UNREADABLE "entry is unreadable"
#define PROGRAM_REFUSAL_WORD_ZERO "entry word is zero"
#define PROGRAM_REFUSAL_HEADER_NULL "header is null"
#define PROGRAM_REFUSAL_HEADER_UNALIGNED "header is not aligned"
#define PROGRAM_REFUSAL_HEADER_UNREADABLE "header is unreadable"
#define PROGRAM_REFUSAL_HEADER_BAD "header is not a shader"
#define PROGRAM_REFUSAL_STAGE_REPEATED "stage is repeated"
#define PROGRAM_REFUSAL_CONTROL_UNREADABLE "control is unreadable"

/* The host builder builds stage 0 (vertex, no VertexA), stage 1 (fragment) and stage 5 (compute). Stages 2 to 4,
   VertexA and the transform feedback are not ported, so a program with any of them is refused. */
#define PROGRAM_STAGE_VERTEX 0u
#define PROGRAM_STAGE_FRAGMENT 1u
#define PROGRAM_STAGE_COMPUTE 5u
#define PROGRAM_STAGE_SUPPORTED_MASK 0x23u

#define PROGRAM_BUILD_HEADER_BYTES 0x76Cu /* the header bytes that the word builder reads, up to h+0x76b */
#define PROGRAM_HEADER_CB_OFFSET 0x700u
#define PROGRAM_HEADER_CODE_OFFSET 0x708u
#define PROGRAM_HEADER_VALUE_OFFSET 0x70Cu
#define PROGRAM_COMPUTE_6F8_OFFSET 0x6F8u
#define PROGRAM_COMPUTE_6FC_OFFSET 0x6FCu
#define PROGRAM_COMPUTE_710_OFFSET 0x710u
#define PROGRAM_COMPUTE_718_OFFSET 0x718u
#define PROGRAM_COMPUTE_71C_OFFSET 0x71Cu
#define PROGRAM_COMPUTE_720_OFFSET 0x720u
#define PROGRAM_COMPUTE_724_OFFSET 0x724u
#define PROGRAM_COMPUTE_728_OFFSET 0x728u
#define PROGRAM_COMPUTE_72C_OFFSET 0x72Cu
#define PROGRAM_COMPUTE_730_OFFSET 0x730u
#define PROGRAM_COMPUTE_734_OFFSET 0x734u
#define PROGRAM_COMPUTE_750_OFFSET 0x750u
#define PROGRAM_COMPUTE_754_OFFSET 0x754u
#define PROGRAM_COMPUTE_FLAG_769_OFFSET 0x769u
#define PROGRAM_COMPUTE_FLAG_76A_OFFSET 0x76Au
#define PROGRAM_COMPUTE_FLAG_76B_OFFSET 0x76Bu
#define PROGRAM_FRAGMENT_G148_OFFSET 0x718u
#define PROGRAM_FRAGMENT_G150_OFFSET 0x719u
#define PROGRAM_FRAGMENT_G158_OFFSET 0x71Au
#define PROGRAM_FRAGMENT_G160_OFFSET 0x71Bu
#define PROGRAM_FRAGMENT_G168_OFFSET 0x71Cu
#define PROGRAM_FRAGMENT_G170_OFFSET 0x71Du
#define PROGRAM_FRAGMENT_G190_OFFSET 0x720u
#define PROGRAM_FRAGMENT_G1A0_OFFSET 0x724u
#define PROGRAM_FRAGMENT_G198_OFFSET 0x734u
#define PROGRAM_FRAGMENT_G178_OFFSET 0x748u
#define PROGRAM_FRAGMENT_G180_OFFSET 0x749u
#define PROGRAM_FRAGMENT_G188_OFFSET 0x74Au
#define PROGRAM_FRAGMENT_G1A0_WORDS 4u

/* The shader pools lie in one 4 GiB window of the GPU address space, so the library subtraction of low32(P) is the
   identity and the entry word is the delta d. */
#define NVN_SHADER_REGION_BASE 0x400000000ull
/* The two methods that carry the region base once for a frame job: high 32 bits, then low 32 bits. */
#define NVN_SHADER_REGION_BASE_HIGH_METHOD 0x582u
#define NVN_SHADER_REGION_BASE_LOW_METHOD 0x583u

/* Measured with the game's own library (nvn_native=0). The host has no device object, so these are constants. */
static const NvnProgramDeviceIn program_device_words = {
    .d1a38 = 0x087f6080u, .d1a3c = 0u, .d1a40 = 0u, .d1a68 = 0x20u,
    .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u, .da4 = 0u,
};

/* Caller holds objs_lock. Reads one entry and its header. On a refusal returns 0 and sets *reason. */
static int program_read_entry(u64 entries, int32_t index, ProgramStageRec* out, uint32_t* stage_index,
                              const char** reason) {
    u64 offset = (u64)(uint32_t)index * PROGRAM_ENTRY_BYTES;
    if (entries > UINT64_MAX - offset) {
        *reason = PROGRAM_REFUSAL_ENTRY_UNREADABLE;
        return 0;
    }
    u64 at = entries + offset;
    if (!native_guest_readable(at, PROGRAM_ENTRY_BYTES)) {
        *reason = PROGRAM_REFUSAL_ENTRY_UNREADABLE;
        return 0;
    }
    uint64_t entry_word = gr64(at);
    uint64_t header = gr64(at + 8);
    uint64_t control = gr64(at + 0x10);
    if (!entry_word) {
        *reason = PROGRAM_REFUSAL_WORD_ZERO;
        return 0;
    }
    if (!header) {
        *reason = PROGRAM_REFUSAL_HEADER_NULL;
        return 0;
    }
    if (header & (PROGRAM_HEADER_ALIGNMENT - 1u)) {
        *reason = PROGRAM_REFUSAL_HEADER_UNALIGNED;
        return 0;
    }
    if (!native_guest_readable(header, PROGRAM_HEADER_READ_BYTES)) {
        *reason = PROGRAM_REFUSAL_HEADER_UNREADABLE;
        return 0;
    }
    if (!nvn_program_header_stage(gr32(header + 4), gr32(header + 8), gr32(header + 0xc), gr32(header + 0x10),
                                  gr32(header + NVN_PROGRAM_STAGE_OFFSET), stage_index)) {
        *reason = PROGRAM_REFUSAL_HEADER_BAD;
        return 0;
    }
    ProgramStageRec stage = {.header = header, .entry_word = entry_word};
    if (control) {
        if (!native_guest_readable(control, PROGRAM_CONTROL_BYTES)) {
            *reason = PROGRAM_REFUSAL_CONTROL_UNREADABLE;
            return 0;
        }
        for (unsigned i = 0; i < PROGRAM_CONTROL_BYTES / 4u; i++) {
            uint32_t word = gr32(control + (u64)i * 4u);
            memcpy(&stage.control[i * 4u], &word, sizeof word);
        }
        stage.has_control = 1;
    }
    *out = stage;
    return 1;
}

/* Caller holds objs_lock. Reads the header words of one stage record. Returns 0 when the header is too short. */
static int program_read_stage_in(const ProgramStageRec* rec, NvnProgramStageIn* out) {
    if (!native_guest_readable(rec->header, PROGRAM_BUILD_HEADER_BYTES)) return 0;
    uint32_t d = (uint32_t)rec->entry_word;
    out->code_offset = (uint32_t)(gr32(rec->header + PROGRAM_HEADER_CODE_OFFSET) + d);
    out->cb_offset = (uint32_t)(gr32(rec->header + PROGRAM_HEADER_CB_OFFSET) + d);
    out->header_value = gr32(rec->header + PROGRAM_HEADER_VALUE_OFFSET);
    return 1;
}

static uint8_t program_guest_byte(u64 addr) { return (uint8_t)(gr32(addr) & 0xffu); }

/* Caller holds objs_lock. program_read_stage_in checked that the header is readable. */
static void program_read_fragment_in(u64 header, NvnProgramFragmentIn* out) {
    out->g148 = program_guest_byte(header + PROGRAM_FRAGMENT_G148_OFFSET);
    out->g150 = program_guest_byte(header + PROGRAM_FRAGMENT_G150_OFFSET);
    out->g158 = program_guest_byte(header + PROGRAM_FRAGMENT_G158_OFFSET);
    out->g160 = program_guest_byte(header + PROGRAM_FRAGMENT_G160_OFFSET);
    out->g168 = program_guest_byte(header + PROGRAM_FRAGMENT_G168_OFFSET);
    out->g170 = program_guest_byte(header + PROGRAM_FRAGMENT_G170_OFFSET);
    out->g178 = program_guest_byte(header + PROGRAM_FRAGMENT_G178_OFFSET);
    out->g180 = program_guest_byte(header + PROGRAM_FRAGMENT_G180_OFFSET);
    out->g188 = program_guest_byte(header + PROGRAM_FRAGMENT_G188_OFFSET);
    out->g190 = gr32(header + PROGRAM_FRAGMENT_G190_OFFSET);
    out->g198 = gr32(header + PROGRAM_FRAGMENT_G198_OFFSET);
    for (uint32_t i = 0; i < PROGRAM_FRAGMENT_G1A0_WORDS; i++)
        out->g1a0[i] = gr32(header + PROGRAM_FRAGMENT_G1A0_OFFSET + (u64)i * 4u);
}

/* Caller holds objs_lock. Reads the compute words. The flags word is three bytes, each its bit 0; the upper bits are
   0 because the library leaves the rest of the word as stack content. */
static int program_read_compute_in(const ProgramStageRec* rec, NvnProgramComputeIn* out) {
    if (!native_guest_readable(rec->header, PROGRAM_BUILD_HEADER_BYTES)) return 0;
    uint32_t d = (uint32_t)rec->entry_word;
    out->code_offset = (uint32_t)(gr32(rec->header + PROGRAM_HEADER_CODE_OFFSET) + d);
    out->cb_offset = (uint32_t)(gr32(rec->header + PROGRAM_HEADER_CB_OFFSET) + d);
    out->h6fc = gr32(rec->header + PROGRAM_COMPUTE_6FC_OFFSET);
    out->h710 = gr32(rec->header + PROGRAM_COMPUTE_710_OFFSET);
    out->h70c = gr32(rec->header + PROGRAM_HEADER_VALUE_OFFSET);
    out->h718 = gr32(rec->header + PROGRAM_COMPUTE_718_OFFSET);
    out->h71c = gr32(rec->header + PROGRAM_COMPUTE_71C_OFFSET);
    out->h720 = gr32(rec->header + PROGRAM_COMPUTE_720_OFFSET);
    out->h724 = gr32(rec->header + PROGRAM_COMPUTE_724_OFFSET);
    out->h728 = gr32(rec->header + PROGRAM_COMPUTE_728_OFFSET);
    out->h72c = gr32(rec->header + PROGRAM_COMPUTE_72C_OFFSET);
    out->h730 = gr32(rec->header + PROGRAM_COMPUTE_730_OFFSET);
    out->h734 = gr32(rec->header + PROGRAM_COMPUTE_734_OFFSET);
    out->h6f8 = gr32(rec->header + PROGRAM_COMPUTE_6F8_OFFSET);
    out->h750_low = gr32(rec->header + PROGRAM_COMPUTE_750_OFFSET);
    out->h750_high = gr32(rec->header + PROGRAM_COMPUTE_754_OFFSET);
    out->flags = 0;
    if (program_guest_byte(rec->header + PROGRAM_COMPUTE_FLAG_769_OFFSET) & 1u)
        out->flags |= NVN_PROGRAM_COMPUTE_FLAG_0;
    if (program_guest_byte(rec->header + PROGRAM_COMPUTE_FLAG_76A_OFFSET) & 1u)
        out->flags |= NVN_PROGRAM_COMPUTE_FLAG_1;
    if (program_guest_byte(rec->header + PROGRAM_COMPUTE_FLAG_76B_OFFSET) & 1u)
        out->flags |= NVN_PROGRAM_COMPUTE_FLAG_2;
    return 1;
}

/* The lowest stage above stage 1 that is present and not supported, or -1. */
static int program_unsupported_stage(uint32_t stage_mask) {
    uint32_t unsupported = stage_mask & ~PROGRAM_STAGE_SUPPORTED_MASK;
    for (uint32_t stage = PROGRAM_STAGE_FRAGMENT + 1u; stage < NVN_PROGRAM_STAGES; stage++)
        if (unsupported & (1u << stage)) return (int)stage;
    return -1;
}

/* nvnProgramInitialize(prog, device): the host keeps the device link and writes nothing into the game memory. */
static void h_program_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    u64 addr = c->x[0], device = c->x[1];
    ProgramRec* r = rec_get(addr, NVN_OBJ_PROGRAM, sizeof *r);
    memset(r, 0, sizeof *r);
    if (!known_record(device, NVN_OBJ_DEVICE)) {
        nvn_objtab_remove(&objs, addr); /* a later call must not find a program without a device */
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program initialization %llx device=%llx is not a live device\n",
                  (unsigned long long)addr, (unsigned long long)device);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    r->device = device;
    r->device_generation = nvn_objtab_find(&objs, device)->generation;
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}

/* Counters of nvnProgramSetShadersExt, guarded by objs_lock. Only the refusal lines use PROGRAM_LOG_LIMIT. */
#define PROGRAM_SUMMARY_INTERVAL 128u /* sets between two summary lines */
static uint32_t program_sets_total, program_sets_refused;
static uint32_t program_sets_by_mask[1u << NVN_PROGRAM_STAGES];

/* Caller holds objs_lock. Prints the total, the refusals and each stage mask that was set. */
static void log_program_set_summary(void) {
    char masks[256];
    size_t used = 0;
    masks[0] = 0;
    for (uint32_t m = 0; m < (1u << NVN_PROGRAM_STAGES) && used < sizeof masks; m++) {
        if (!program_sets_by_mask[m]) continue;
        int n = snprintf(masks + used, sizeof masks - used, " %x:%u", (unsigned)m, (unsigned)program_sets_by_mask[m]);
        if (n < 0 || (size_t)n >= sizeof masks - used) break;
        used += (size_t)n;
    }
    LOG("nvn-native: program set summary sets=%u refused=%u masks:%s\n", (unsigned)program_sets_total,
        (unsigned)program_sets_refused, masks);
}

/* nvnProgramSetShadersExt(prog, count, entries). All entries must be valid: the host writes the new stage table only
   then, and keeps the old content on a refusal. */
static void h_program_set_shaders_ext(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    u64 addr = c->x[0], entries = c->x[2];
    int32_t count = (int32_t)c->x[1];
    ProgramRec* r = known_record(addr, NVN_OBJ_PROGRAM);
    if (!r) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program set shaders %llx is not a live program\n",
                  (unsigned long long)addr);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    ProgramRec next = *r;
    next.stage_mask = 0;
    memset(next.stage, 0, sizeof next.stage);
    int ok = 1;
    if (count < 0) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: count=%d is negative\n",
                  (unsigned long long)addr, count);
        ok = 0;
    } else if ((uint32_t)count > NVN_PROGRAM_STAGES) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: count=%d is above %u\n",
                  (unsigned long long)addr, count, (unsigned)NVN_PROGRAM_STAGES);
        ok = 0;
    } else if (count > 0 && !entries) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: entries is null\n",
                  (unsigned long long)addr);
        ok = 0;
    } else if (count > 0 && !native_guest_readable(entries, (u64)count * PROGRAM_ENTRY_BYTES)) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: entries=%llx is unreadable\n",
                  (unsigned long long)addr, (unsigned long long)entries);
        ok = 0;
    }
    for (int32_t i = 0; ok && i < count; i++) {
        ProgramStageRec stage;
        uint32_t stage_index = 0;
        const char* reason = NULL;
        if (!program_read_entry(entries, i, &stage, &stage_index, &reason)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx entry=%d refused: %s\n",
                      (unsigned long long)addr, i, reason);
            ok = 0;
            break;
        }
        if (next.stage_mask & (1u << stage_index)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx entry=%d refused: %s\n",
                      (unsigned long long)addr, i, PROGRAM_REFUSAL_STAGE_REPEATED);
            ok = 0;
            break;
        }
        next.stage[stage_index] = stage;
        next.stage_mask |= 1u << stage_index;
    }
    if (ok) {
        int stage = program_unsupported_stage(next.stage_mask);
        if (stage >= 0) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT,
                      "nvn-native: program %llx refused: stage %d is not supported by the host builder\n",
                      (unsigned long long)addr, stage);
            ok = 0;
        }
    }
    if (ok) {
        NvnProgramStageIn vertex_in, fragment_in;
        NvnProgramComputeIn compute_in;
        NvnProgramFragmentIn frag = {0};
        const NvnProgramStageIn* vertex = NULL;
        const NvnProgramStageIn* fragment = NULL;
        const NvnProgramComputeIn* compute = NULL;
        if (next.stage_mask & (1u << PROGRAM_STAGE_VERTEX)) {
            if (!program_read_stage_in(&next.stage[PROGRAM_STAGE_VERTEX], &vertex_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)PROGRAM_STAGE_VERTEX);
                ok = 0;
            } else {
                vertex = &vertex_in;
            }
        }
        if (ok && (next.stage_mask & (1u << PROGRAM_STAGE_FRAGMENT))) {
            if (!program_read_stage_in(&next.stage[PROGRAM_STAGE_FRAGMENT], &fragment_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)PROGRAM_STAGE_FRAGMENT);
                ok = 0;
            } else {
                fragment = &fragment_in;
                frag.present = 1;
                program_read_fragment_in(next.stage[PROGRAM_STAGE_FRAGMENT].header, &frag);
            }
        }
        if (ok && (next.stage_mask & (1u << PROGRAM_STAGE_COMPUTE))) {
            if (!program_read_compute_in(&next.stage[PROGRAM_STAGE_COMPUTE], &compute_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)PROGRAM_STAGE_COMPUTE);
                ok = 0;
            } else {
                compute = &compute_in;
            }
        }
        if (ok && compute && (vertex || fragment)) {
            uint32_t other = vertex ? PROGRAM_STAGE_VERTEX : PROGRAM_STAGE_FRAGMENT;
            LOG_LIMIT(PROGRAM_LOG_LIMIT,
                      "nvn-native: program %llx refused: compute stage %u with stage %u is not supported by the host "
                      "builder\n",
                      (unsigned long long)addr, (unsigned)PROGRAM_STAGE_COMPUTE, (unsigned)other);
            ok = 0;
        }
        if (ok && !nvn_program_build_words(compute, vertex, fragment, &frag, &program_device_words,
                                           NVN_SHADER_REGION_BASE, next.words, &next.word_count)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: a word value is above 13 bits\n",
                      (unsigned long long)addr);
            ok = 0;
        }
    }
    program_sets_total++;
    if (ok) {
        *r = next;
        /* The first set of each stage mask is logged in full, the rest only counts in the summary. */
        if (program_sets_by_mask[next.stage_mask]++ == 0)
            LOG("nvn-native: program %llx set stages=%x count=%d words=%u\n", (unsigned long long)addr,
                next.stage_mask, count, next.word_count);
        c->x[0] = 1;
    } else {
        program_sets_refused++;
        c->x[0] = 0;
    }
    if (program_sets_total % PROGRAM_SUMMARY_INTERVAL == 0) log_program_set_summary();
    pm_unlock(&objs_lock);
}

/* nvnProgramFinalize(prog): the library does not check the object, and neither does the host. */
static void h_program_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (known_record(c->x[0], NVN_OBJ_PROGRAM)) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
/* Caller holds objs_lock. Both descriptor types use signed indices and 32-byte entries. */
static int descriptor_destination(CPU* c, NvnObjKind kind, const char* operation, uint32_t entry_bytes, uint64_t* at) {
    DescriptorPoolRec* pool = known_record(c->x[0], kind);
    if (!pool) {
        LOG("nvn-native: %s unknown descriptor pool=%llx\n", operation, (unsigned long long)c->x[0]);
        return 0;
    }
    MemoryPoolRec* memory = known_record(pool->memory_pool, NVN_OBJ_MEMORY_POOL);
    if (!memory) {
        LOG("nvn-native: %s unknown memory pool=%llx\n", operation, (unsigned long long)pool->memory_pool);
        return 0;
    }
    uint64_t base = nvn_memory_pool_map(memory->flags, memory->storage);
    if (!base) {
        LOG("nvn-native: %s has no CPU base, memory pool=%llx flags=%x\n",
            operation, (unsigned long long)pool->memory_pool, memory->flags);
        return 0;
    }
    uint64_t offset;
    if (!nvn_descriptor_offset(pool->offset, pool->count, memory->size, entry_bytes, (int32_t)c->x[1], &offset) ||
        offset > UINT64_MAX - base || !native_guest_writable(base + offset, entry_bytes)) {
        LOG("nvn-native: %s index=%d is outside descriptor pool=%llx (offset=%llx count=%u memory size=%llx)\n",
            operation, (int32_t)c->x[1], (unsigned long long)c->x[0], (unsigned long long)pool->offset, pool->count,
            (unsigned long long)memory->size);
        return 0;
    }
    *at = base + offset;
    return 1;
}
static void sampler_pool_register(CPU* c) {
    uint64_t at;
    if (!descriptor_destination(c, NVN_OBJ_SAMPLER_POOL, "sampler registration", NVN_SAMPLER_DESCRIPTOR_BYTES, &at)) return;
    NvnSampler* sampler = known_record(c->x[2], NVN_OBJ_SAMPLER);
    if (!sampler) {
        LOG("nvn-native: sampler registration unknown sampler=%llx\n", (unsigned long long)c->x[2]);
        return;
    }
    uint32_t words[NVN_SAMPLER_DESCRIPTOR_WORDS];
    if (!nvn_sampler_descriptor(sampler, &nvn_sampler_device_words, words)) {
        LOG("nvn-native: sampler registration unsupported wrap modes %u,%u,%u sampler=%llx\n", sampler->wrap[0],
            sampler->wrap[1], sampler->wrap[2], (unsigned long long)c->x[2]);
        return;
    }
    for (unsigned i = 0; i < NVN_SAMPLER_DESCRIPTOR_WORDS; i++) gw32(at + i * sizeof(uint32_t), words[i]);
}
static void h_sampler_pool_register_sampler(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    sampler_pool_register(c);
    pm_unlock(&objs_lock);
}
#define TEXTURE_FORMAT_TABLE_POINTER 0xe03fd40ull
#define TEXTURE_FORMAT_RECORD_BYTES 0x30u
#define TEXTURE_FORMAT_SEED_OFFSET 0x24u
#define TEXTURE_FORMAT_DEPTH_OFFSET 0x0eu
#define TEXTURE_FORMAT_STENCIL_OFFSET 0x0fu
#define TEXTURE_FORMAT_ONE_OFFSET 0x11u
#define TEXTURE_FORMAT_SRGB_OFFSET 0x12u
#define TEXTURE_FORMAT_COPY_OFFSET 0x18u

static int texture_format_record(uint32_t format, NvnTextureFormatRecord* record) {
    if (format > NVN_TEXTURE_FORMAT_MAX) {
        LOG("nvn-native: texture registration invalid format=%x\n", format);
        return 0;
    }
    if (!native_guest_readable(TEXTURE_FORMAT_TABLE_POINTER, sizeof(uint64_t))) {
        LOG("nvn-native: texture registration unreadable table pointer at=%llx\n",
            (unsigned long long)TEXTURE_FORMAT_TABLE_POINTER);
        return 0;
    }
    uint64_t table = gr64(TEXTURE_FORMAT_TABLE_POINTER);
    uint64_t offset = (uint64_t)format * TEXTURE_FORMAT_RECORD_BYTES;
    if (!table || table > UINT64_MAX - offset || !native_guest_readable(table + offset, TEXTURE_FORMAT_RECORD_BYTES)) {
        LOG("nvn-native: texture registration unreadable format=%x table=%llx\n", format, (unsigned long long)table);
        return 0;
    }
    uint64_t at = table + offset;
    record->seed = gr32(at + TEXTURE_FORMAT_SEED_OFFSET);
    record->depth_size = *GP(at + TEXTURE_FORMAT_DEPTH_OFFSET);
    record->stencil_size = *GP(at + TEXTURE_FORMAT_STENCIL_OFFSET);
    record->one_code = *GP(at + TEXTURE_FORMAT_ONE_OFFSET);
    record->srgb = *GP(at + TEXTURE_FORMAT_SRGB_OFFSET);
    return 1;
}
#define VERTEX_FORMAT_WORD_OFFSET 0x14u

/* The format word of one vertex attribute format index. The replay reads the game's table on its own thread; the
   reads are of guest memory, so objs_lock is not needed. */
static int vertex_format_word(uint32_t index, uint32_t* word) {
    if (!native_guest_readable(TEXTURE_FORMAT_TABLE_POINTER, sizeof(uint64_t))) {
        LOG("nvn-native: vertex format unreadable table pointer at=%llx\n",
            (unsigned long long)TEXTURE_FORMAT_TABLE_POINTER);
        return 0;
    }
    uint64_t table = gr64(TEXTURE_FORMAT_TABLE_POINTER);
    uint64_t offset = (uint64_t)index * TEXTURE_FORMAT_RECORD_BYTES;
    if (!table || table > UINT64_MAX - offset || !native_guest_readable(table + offset, TEXTURE_FORMAT_RECORD_BYTES)) {
        LOG("nvn-native: vertex format unreadable index=%x table=%llx\n", index, (unsigned long long)table);
        return 0;
    }
    *word = gr32(table + offset + VERTEX_FORMAT_WORD_OFFSET);
    return 1;
}
/* The copy format word is the column +0x18 of the same table. The replay reads the game's table on its own thread;
   the reads are of guest memory, so objs_lock is not needed. 0 means an unreadable or invalid format. */
static uint32_t copy_format_word_cb(void* ctx, uint32_t format) {
    (void)ctx;
    if (format > NVN_TEXTURE_FORMAT_MAX) {
        LOG("nvn-native: copy format invalid format=%x\n", format);
        return 0;
    }
    if (!native_guest_readable(TEXTURE_FORMAT_TABLE_POINTER, sizeof(uint64_t))) {
        LOG("nvn-native: copy format unreadable table pointer at=%llx\n",
            (unsigned long long)TEXTURE_FORMAT_TABLE_POINTER);
        return 0;
    }
    uint64_t table = gr64(TEXTURE_FORMAT_TABLE_POINTER);
    uint64_t offset = (uint64_t)format * TEXTURE_FORMAT_RECORD_BYTES;
    if (!table || table > UINT64_MAX - offset ||
        !native_guest_readable(table + offset, TEXTURE_FORMAT_RECORD_BYTES)) {
        LOG("nvn-native: copy format unreadable format=%x table=%llx\n", format, (unsigned long long)table);
        return 0;
    }
    return gr32(table + offset + TEXTURE_FORMAT_COPY_OFFSET);
}
static void texture_pool_register(CPU* c, NvnTextureDescriptorMode mode) {
    uint64_t at;
    if (!descriptor_destination(c, NVN_OBJ_TEXTURE_POOL, "texture registration", NVN_TEXTURE_DESCRIPTOR_BYTES, &at)) return;
    TextureRec* texture = known_record(c->x[2], NVN_OBJ_TEXTURE);
    if (!texture) {
        LOG("nvn-native: texture registration unknown texture=%llx\n", (unsigned long long)c->x[2]);
        return;
    }
    NvnTextureView* view = c->x[3] ? known_record(c->x[3], NVN_OBJ_TEXTURE_VIEW) : NULL;
    if (c->x[3] && !view) {
        LOG("nvn-native: texture registration unknown view=%llx\n", (unsigned long long)c->x[3]);
        return;
    }
    uint32_t format = view && (view->flags & NVN_VIEW_FORMAT) ? view->format : texture->builder.format;
    NvnTextureFormatRecord record;
    if (!texture_format_record(format, &record)) return;
    uint32_t words[NVN_TEXTURE_DESCRIPTOR_WORDS];
    NvnTextureDescriptorResult result =
        nvn_texture_descriptor(texture, view, &nvn_texture_device_words, &record, mode, words);
    if (result != NVN_TEXTURE_DESCRIPTOR_OK) {
        LOG("nvn-native: texture registration invalid descriptor result=%u texture=%llx\n",
            (unsigned)result, (unsigned long long)c->x[2]);
        return;
    }
    for (unsigned i = 0; i < NVN_TEXTURE_DESCRIPTOR_WORDS; i++) gw32(at + i * sizeof(uint32_t), words[i]);
}
static void h_texture_pool_register_texture(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    texture_pool_register(c, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE);
    pm_unlock(&objs_lock);
}
static void h_texture_pool_register_image(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    texture_pool_register(c, NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE);
    pm_unlock(&objs_lock);
}
#define TEXTURE_GETTER(name, value) \
    static void h_texture_get_##name(CPU* c, u64 entry) { \
        (void)entry; \
        pm_lock(&objs_lock); \
        TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE); \
        if (!t) LOG("nvn-native: texture getter " #name " unknown texture=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = t ? (value) : 0; \
        pm_unlock(&objs_lock); \
    }
TEXTURE_GETTER(width, t->builder.width)
TEXTURE_GETTER(height, t->builder.height)
TEXTURE_GETTER(stride, t->layout.linear_stride)
TEXTURE_GETTER(target, t->builder.target)
TEXTURE_GETTER(depth, t->builder.depth)
#undef TEXTURE_GETTER

/* The outputs of a texture view getter: the pointer must be writable before the record is used. One log line on failure. */
static int texture_view_getter(CPU* c, const char* name, unsigned outputs, const NvnTextureView** out) {
    for (unsigned i = 0; i < outputs; i++)
        if (!native_guest_writable(c->x[1 + i], sizeof(uint32_t))) {
            LOG("nvn-native: %s unwritable output\n", name);
            return 0;
        }
    const NvnTextureView* v = known_record(c->x[0], NVN_OBJ_TEXTURE_VIEW);
    if (!v) {
        LOG("nvn-native: %s unknown view=%llx\n", name, (unsigned long long)c->x[0]);
        return 0;
    }
    *out = v;
    return 1;
}
static void h_texture_view_get_levels(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetLevels";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 2, &v)) {
        gw32(c->x[1], v->base_level);
        gw32(c->x[2], v->level_count);
        c->x[0] = (v->flags & NVN_VIEW_LEVELS) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_view_get_layers(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetLayers";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 2, &v)) {
        gw32(c->x[1], v->base_layer);
        gw32(c->x[2], v->layer_count);
        c->x[0] = (v->flags & NVN_VIEW_LAYERS) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_view_get_format(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetFormat";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 1, &v)) {
        gw32(c->x[1], v->format);
        c->x[0] = (v->flags & NVN_VIEW_FORMAT) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_view_get_swizzle(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetSwizzle";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 4, &v)) {
        for (unsigned i = 0; i < 4; i++) gw32(c->x[1 + i], v->swizzle[i]);
        c->x[0] = (v->flags & NVN_VIEW_SWIZZLE) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_view_get_depth_stencil_mode(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetDepthStencilMode";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 1, &v)) {
        gw32(c->x[1], v->depth_stencil_mode);
        c->x[0] = (v->flags & NVN_VIEW_DEPTH_STENCIL_MODE) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_view_get_target(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureViewGetTarget";
    pm_lock(&objs_lock);
    const NvnTextureView* v = NULL;
    if (texture_view_getter(c, name, 1, &v)) {
        gw32(c->x[1], v->target);
        c->x[0] = (v->flags & NVN_VIEW_TARGET) != 0;
    } else {
        c->x[0] = 0;
    }
    pm_unlock(&objs_lock);
}

static int texture_initialize(TextureRec* t, const NvnTextureBuilder* b) {
    if (b->gl_name) {
        LOG("nvn-native: texture import not supported\n");
        return 0;
    }
    MemoryPoolRec* pool = known_record(b->pool, NVN_OBJ_MEMORY_POOL);
    if (!pool) {
        LOG("nvn-native: texture initialization %s pool=%llx\n", b->pool ? "unknown" : "null",
            (unsigned long long)b->pool);
        return 0;
    }
    if (pool->flags & NVN_POOL_PHYSICAL) {
        LOG("nvn-native: texture initialization physical pool=%llx flags=%x\n",
            (unsigned long long)b->pool, pool->flags);
        return 0;
    }
    t->builder = *b;
    if (!nvn_texture_layout(b, &nvn_texture_device_words, &t->layout)) {
        LOG("nvn-native: texture initialization unsupported format=%x\n", b->format);
        return 0;
    }
    if ((b->flags & NVN_TEXTURE_FLAG_COMPRESS) && !(pool->flags & NVN_TEXTURE_POOL_STORAGE_FLAGS)) {
        LOG("nvn-native: texture initialization pool=%llx flags=%x needs texture storage flags=280\n",
            (unsigned long long)b->pool, pool->flags);
        return 0;
    }
    if (!nvn_pool_range_fits(pool->size, b->offset, t->layout.size)) {
        LOG("nvn-native: texture initialization pool=%llx size=%llx: offset=%llx layout size=%llx does not fit\n",
            (unsigned long long)b->pool, (unsigned long long)pool->size, (unsigned long long)b->offset,
            (unsigned long long)t->layout.size);
        return 0;
    }
    t->address = pool->gpu_address + b->offset;
    if (nvn_trace_active())
        LOG("nvn-native: texture format=%x flags=%x target=%u width=%u height=%u levels=%u size=%llx\n",
            b->format, b->flags, b->target, b->width, b->height, b->levels, (unsigned long long)t->layout.size);
    return 1;
}
static void h_texture_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnTextureBuilder b = *texture_builder(c->x[1]);
    TextureRec* t = rec_get(c->x[0], NVN_OBJ_TEXTURE, sizeof *t);
    memset(t, 0, sizeof *t);
    u64 addr = c->x[0];
    int ok = texture_initialize(t, &b);
    if (!ok) nvn_objtab_remove(&objs, addr); /* later calls must not find a half-made texture */
    c->x[0] = ok;
    pm_unlock(&objs_lock);
}

static void h_texture_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    if (o && o->kind == NVN_OBJ_TEXTURE) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
/* The library flushes the pool cache and moves no data; the host write tracking already covers the pool. */
static void h_texture_flush_texels(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (!known_record(c->x[0], NVN_OBJ_TEXTURE))
        LOG("nvn-native: nvnTextureFlushTexels unknown texture=%llx\n", (unsigned long long)c->x[0]);
    pm_unlock(&objs_lock);
}

/* nvnTextureWriteTexels: texel data of a tightly packed linear source goes into the block linear bytes of a texture.
   The host checks the whole call before the first write, so a refusal leaves the texture unchanged. */
#define TEXTURE_WRITE_LOG_LIMIT 32u      /* log lines for the faults of the game in nvnTextureWriteTexels */
#define TEXTURE_WRITE_REGION_BYTES 0x18u /* the six u32 fields of the region */

typedef struct {
    const TextureRec* texture;
    const MemoryPoolRec* pool;
    NvnTextureLevel level;
    uint64_t source, src_row_bytes, src_layer_stride;
    uint64_t layer_first;
    uint32_t layer_count;
    uint32_t x_start, x_end, row_start, row_end;
} TextureWriteCall;

static uint64_t texture_write_ceil(uint64_t n, uint64_t d) { return (n + d - 1) / d; }
static int texture_write_mul(uint64_t a, uint64_t b, uint64_t* out) {
    if (a && b > UINT64_MAX / a) return 0;
    *out = a * b;
    return 1;
}
static int texture_write_add(uint64_t a, uint64_t b, uint64_t* out) {
    if (a > UINT64_MAX - b) return 0;
    *out = a + b;
    return 1;
}
/* Reads and checks the arguments of one call. 1 with call filled when the call writes; 0 means return now, and the
   reason of a refusal is in the log. Caller holds objs_lock. */
static int texture_write_check(CPU* c, TextureWriteCall* call) {
    const TextureRec* texture = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!texture) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels unknown texture=%llx\n",
                  (unsigned long long)c->x[0]);
        return 0;
    }
    const NvnTextureView* view = NULL;
    if (c->x[1]) {
        view = known_record(c->x[1], NVN_OBJ_TEXTURE_VIEW);
        if (!view) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels unknown view=%llx\n",
                      (unsigned long long)c->x[1]);
            return 0;
        }
        if (view->flags & NVN_VIEW_FORMAT) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                      "nvn-native: nvnTextureWriteTexels view=%llx format changes the level size\n",
                      (unsigned long long)c->x[1]);
            return 0;
        }
    }
    if (!native_guest_readable(c->x[2], TEXTURE_WRITE_REGION_BYTES)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels unreadable region=%llx\n",
                  (unsigned long long)c->x[2]);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels texture=%llx is linear\n",
                  (unsigned long long)c->x[0]);
        return 0;
    }
    if (texture->builder.target == NVN_TEXTURE_TARGET_BUFFER) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels texture=%llx has the BUFFER target\n",
                  (unsigned long long)c->x[0]);
        return 0;
    }
    if (texture->layout.sample_x != 1 || texture->layout.sample_y != 1) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels texture=%llx is multisample\n",
                  (unsigned long long)c->x[0]);
        return 0;
    }
    const uint64_t region = c->x[2];
    uint32_t rx = gr32(region), ry = gr32(region + 4), rz = gr32(region + 8);
    uint32_t width = gr32(region + 0xc), height = gr32(region + 0x10), depth = gr32(region + 0x14);
    /* The effective target 1D array moves y and height to the layer fields and writes one row. */
    uint32_t target = view ? view->target : texture->builder.target;
    int one_d_array = target == NVN_TEXTURE_TARGET_1D_ARRAY;
    uint32_t region_layer = one_d_array ? ry : rz;
    uint32_t layer_count = one_d_array ? height : depth;
    uint32_t region_row = one_d_array ? 0 : ry;
    uint32_t row_count = one_d_array ? 1 : height;
    if (!width || !layer_count || !row_count) return 0; /* the library writes nothing */
    uint32_t level = view && (view->flags & NVN_VIEW_LEVELS) ? view->base_level : 0;
    if (!nvn_texture_level(texture, level, &call->level)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                  "nvn-native: nvnTextureWriteTexels texture=%llx level=%u levels=%u is outside the texture\n",
                  (unsigned long long)c->x[0], level, (unsigned)texture->layout.levels);
        return 0;
    }
    const MemoryPoolRec* pool = known_record(texture->builder.pool, NVN_OBJ_MEMORY_POOL);
    if (!pool) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels unknown pool=%llx\n",
                  (unsigned long long)texture->builder.pool);
        return 0;
    }
    uint32_t base_layer = view && (view->flags & NVN_VIEW_LAYERS) ? view->base_layer : 0;
    uint64_t layer_first;
    if (!texture_write_add(base_layer, region_layer, &layer_first)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels layer arithmetic overflows\n");
        return 0;
    }
    uint64_t layer_end;
    /* A 3D level counts its slices in the level depth; the other targets count layers in the texture depth. */
    uint64_t max_layers = texture->layout.dim == 3 ? call->level.depth :
                          texture->layout.depth ? texture->layout.depth : 1;
    if (!texture_write_add(layer_first, layer_count, &layer_end) || layer_end > max_layers) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                  "nvn-native: nvnTextureWriteTexels layer=%llu count=%u depth=%llu is outside the texture\n",
                  (unsigned long long)layer_first, layer_count, (unsigned long long)texture->layout.depth);
        return 0;
    }
    uint64_t bw = texture->layout.block_width, bh = texture->layout.block_height;
    uint64_t bpb = texture->layout.bytes_per_block;
    uint64_t x_start = texture_write_ceil(rx, bw) * bpb;
    uint64_t src_row_bytes = texture_write_ceil(width, bw) * bpb;
    uint64_t x_end = x_start + src_row_bytes;
    uint64_t row_start = texture_write_ceil(region_row, bh);
    uint64_t rows = texture_write_ceil(row_count, bh);
    uint64_t row_end = row_start + rows;
    uint64_t src_layer_stride;
    if (!texture_write_mul(src_row_bytes, rows, &src_layer_stride) || x_end > UINT32_MAX ||
        row_end > UINT32_MAX) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels size arithmetic overflows\n");
        return 0;
    }
    uint64_t x_limit = (uint64_t)call->level.gobs_x * NVN_TEXTURE_GOB_WIDTH;
    /* The level holds whole blocks: a block is 2^bh GOBs, that is 8 << bh rows, high. */
    uint64_t row_block = (uint64_t)NVN_TEXTURE_GOB_HEIGHT << call->level.block_height_log2;
    uint64_t row_limit = (call->level.rows + row_block - 1) / row_block * row_block;
    if (x_end > x_limit) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                  "nvn-native: nvnTextureWriteTexels x_end=%llu is outside the level columns=%llu\n",
                  (unsigned long long)x_end, (unsigned long long)x_limit);
        return 0;
    }
    if (row_end > row_limit) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                  "nvn-native: nvnTextureWriteTexels row_end=%llu is outside the level rows=%llu\n",
                  (unsigned long long)row_end, (unsigned long long)row_limit);
        return 0;
    }
    /* The source holds only the region: its rows are src_row_bytes apart and start at the first byte (d8232a0). */
    uint64_t source_length, source_end;
    if (!texture_write_mul(layer_count, src_layer_stride, &source_length) ||
        !texture_write_add(c->x[3], source_length, &source_end)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels source arithmetic overflows\n");
        return 0;
    }
    if (!native_guest_readable(c->x[3], source_length)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels unreadable source=%llx bytes=%llu\n",
                  (unsigned long long)c->x[3], (unsigned long long)source_length);
        return 0;
    }
    if (texture->layout.dim == 3) {
        /* A 3D level is one layer: its slices share the level base and the level size covers all of them. */
        uint64_t dst;
        if (!texture_write_add(pool->storage, texture->builder.offset, &dst) ||
            !texture_write_add(dst, call->level.offset, &dst)) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels address arithmetic overflows\n");
            return 0;
        }
        if (!native_guest_writable(dst, call->level.size)) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                      "nvn-native: nvnTextureWriteTexels unwritable 3D level destination=%llx bytes=%llu\n",
                      (unsigned long long)dst, (unsigned long long)call->level.size);
            return 0;
        }
    } else {
        uint64_t layer_stride = texture->layout.layer_stride;
        for (uint32_t i = 0; i < layer_count; i++) {
            uint64_t layer_offset, dst;
            if (!texture_write_mul(layer_first + i, layer_stride, &layer_offset) ||
                !texture_write_add(texture->builder.offset, layer_offset, &layer_offset) ||
                !texture_write_add(pool->storage, layer_offset, &dst) ||
                !texture_write_add(dst, call->level.offset, &dst)) {
                LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: nvnTextureWriteTexels address arithmetic overflows\n");
                return 0;
            }
            if (!native_guest_writable(dst, call->level.size)) {
                LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT,
                          "nvn-native: nvnTextureWriteTexels unwritable layer=%llu destination=%llx bytes=%llu\n",
                          (unsigned long long)(layer_first + i), (unsigned long long)dst,
                          (unsigned long long)call->level.size);
                return 0;
            }
        }
    }
    call->texture = texture;
    call->pool = pool;
    call->source = c->x[3];
    call->src_row_bytes = src_row_bytes;
    call->src_layer_stride = src_layer_stride;
    call->layer_first = layer_first;
    call->layer_count = layer_count;
    call->x_start = (uint32_t)x_start;
    call->x_end = (uint32_t)x_end;
    call->row_start = (uint32_t)row_start;
    call->row_end = (uint32_t)row_end;
    return 1;
}
/* Caller holds objs_lock and texture_write_check accepted the whole call. Copies one slice: slice z of a 3D level
   at the level base, layer i of a 2D level at its layer stride. */
static void texture_write_slice(const TextureWriteCall* call, uint32_t index) {
    uint64_t layer = call->layer_first + index;
    int is_3d = call->texture->layout.dim == 3;
    uint64_t dst = call->pool->storage + call->texture->builder.offset + call->level.offset +
                   (is_3d ? 0 : layer * call->texture->layout.layer_stride);
    const uint8_t* src = GP(call->source + index * call->src_layer_stride);
    nvn_texture_write_rows(GP(dst), &call->level, call->x_start, call->x_end, call->row_start, call->row_end,
                           src, call->src_row_bytes, is_3d ? (uint32_t)layer : 0);
}
static void h_texture_write_texels(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    TextureWriteCall call;
    if (texture_write_check(c, &call))
        for (uint32_t i = 0; i < call.layer_count; i++) texture_write_slice(&call, i);
    pm_unlock(&objs_lock);
}

/* G6: command buffers (decisions 3 to 5 of the 3c design). The host records ops and never writes the command or control
   memory of the game. A published list is owned by the command buffer until the game gives control memory that overlaps
   the memory the list had: the library's handle is a control memory address, so that is when a handle ends. */
typedef struct { uint64_t handle, begin, end; } OwnedList;
typedef struct {
    uint64_t device, callback, callback_data;
    uint64_t command_pool, command_offset, command_size;
    uint64_t control_begin, control_end;
    uint32_t command_adds, control_adds, dropped_ops;
    int recording;
    NvnOpList open;
    OwnedList* owned;
    uint32_t owned_count, owned_cap;
} CommandBufferRec;
#define COMMAND_LOG_LIMIT       16u   /* log lines for faults of the game or of the host in recording */
#define COMMAND_CALL_COUNT_MAX  4096u /* handles in one CallCommands */
static uint32_t command_log_count;
#define COMMAND_LOG(...)                                       \
    do {                                                       \
        if (command_log_count++ < COMMAND_LOG_LIMIT) LOG(__VA_ARGS__); \
    } while (0)

static CommandBufferRec* command_buffer(u64 addr, const char* operation) {
    CommandBufferRec* cb = known_record(addr, NVN_OBJ_COMMAND_BUFFER);
    if (!cb) COMMAND_LOG("nvn-native: %s unknown command buffer=%llx\n", operation, (unsigned long long)addr);
    return cb;
}
static void command_buffer_drop_owned(CommandBufferRec* cb, int (*test)(const OwnedList*, uint64_t, uint64_t),
                                      uint64_t begin, uint64_t end) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < cb->owned_count; i++) {
        if (test(&cb->owned[i], begin, end))
            nvn_list_unref(&lists, cb->owned[i].handle);
        else
            cb->owned[kept++] = cb->owned[i];
    }
    cb->owned_count = kept;
}
static int owned_any(const OwnedList* l, uint64_t begin, uint64_t end) { (void)l; (void)begin; (void)end; return 1; }
static int owned_overlaps(const OwnedList* l, uint64_t begin, uint64_t end) {
    return l->begin < end && begin < l->end;
}
static void command_buffer_release(CommandBufferRec* cb) {
    nvn_oplist_free(&cb->open);
    command_buffer_drop_owned(cb, owned_any, 0, 0);
    free(cb->owned);
    cb->owned = NULL;
    cb->owned_cap = 0;
}
static void h_command_buffer_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* old = known_record(c->x[0], NVN_OBJ_COMMAND_BUFFER);
    if (old) command_buffer_release(old); /* initialized again without Finalize */
    CommandBufferRec* cb = rec_get(c->x[0], NVN_OBJ_COMMAND_BUFFER, sizeof *cb);
    memset(cb, 0, sizeof *cb);
    cb->device = c->x[1];
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}
static void h_command_buffer_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = known_record(c->x[0], NVN_OBJ_COMMAND_BUFFER);
    if (cb) {
        command_buffer_release(cb);
        nvn_objtab_remove(&objs, c->x[0]);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_memory_callback(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferSetMemoryCallback");
    if (cb) cb->callback = c->x[1]; /* stored, never called: the host has no command memory to fill */
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_memory_callback_data(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferSetMemoryCallbackData");
    if (cb) cb->callback_data = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_command_buffer_add_command_memory(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferAddCommandMemory");
    if (cb) {
        cb->command_pool = c->x[1];
        cb->command_offset = c->x[2];
        cb->command_size = c->x[3];
        cb->command_adds++;
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_add_control_memory(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferAddControlMemory");
    if (cb) {
        cb->control_begin = c->x[1];
        cb->control_end = c->x[1] <= UINT64_MAX - c->x[2] ? c->x[1] + c->x[2] : UINT64_MAX;
        cb->control_adds++;
        command_buffer_drop_owned(cb, owned_overlaps, cb->control_begin, cb->control_end);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_begin_recording(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferBeginRecording");
    if (cb) {
        if (cb->recording) {
            COMMAND_LOG("nvn-native: BeginRecording during a recording, command buffer=%llx drops %u ops\n",
                        (unsigned long long)c->x[0], cb->open.ops);
            nvn_oplist_free(&cb->open);
        }
        cb->recording = 1;
    }
    pm_unlock(&objs_lock);
}
static int command_buffer_own(CommandBufferRec* cb, uint64_t handle) {
    if (cb->owned_count == cb->owned_cap) {
        uint32_t cap = cb->owned_cap ? cb->owned_cap * 2 : 16;
        OwnedList* fresh = realloc(cb->owned, (size_t)cap * sizeof *fresh);
        if (!fresh) return 0;
        cb->owned = fresh;
        cb->owned_cap = cap;
    }
    cb->owned[cb->owned_count++] = (OwnedList){handle, cb->control_begin, cb->control_end};
    return 1;
}
static void h_command_buffer_end_recording(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferEndRecording");
    uint64_t handle = 0;
    if (cb && cb->recording) {
        cb->recording = 0;
        handle = nvn_list_publish(&lists, &cb->open);
        if (!handle) fatal("nvn-native: out of memory for a command list\n");
        if (!command_buffer_own(cb, handle)) fatal("nvn-native: out of memory for the lists of a command buffer\n");
    }
    c->x[0] = handle; /* 0 outside a recording, as in the library */
    pm_unlock(&objs_lock);
}
/* Appends one op to the open list. An op outside a recording or one that cannot be stored is dropped with a log line. */
static void command_record(CommandBufferRec* cb, u64 addr, const char* operation, NvnOpCode code, const void* payload,
                           uint32_t bytes) {
    if (!cb->recording) {
        cb->dropped_ops++;
        __atomic_fetch_add(&dropped_ops, 1, __ATOMIC_RELAXED);
        COMMAND_LOG("nvn-native: %s outside a recording, command buffer=%llx: dropped\n", operation,
                    (unsigned long long)addr);
        return;
    }
    if (!nvn_oplist_append(&cb->open, code, payload, bytes)) fatal("nvn-native: out of memory for a command op\n");
}
static void command_dropped(CommandBufferRec* cb, const char* operation, const char* reason, u64 value) {
    cb->dropped_ops++;
    __atomic_fetch_add(&dropped_ops, 1, __ATOMIC_RELAXED);
    COMMAND_LOG("nvn-native: %s %s (%llx): dropped\n", operation, reason, (unsigned long long)value);
}
static int command_object(u64 addr, NvnObjKind kind, uint32_t* generation) {
    NvnObj* o = nvn_objtab_find(&objs, addr);
    if (!o || o->kind != (uint32_t)kind) return 0;
    *generation = o->generation;
    return 1;
}
#define COMMAND_POOL_HANDLER(name, operation, kind, code)                                                   \
    static void h_command_buffer_##name(CPU* c, u64 entry) {                                                \
        (void)entry;                                                                                        \
        pm_lock(&objs_lock);                                                                                \
        CommandBufferRec* cb = command_buffer(c->x[0], operation);                                          \
        NvnOpPool op = {c->x[1], 0, 0};                                                                     \
        if (!cb) {                                                                                          \
        } else if (!command_object(op.pool, kind, &op.generation))                                          \
            command_dropped(cb, operation, "unknown pool", op.pool);                                        \
        else                                                                                                \
            command_record(cb, c->x[0], operation, code, &op, sizeof op);                                   \
        pm_unlock(&objs_lock);                                                                              \
    }
COMMAND_POOL_HANDLER(set_texture_pool, "nvnCommandBufferSetTexturePool", NVN_OBJ_TEXTURE_POOL, NVN_OP_SET_TEXTURE_POOL)
COMMAND_POOL_HANDLER(set_sampler_pool, "nvnCommandBufferSetSamplerPool", NVN_OBJ_SAMPLER_POOL, NVN_OP_SET_SAMPLER_POOL)

/* one target of SetRenderTargets: the texture by address and generation, the view copied. 0 and a log line when the game
   named an object the host does not know. */
static int command_target(u64 texture, u64 view, NvnOpTarget* out, CommandBufferRec* cb, const char* operation) {
    *out = (NvnOpTarget){0};
    if (!texture) return 1;
    if (!command_object(texture, NVN_OBJ_TEXTURE, &out->generation)) {
        command_dropped(cb, operation, "unknown texture", texture);
        return 0;
    }
    out->texture = texture;
    if (!view) return 1;
    const NvnTextureView* v = known_record(view, NVN_OBJ_TEXTURE_VIEW);
    if (!v) {
        command_dropped(cb, operation, "unknown texture view", view);
        return 0;
    }
    out->view = *v;
    out->has_view = 1;
    return 1;
}
static void h_command_buffer_set_render_targets(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetRenderTargets";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    u64 textures = c->x[2], views = c->x[3];
    u32 count = (u32)c->x[1];
    NvnOpRenderTargets op;
    memset(&op, 0, sizeof op);
    if (!cb) goto done;
    if (count > NVN_RENDER_TARGETS_MAX) {
        command_dropped(cb, name, "too many targets", count);
        goto done;
    }
    if (count && (!native_guest_readable(textures, (u64)count * 8) ||
                  (views && !native_guest_readable(views, (u64)count * 8)))) {
        command_dropped(cb, name, "unreadable target array", textures);
        goto done;
    }
    op.count = count;
    for (u32 i = 0; i < count; i++) {
        u64 view = views ? gr64(views + (u64)i * 8) : 0;
        if (!command_target(gr64(textures + (u64)i * 8), view, &op.color[i], cb, name)) goto done;
    }
    if (!command_target(c->x[4], c->x[5], &op.depth, cb, name)) goto done;
    command_record(cb, c->x[0], name, NVN_OP_SET_RENDER_TARGETS, &op, sizeof op);
done:
    pm_unlock(&objs_lock);
}
/* SetViewports and SetScissors: one op for each rectangle, so an op stays small. */
static void h_command_buffer_set_viewports(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetViewports";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    u32 first = (u32)c->x[1], count = (u32)c->x[2];
    if (cb && (int32_t)count > 0) {
        if (first >= NVN_VIEWPORTS_MAX || count > NVN_VIEWPORTS_MAX - first)
            command_dropped(cb, name, "range outside the 16 viewports", ((u64)first << 32) | count);
        else if (!native_guest_readable(c->x[3], (u64)count * 16))
            command_dropped(cb, name, "unreadable array", c->x[3]);
        else
            for (u32 i = 0; i < count; i++) {
                NvnOpViewport op = {first + i};
                u32 bits[4];
                for (u32 k = 0; k < 4; k++) bits[k] = gr32(c->x[3] + (u64)i * 16 + k * 4);
                memcpy(&op.x, bits, sizeof bits);
                command_record(cb, c->x[0], name, NVN_OP_SET_VIEWPORT, &op, sizeof op);
            }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_scissors(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetScissors";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    u32 first = (u32)c->x[1], count = (u32)c->x[2];
    if (cb && (int32_t)count > 0) {
        if (first >= NVN_VIEWPORTS_MAX || count > NVN_VIEWPORTS_MAX - first)
            command_dropped(cb, name, "range outside the 16 scissors", ((u64)first << 32) | count);
        else if (!native_guest_readable(c->x[3], (u64)count * 16))
            command_dropped(cb, name, "unreadable array", c->x[3]);
        else
            for (u32 i = 0; i < count; i++) {
                NvnOpScissor op = {first + i};
                u32 bits[4];
                for (u32 k = 0; k < 4; k++) bits[k] = gr32(c->x[3] + (u64)i * 16 + k * 4);
                memcpy(&op.x, bits, sizeof bits);
                command_record(cb, c->x[0], name, NVN_OP_SET_SCISSOR, &op, sizeof op);
            }
    }
    pm_unlock(&objs_lock);
}
/* nvnCommandBufferBindTexture(cb, stage, slot, handle). The library checks neither the stage nor the slot; the host
   refuses a value that its bind table cannot hold. */
static void h_command_buffer_bind_texture(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTexture";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpBindTexture op = {(u32)c->x[1], (u32)c->x[2], c->x[3]};
    if (!cb) {
    } else if (op.stage > NVN_BIND_STAGE_MAX)
        command_dropped(cb, name, "stage outside the bind stages", op.stage);
    else if (op.slot >= NVN_BIND_SLOT_MAX)
        command_dropped(cb, name, "slot outside the bind slots", op.slot);
    else
        command_record(cb, c->x[0], name, NVN_OP_BIND_TEXTURE, &op, sizeof op);
    pm_unlock(&objs_lock);
}
/* One buffer bind: the stage, the slot limit and the bind table are the host's; the library checks none of them. */
static void command_bind_buffer(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                uint32_t stage, uint32_t slot, uint32_t slot_max, uint64_t buffer, uint64_t size) {
    NvnOpBindBuffer op = {kind, stage, slot, 0, buffer, size};
    if (stage > NVN_BIND_STAGE_MAX)
        command_dropped(cb, operation, "stage outside the bind stages", stage);
    else if (slot >= slot_max)
        command_dropped(cb, operation, "slot outside the bind slots", slot);
    else
        command_record(cb, addr, operation, NVN_OP_BIND_BUFFER, &op, sizeof op);
}
/* The range checks of a plural bind, done before any entry is read. 1 when the call can be recorded. */
static int command_bind_range_ok(CommandBufferRec* cb, const char* operation, uint32_t stage, uint32_t first,
                                 int32_t count, u64 array, uint32_t slot_max) {
    if (stage > NVN_BIND_STAGE_MAX) {
        command_dropped(cb, operation, "stage outside the bind stages", stage);
        return 0;
    }
    if ((u64)first + (u64)count > slot_max) {
        command_dropped(cb, operation, "range outside the bind slots", ((u64)first << 32) | (u32)count);
        return 0;
    }
    if (!native_guest_readable(array, (u64)count * 16)) {
        command_dropped(cb, operation, "unreadable array", array);
        return 0;
    }
    return 1;
}
/* A plural bind whose array entry is {u64 addr; u32 size; u32 pad}. */
static void command_bind_buffers_sized(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                       uint32_t stage, uint32_t first, int32_t count, u64 array, uint32_t slot_max) {
    if (!command_bind_range_ok(cb, operation, stage, first, count, array, slot_max)) return;
    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        u64 at = array + (u64)i * 16;
        NvnOpBindBuffer op = {kind, stage, first + i, 0, gr64(at), (uint32_t)gr32(at + 8)};
        command_record(cb, addr, operation, NVN_OP_BIND_BUFFER, &op, sizeof op);
    }
}
/* A plural bind whose array entry is {u64 addr; u64 size}. The vertex and transform feedback binds have no stage. */
static void command_bind_buffers_full(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                      uint32_t first, int32_t count, u64 array, uint32_t slot_max) {
    if (!command_bind_range_ok(cb, operation, 0, first, count, array, slot_max)) return;
    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        u64 at = array + (u64)i * 16;
        NvnOpBindBuffer op = {kind, 0, first + i, 0, gr64(at), gr64(at + 8)};
        command_record(cb, addr, operation, NVN_OP_BIND_BUFFER, &op, sizeof op);
    }
}
static void h_command_buffer_bind_uniform_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindUniformBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_UNIFORM, (u32)c->x[1], (u32)c->x[2],
                                NVN_UNIFORM_SLOT_MAX, c->x[3], (uint32_t)c->x[4]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_storage_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindStorageBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_STORAGE, (u32)c->x[1], (u32)c->x[2],
                                NVN_STORAGE_SLOT_MAX, c->x[3], (uint32_t)c->x[4]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_vertex_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindVertexBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_VERTEX, 0, (u32)c->x[1], NVN_VERTEX_SLOT_MAX,
                                c->x[2], c->x[3]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_transform_feedback_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTransformFeedbackBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_TRANSFORM_FEEDBACK, 0, (u32)c->x[1], NVN_XFB_SLOT_MAX,
                                c->x[2], c->x[3]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_uniform_buffers(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindUniformBuffers";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    int32_t count = (int32_t)c->x[3];
    if (cb && count > 0)
        command_bind_buffers_sized(cb, c->x[0], name, NVN_BIND_UNIFORM, (u32)c->x[1], (u32)c->x[2], count,
                                   c->x[4], NVN_UNIFORM_SLOT_MAX);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_storage_buffers(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindStorageBuffers";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    int32_t count = (int32_t)c->x[3];
    if (cb && count > 0)
        command_bind_buffers_sized(cb, c->x[0], name, NVN_BIND_STORAGE, (u32)c->x[1], (u32)c->x[2], count,
                                   c->x[4], NVN_STORAGE_SLOT_MAX);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_vertex_buffers(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindVertexBuffers";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    int32_t count = (int32_t)c->x[2];
    if (cb && count > 0)
        command_bind_buffers_full(cb, c->x[0], name, NVN_BIND_VERTEX, (u32)c->x[1], count, c->x[3],
                                  NVN_VERTEX_SLOT_MAX);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_transform_feedback_buffers(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTransformFeedbackBuffers";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    int32_t count = (int32_t)c->x[2];
    if (cb && count > 0)
        command_bind_buffers_full(cb, c->x[0], name, NVN_BIND_TRANSFORM_FEEDBACK, (u32)c->x[1], count, c->x[3],
                                  NVN_XFB_SLOT_MAX);
    pm_unlock(&objs_lock);
}
/* G3: the six state objects. The record holds the words of the library layout, so the formulas of the tables apply. */
typedef struct {
    NvnObjKind obj;
    uint32_t record_words, bind_words;
} StateKindInfo;
static const StateKindInfo state_kinds[NVN_STATE_COUNT] = {
    {NVN_OBJ_COLOR_STATE, 1, 1},
    {NVN_OBJ_DEPTH_STENCIL_STATE, 2, 2},
    {NVN_OBJ_BLEND_STATE, 2, 2},
    {NVN_OBJ_POLYGON_STATE, 1, 1},
    {NVN_OBJ_MULTISAMPLE_STATE, 6, 5},
    {NVN_OBJ_CHANNEL_MASK_STATE, 1, 1},
};
static uint32_t* state_words(u64 addr, NvnStateKind kind) {
    const StateKindInfo* info = &state_kinds[kind];
    return rec_get(addr, info->obj, (size_t)info->record_words * sizeof(uint32_t));
}
/* The bind copies the words that the replay stores. The advanced blend path of the library is not decoded. */
static void command_bind_state(CommandBufferRec* cb, u64 addr, u64 state_addr, const char* operation,
                               NvnStateKind kind) {
    const StateKindInfo* info = &state_kinds[kind];
    const uint32_t* state = rec_get(state_addr, info->obj, (size_t)info->record_words * sizeof(uint32_t));
    NvnOpBindState op = {(uint32_t)kind, info->bind_words, {0}};
    for (uint32_t i = 0; i < info->bind_words; i++) op.w[i] = state[i];
    if (kind == NVN_STATE_BLEND && ((op.w[0] >> 8) & 0x3Fu) != 0)
        command_dropped(cb, operation, "blend advanced mode is not supported", op.w[0]);
    else
        command_record(cb, addr, operation, NVN_OP_BIND_STATE, &op, sizeof op);
}
#define BIND_STATE_HANDLER(name, operation, kind)                        \
    static void h_command_buffer_bind_##name(CPU* c, u64 entry) {        \
        (void)entry;                                                     \
        pm_lock(&objs_lock);                                             \
        CommandBufferRec* cb = command_buffer(c->x[0], operation);       \
        if (cb) command_bind_state(cb, c->x[0], c->x[1], operation, kind); \
        pm_unlock(&objs_lock);                                           \
    }
BIND_STATE_HANDLER(color_state, "nvnCommandBufferBindColorState", NVN_STATE_COLOR)
BIND_STATE_HANDLER(depth_stencil_state, "nvnCommandBufferBindDepthStencilState", NVN_STATE_DEPTH_STENCIL)
BIND_STATE_HANDLER(blend_state, "nvnCommandBufferBindBlendState", NVN_STATE_BLEND)
BIND_STATE_HANDLER(polygon_state, "nvnCommandBufferBindPolygonState", NVN_STATE_POLYGON)
BIND_STATE_HANDLER(multisample_state, "nvnCommandBufferBindMultisampleState", NVN_STATE_MULTISAMPLE)
BIND_STATE_HANDLER(channel_mask_state, "nvnCommandBufferBindChannelMaskState", NVN_STATE_CHANNEL_MASK)
#undef BIND_STATE_HANDLER

#define STATE_INSERT_HANDLER(name, kind, mask, shift)          \
    static void h_##name(CPU* c, u64 entry) {                  \
        (void)entry;                                           \
        pm_lock(&objs_lock);                                   \
        uint32_t* w = state_words(c->x[0], kind);              \
        w[0] = nvn_state_insert(w[0], mask, shift, (u32)c->x[1]); \
        pm_unlock(&objs_lock);                                 \
    }
STATE_INSERT_HANDLER(blend_state_set_blend_target, NVN_STATE_BLEND, 0x7u, 0)
STATE_INSERT_HANDLER(color_state_set_logic_op, NVN_STATE_COLOR, 0x00FF0000u, 16)
STATE_INSERT_HANDLER(color_state_set_alpha_test, NVN_STATE_COLOR, 0x0F000000u, 24)
STATE_INSERT_HANDLER(depth_stencil_state_set_depth_func, NVN_STATE_DEPTH_STENCIL, 0x000000F0u, 4)
STATE_INSERT_HANDLER(polygon_state_set_cull_face, NVN_STATE_POLYGON, 0x3u, 0)
STATE_INSERT_HANDLER(polygon_state_set_front_face, NVN_STATE_POLYGON, 0x4u, 2)
STATE_INSERT_HANDLER(polygon_state_set_polygon_mode, NVN_STATE_POLYGON, 0x18u, 3)
STATE_INSERT_HANDLER(polygon_state_set_polygon_offset_enables, NVN_STATE_POLYGON, 0x78000u, 15)
STATE_INSERT_HANDLER(multisample_state_set_samples, NVN_STATE_MULTISAMPLE, 0x1F00u, 8)
STATE_INSERT_HANDLER(multisample_state_set_raster_samples, NVN_STATE_MULTISAMPLE, 0xF8000u, 15)
STATE_INSERT_HANDLER(multisample_state_set_coverage_modulation_mode, NVN_STATE_MULTISAMPLE, 0x6000u, 13)
STATE_INSERT_HANDLER(multisample_state_set_coverage_to_color_output, NVN_STATE_MULTISAMPLE, 0xE00000u, 21)
#undef STATE_INSERT_HANDLER

#define STATE_FLAG_HANDLER(name, kind, bit)                    \
    static void h_##name(CPU* c, u64 entry) {                  \
        (void)entry;                                           \
        pm_lock(&objs_lock);                                   \
        uint32_t* w = state_words(c->x[0], kind);              \
        w[0] = nvn_state_set_flag(w[0], bit, (u32)c->x[1]);    \
        pm_unlock(&objs_lock);                                 \
    }
STATE_FLAG_HANDLER(depth_stencil_state_set_depth_test_enable, NVN_STATE_DEPTH_STENCIL, 0)
STATE_FLAG_HANDLER(depth_stencil_state_set_depth_write_enable, NVN_STATE_DEPTH_STENCIL, 1)
STATE_FLAG_HANDLER(depth_stencil_state_set_stencil_test_enable, NVN_STATE_DEPTH_STENCIL, 2)
STATE_FLAG_HANDLER(multisample_state_set_multisample_enable, NVN_STATE_MULTISAMPLE, 0)
STATE_FLAG_HANDLER(multisample_state_set_alpha_to_coverage_enable, NVN_STATE_MULTISAMPLE, 1)
STATE_FLAG_HANDLER(multisample_state_set_alpha_to_coverage_dither, NVN_STATE_MULTISAMPLE, 2)
STATE_FLAG_HANDLER(multisample_state_set_sample_locations_enable, NVN_STATE_MULTISAMPLE, 3)
STATE_FLAG_HANDLER(multisample_state_set_sample_locations_grid_enable, NVN_STATE_MULTISAMPLE, 4)
STATE_FLAG_HANDLER(multisample_state_set_coverage_to_color_enable, NVN_STATE_MULTISAMPLE, 20)
#undef STATE_FLAG_HANDLER

static void h_blend_state_set_blend_func(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[1] = nvn_blend_state_set_blend_func(w[1], (u32)c->x[1], (u32)c->x[2], (u32)c->x[3], (u32)c->x[4]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_set_blend_equation(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[0] = nvn_blend_state_set_blend_equation(w[0], (u32)c->x[1], (u32)c->x[2]);
    pm_unlock(&objs_lock);
}
static void h_channel_mask_state_set_channel_mask(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_CHANNEL_MASK);
    w[0] = nvn_channel_mask_state_set_channel_mask(w[0], (u32)c->x[1], (u32)c->x[2], (u32)c->x[3],
                                                   (u32)c->x[4], (u32)c->x[5]);
    pm_unlock(&objs_lock);
}
static void h_color_state_set_blend_enable(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_COLOR);
    w[0] = nvn_color_state_set_blend_enable(w[0], (u32)c->x[1], (u32)c->x[2]);
    pm_unlock(&objs_lock);
}
static void h_depth_stencil_state_set_stencil_func(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_DEPTH_STENCIL);
    w[1] = nvn_depth_stencil_state_set_stencil_func(w[1], (u32)c->x[1], (u32)c->x[2]);
    pm_unlock(&objs_lock);
}
static void h_depth_stencil_state_set_stencil_op(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_DEPTH_STENCIL);
    w[1] = nvn_depth_stencil_state_set_stencil_op(w[1], (u32)c->x[1], (u32)c->x[2], (u32)c->x[3], (u32)c->x[4]);
    pm_unlock(&objs_lock);
}

#define STATE_DEFAULTS_HANDLER(name, kind, defaults) \
    static void h_##name(CPU* c, u64 entry) {         \
        (void)entry;                                  \
        pm_lock(&objs_lock);                          \
        defaults(state_words(c->x[0], kind));         \
        pm_unlock(&objs_lock);                        \
    }
STATE_DEFAULTS_HANDLER(blend_state_set_defaults, NVN_STATE_BLEND, nvn_blend_state_defaults)
STATE_DEFAULTS_HANDLER(channel_mask_state_set_defaults, NVN_STATE_CHANNEL_MASK, nvn_channel_mask_state_defaults)
STATE_DEFAULTS_HANDLER(color_state_set_defaults, NVN_STATE_COLOR, nvn_color_state_defaults)
STATE_DEFAULTS_HANDLER(depth_stencil_state_set_defaults, NVN_STATE_DEPTH_STENCIL, nvn_depth_stencil_state_defaults)
STATE_DEFAULTS_HANDLER(multisample_state_set_defaults, NVN_STATE_MULTISAMPLE, nvn_multisample_state_defaults)
STATE_DEFAULTS_HANDLER(polygon_state_set_defaults, NVN_STATE_POLYGON, nvn_polygon_state_defaults)
#undef STATE_DEFAULTS_HANDLER

#define STATE_GET_HANDLER(name, kind, value)      \
    static void h_##name(CPU* c, u64 entry) {     \
        (void)entry;                              \
        pm_lock(&objs_lock);                      \
        const uint32_t* w = state_words(c->x[0], kind); \
        c->x[0] = (value);                        \
        pm_unlock(&objs_lock);                    \
    }
STATE_GET_HANDLER(blend_state_get_blend_target, NVN_STATE_BLEND, nvn_blend_state_get_blend_target(w[0]))
STATE_GET_HANDLER(color_state_get_alpha_test, NVN_STATE_COLOR, nvn_color_state_get_alpha_test(w[0]))
STATE_GET_HANDLER(color_state_get_blend_enable, NVN_STATE_COLOR,
                  nvn_color_state_get_blend_enable(w[0], (u32)c->x[1]))
STATE_GET_HANDLER(depth_stencil_state_get_depth_func, NVN_STATE_DEPTH_STENCIL,
                  nvn_depth_stencil_state_get_depth_func(w[0]))
STATE_GET_HANDLER(depth_stencil_state_get_depth_test_enable, NVN_STATE_DEPTH_STENCIL,
                  nvn_depth_stencil_state_get_depth_test_enable(w[0]))
STATE_GET_HANDLER(depth_stencil_state_get_depth_write_enable, NVN_STATE_DEPTH_STENCIL,
                  nvn_depth_stencil_state_get_depth_write_enable(w[0]))
STATE_GET_HANDLER(multisample_state_get_multisample_enable, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_multisample_enable(w[0]))
STATE_GET_HANDLER(multisample_state_get_samples, NVN_STATE_MULTISAMPLE, nvn_multisample_state_get_samples(w[0]))
STATE_GET_HANDLER(multisample_state_get_alpha_to_coverage_enable, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_alpha_to_coverage_enable(w[0]))
STATE_GET_HANDLER(multisample_state_get_alpha_to_coverage_dither, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_alpha_to_coverage_dither(w[0]))
STATE_GET_HANDLER(multisample_state_get_coverage_modulation_mode, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_coverage_modulation_mode(w[0]))
STATE_GET_HANDLER(multisample_state_get_raster_samples, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_raster_samples(w[0]))
STATE_GET_HANDLER(multisample_state_get_coverage_to_color_enable, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_coverage_to_color_enable(w[0]))
STATE_GET_HANDLER(multisample_state_get_coverage_to_color_output, NVN_STATE_MULTISAMPLE,
                  nvn_multisample_state_get_coverage_to_color_output(w[0]))
STATE_GET_HANDLER(polygon_state_get_cull_face, NVN_STATE_POLYGON, nvn_polygon_state_get_cull_face(w[0]))
STATE_GET_HANDLER(polygon_state_get_front_face, NVN_STATE_POLYGON, nvn_polygon_state_get_front_face(w[0]))
#undef STATE_GET_HANDLER

/* The Get functions with output pointers check every destination before the first store. */
static void h_blend_state_get_blend_func(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnBlendStateGetBlendFunc";
    pm_lock(&objs_lock);
    if (!native_guest_writable(c->x[1], sizeof(uint32_t)) || !native_guest_writable(c->x[2], sizeof(uint32_t)) ||
        !native_guest_writable(c->x[3], sizeof(uint32_t)) || !native_guest_writable(c->x[4], sizeof(uint32_t))) {
        LOG("nvn-native: %s unwritable output\n", name);
        pm_unlock(&objs_lock);
        return;
    }
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    uint32_t out[4];
    nvn_blend_state_get_blend_func(w[1], out);
    for (uint32_t i = 0; i < 4; i++) gw32(c->x[1 + i], out[i]);
    pm_unlock(&objs_lock);
}
static void h_channel_mask_state_get_channel_mask(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnChannelMaskStateGetChannelMask";
    pm_lock(&objs_lock);
    if (!native_guest_writable(c->x[2], 1) || !native_guest_writable(c->x[3], 1) ||
        !native_guest_writable(c->x[4], 1) || !native_guest_writable(c->x[5], 1)) {
        LOG("nvn-native: %s unwritable output\n", name);
        pm_unlock(&objs_lock);
        return;
    }
    const uint32_t* w = state_words(c->x[0], NVN_STATE_CHANNEL_MASK);
    uint8_t out[4];
    nvn_channel_mask_state_get_channel_mask(w[0], (u32)c->x[1], out);
    for (uint32_t i = 0; i < 4; i++) *GP(c->x[2 + i]) = out[i];
    pm_unlock(&objs_lock);
}
static void h_color_state_get_logic_op(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_COLOR);
    c->x[0] = nvn_color_state_get_logic_op(w[0]);
    pm_unlock(&objs_lock);
}
static void h_depth_stencil_state_get_stencil_test_enable(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_DEPTH_STENCIL);
    c->x[0] = nvn_depth_stencil_state_get_stencil_test_enable(w[0]);
    pm_unlock(&objs_lock);
}
static void h_depth_stencil_state_get_stencil_func(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_DEPTH_STENCIL);
    c->x[0] = nvn_depth_stencil_state_get_stencil_func(w[1], (u32)c->x[1]);
    pm_unlock(&objs_lock);
}
static void h_polygon_state_get_polygon_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_POLYGON);
    c->x[0] = nvn_polygon_state_get_polygon_mode(w[0]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_get_blend_equation(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnBlendStateGetBlendEquation";
    pm_lock(&objs_lock);
    if (!native_guest_writable(c->x[1], sizeof(uint32_t)) || !native_guest_writable(c->x[2], sizeof(uint32_t))) {
        LOG("nvn-native: %s unwritable output\n", name);
        pm_unlock(&objs_lock);
        return;
    }
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    uint32_t rgb = 0, alpha = 0;
    nvn_blend_state_get_blend_equation(w[0], &rgb, &alpha);
    gw32(c->x[1], rgb);
    gw32(c->x[2], alpha);
    pm_unlock(&objs_lock);
}
static void h_depth_stencil_state_get_stencil_op(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnDepthStencilStateGetStencilOp";
    pm_lock(&objs_lock);
    if (!native_guest_writable(c->x[2], sizeof(uint32_t)) || !native_guest_writable(c->x[3], sizeof(uint32_t)) ||
        !native_guest_writable(c->x[4], sizeof(uint32_t))) {
        LOG("nvn-native: %s unwritable output\n", name);
        pm_unlock(&objs_lock);
        return;
    }
    const uint32_t* w = state_words(c->x[0], NVN_STATE_DEPTH_STENCIL);
    uint32_t out[3];
    nvn_depth_stencil_state_get_stencil_op(w[1], (u32)c->x[1], out);
    for (uint32_t i = 0; i < 3; i++) gw32(c->x[2 + i], out[i]);
    pm_unlock(&objs_lock);
}
static void h_polygon_state_get_polygon_offset_enables(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_POLYGON);
    c->x[0] = nvn_polygon_state_get_polygon_offset_enables(w[0]);
    pm_unlock(&objs_lock);
}

/* G5: the vertex state objects. Their records start zeroed, which is the default of both kinds. */
static uint32_t* vertex_attrib_words(u64 addr) {
    return rec_get(addr, NVN_OBJ_VERTEX_ATTRIB_STATE, sizeof(uint32_t));
}
static uint32_t* vertex_stream_words(u64 addr) {
    return rec_get(addr, NVN_OBJ_VERTEX_STREAM_STATE, 2 * sizeof(uint32_t));
}
static void h_vertex_attrib_state_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    vertex_attrib_words(c->x[0])[0] = 0;
    pm_unlock(&objs_lock);
}
static void h_vertex_attrib_state_set_format(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = vertex_attrib_words(c->x[0]);
    w[0] = nvn_vertex_attrib_state_set_format(w[0], (u32)c->x[1], (u32)c->x[2]);
    pm_unlock(&objs_lock);
}
static void h_vertex_attrib_state_set_stream_index(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = vertex_attrib_words(c->x[0]);
    w[0] = nvn_vertex_attrib_state_set_stream_index(w[0], (u32)c->x[1]);
    pm_unlock(&objs_lock);
}
static void h_vertex_stream_state_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = vertex_stream_words(c->x[0]);
    w[0] = 0;
    w[1] = 0;
    pm_unlock(&objs_lock);
}
static void h_vertex_stream_state_set_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    vertex_stream_words(c->x[0])[0] = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_vertex_stream_state_set_divisor(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    vertex_stream_words(c->x[0])[1] = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_vertex_stream_state_get_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = vertex_stream_words(c->x[0]);
    c->x[0] = w[0];
    pm_unlock(&objs_lock);
}
static void h_vertex_stream_state_get_divisor(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = vertex_stream_words(c->x[0]);
    c->x[0] = w[1];
    pm_unlock(&objs_lock);
}
/* The library stores the format as 32 bits and the relative offset as 64 bits (a ptrdiff_t). */
static void h_vertex_attrib_state_get_format(CPU* c, u64 entry) {
    (void)entry;
    if (!native_guest_writable(c->x[1], sizeof(uint32_t)) || !native_guest_writable(c->x[2], sizeof(uint64_t))) {
        LOG_LIMIT(8, "nvn-native: nvnVertexAttribStateGetFormat unwritable output\n");
        return;
    }
    pm_lock(&objs_lock);
    uint32_t format;
    uint64_t offset;
    nvn_vertex_attrib_state_get_format(vertex_attrib_words(c->x[0])[0], &format, &offset);
    pm_unlock(&objs_lock);
    gw32(c->x[1], format);
    gw64(c->x[2], offset);
}
static void h_vertex_attrib_state_get_stream_index(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = nvn_vertex_attrib_state_get_stream_index(vertex_attrib_words(c->x[0])[0]);
    pm_unlock(&objs_lock);
}

/* G4: draw, dispatch, program bind and the rest of the raster setters. A draw and a dispatch are recorded, not
   executed: the pipeline stage reads the table later. */
static void command_draw(CommandBufferRec* cb, u64 addr, const char* operation, const NvnOpDraw* op) {
    command_record(cb, addr, operation, NVN_OP_DRAW, op, sizeof *op);
}
static void h_command_buffer_draw_arrays(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDrawArrays";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDraw op = {.kind = NVN_DRAW_ARRAYS, .mode = (u32)c->x[1], .first = (u32)c->x[2],
                    .count = (u32)c->x[3], .instances = 1};
    if (cb) command_draw(cb, c->x[0], name, &op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_draw_arrays_instanced(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDrawArraysInstanced";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDraw op = {.kind = NVN_DRAW_ARRAYS_INSTANCED, .mode = (u32)c->x[1], .first = (u32)c->x[2],
                    .count = (u32)c->x[3], .base_instance = (u32)c->x[4], .instances = (u32)c->x[5]};
    if (cb) command_draw(cb, c->x[0], name, &op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_draw_elements(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDrawElements";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDraw op = {.kind = NVN_DRAW_ELEMENTS, .mode = (u32)c->x[1], .type = (u32)c->x[2],
                    .count = (u32)c->x[3], .address = c->x[4], .instances = 1};
    if (cb) command_draw(cb, c->x[0], name, &op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_draw_elements_base_vertex(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDrawElementsBaseVertex";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDraw op = {.kind = NVN_DRAW_ELEMENTS_BASE_VERTEX, .mode = (u32)c->x[1], .type = (u32)c->x[2],
                    .count = (u32)c->x[3], .address = c->x[4], .base_vertex = (u32)c->x[5], .instances = 1};
    if (cb) command_draw(cb, c->x[0], name, &op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_draw_elements_instanced(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDrawElementsInstanced";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDraw op = {.kind = NVN_DRAW_ELEMENTS_INSTANCED, .mode = (u32)c->x[1], .type = (u32)c->x[2],
                    .count = (u32)c->x[3], .address = c->x[4], .base_vertex = (u32)c->x[5],
                    .base_instance = (u32)c->x[6], .instances = (u32)c->x[7]};
    if (cb) command_draw(cb, c->x[0], name, &op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_dispatch_compute(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferDispatchCompute";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpDispatch op = {(u32)c->x[1], (u32)c->x[2], (u32)c->x[3], 0};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_DISPATCH, &op, sizeof op);
    pm_unlock(&objs_lock);
}
/* The library does nothing for a null program or an empty stage mask. */
static void h_command_buffer_bind_program(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindProgram";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpBindProgram op = {.program = c->x[1], .stage_mask = (u32)c->x[2]};
    if (cb && op.program && op.stage_mask) {
        if (op.stage_mask > NVN_PROGRAM_STAGE_MASK_ALL)
            command_dropped(cb, name, "unknown stage bits", op.stage_mask);
        else {
            const ProgramRec* program = known_record(op.program, NVN_OBJ_PROGRAM);
            if (!program)
                command_dropped(cb, name, "unknown program", op.program);
            else {
                op.program_mask = program->stage_mask;
                if (program->stage_mask & (1u << NVN_PROGRAM_STAGE_COMPUTE)) {
                    op.word_count = NVN_PROGRAM_COMPUTE_WORDS;
                    memcpy(op.words, program->words, NVN_PROGRAM_COMPUTE_WORDS * sizeof op.words[0]);
                } else {
                    op.word_count = program->word_count;
                    memcpy(op.words, program->words, sizeof op.words);
                }
                command_record(cb, c->x[0], name, NVN_OP_BIND_PROGRAM, &op, sizeof op);
            }
        }
    }
    pm_unlock(&objs_lock);
}
/* The words are raw: an enable word as given and a float as its 32-bit bits. The replay maps them to methods. */
static void command_raster(CommandBufferRec* cb, u64 addr, const char* operation, NvnRasterKind which,
                           uint32_t w0, uint32_t w1, uint32_t w2) {
    NvnOpRaster op = {(uint32_t)which, {w0, w1, w2}};
    command_record(cb, addr, operation, NVN_OP_SET_RASTER, &op, sizeof op);
}
#define COMMAND_RASTER_HANDLER(name, operation, which, w0, w1, w2)                    \
    static void h_command_buffer_##name(CPU* c, u64 entry) {                           \
        (void)entry;                                                                   \
        pm_lock(&objs_lock);                                                           \
        CommandBufferRec* cb = command_buffer(c->x[0], operation);                     \
        if (cb) command_raster(cb, c->x[0], operation, which, (w0), (w1), (w2));       \
        pm_unlock(&objs_lock);                                                         \
    }
COMMAND_RASTER_HANDLER(set_depth_bounds, "nvnCommandBufferSetDepthBounds", NVN_RASTER_DEPTH_BOUNDS,
                       (u32)c->x[1], SNX_VS(c->v[0], 0), SNX_VS(c->v[1], 0))
COMMAND_RASTER_HANDLER(set_rasterizer_discard, "nvnCommandBufferSetRasterizerDiscard", NVN_RASTER_DISCARD,
                       (u32)c->x[1], 0, 0)
COMMAND_RASTER_HANDLER(set_polygon_offset_clamp, "nvnCommandBufferSetPolygonOffsetClamp",
                       NVN_RASTER_POLYGON_OFFSET_CLAMP, SNX_VS(c->v[0], 0), SNX_VS(c->v[1], 0),
                       SNX_VS(c->v[2], 0))
COMMAND_RASTER_HANDLER(set_depth_clamp, "nvnCommandBufferSetDepthClamp", NVN_RASTER_DEPTH_CLAMP,
                       (u32)c->x[1], 0, 0)
COMMAND_RASTER_HANDLER(set_line_width, "nvnCommandBufferSetLineWidth", NVN_RASTER_LINE_WIDTH,
                       SNX_VS(c->v[0], 0), 0, 0)
COMMAND_RASTER_HANDLER(set_point_size, "nvnCommandBufferSetPointSize", NVN_RASTER_POINT_SIZE,
                       SNX_VS(c->v[0], 0), 0, 0)
#undef COMMAND_RASTER_HANDLER

/* G5: the vertex state binds. Above 16 the host cannot store the bind, and a count below 1 records 16 disabled entries. */
#define NVN_VERTEX_ATTRIB_BYTES 4u
#define NVN_VERTEX_STREAM_BYTES 8u
static int command_bind_vertex_count(CommandBufferRec* cb, const char* operation, u64 states, uint32_t* count,
                                     uint32_t limit) {
    uint32_t n = *count;
    if ((int32_t)n > (int32_t)limit) {
        command_dropped(cb, operation, "more than 16 states", n);
        return 0;
    }
    if ((int32_t)n < 1) n = 0;
    if (n && states == 0) {
        command_dropped(cb, operation, "null states", states);
        return 0;
    }
    *count = n;
    return 1;
}
static void command_bind_vertex_attrib(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t count,
                                       u64 states) {
    if (!command_bind_vertex_count(cb, operation, states, &count, NVN_VERTEX_ATTRIB_MAX)) return;
    NvnOpBindVertexAttrib op = {.count = count};
    for (uint32_t i = 0; i < count; i++)
        op.w[i] = vertex_attrib_words(states + (u64)i * NVN_VERTEX_ATTRIB_BYTES)[0];
    command_record(cb, addr, operation, NVN_OP_BIND_VERTEX_ATTRIB, &op, sizeof op);
}
static void command_bind_vertex_stream(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t count,
                                       u64 states) {
    if (!command_bind_vertex_count(cb, operation, states, &count, NVN_VERTEX_STREAM_MAX)) return;
    NvnOpBindVertexStream op = {.count = count};
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t* w = vertex_stream_words(states + (u64)i * NVN_VERTEX_STREAM_BYTES);
        op.stride[i] = w[0];
        op.divisor[i] = w[1];
    }
    command_record(cb, addr, operation, NVN_OP_BIND_VERTEX_STREAM, &op, sizeof op);
}
static void h_command_buffer_bind_vertex_attrib_state(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindVertexAttribState";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_vertex_attrib(cb, c->x[0], name, (u32)c->x[1], c->x[2]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_vertex_stream_state(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindVertexStreamState";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_vertex_stream(cb, c->x[0], name, (u32)c->x[1], c->x[2]);
    pm_unlock(&objs_lock);
}
/* The library reads the depth mode of the device at record time. One op for each range. */
static void command_depth_range(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t index,
                                uint32_t depth_mode, uint32_t near_bits, uint32_t far_bits) {
    NvnOpDepthRange op = {index, depth_mode, near_bits, far_bits};
    command_record(cb, addr, operation, NVN_OP_SET_DEPTH_RANGE, &op, sizeof op);
}
static void h_command_buffer_set_depth_ranges(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetDepthRanges";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    u32 first = (u32)c->x[1], count = (u32)c->x[2];
    if (cb && (int32_t)count > 0) {
        if (first >= NVN_VIEWPORTS_MAX || count > NVN_VIEWPORTS_MAX - first)
            command_dropped(cb, name, "range outside the 16 depth ranges", ((u64)first << 32) | count);
        else if (!native_guest_readable(c->x[3], (u64)count * 8))
            command_dropped(cb, name, "unreadable array", c->x[3]);
        else {
            u32 depth_mode = device_rec(cb->device)->depth_mode;
            for (u32 i = 0; i < count; i++)
                command_depth_range(cb, c->x[0], name, first + i, depth_mode, gr32(c->x[3] + (u64)i * 8),
                                    gr32(c->x[3] + (u64)i * 8 + 4));
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_depth_range(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetDepthRange";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        u32 depth_mode = device_rec(cb->device)->depth_mode;
        command_depth_range(cb, c->x[0], name, 0, depth_mode, SNX_VS(c->v[0], 0), SNX_VS(c->v[1], 0));
    }
    pm_unlock(&objs_lock);
}

static void h_command_buffer_clear_color(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferClearColor";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        NvnOpClearColor op = {(u32)c->x[1], (u32)c->x[3], {0}};
        if (!native_guest_readable(c->x[2], sizeof op.color))
            command_dropped(cb, name, "unreadable color", c->x[2]);
        else {
            for (u32 k = 0; k < 4; k++) op.color[k] = gr32(c->x[2] + k * 4);
            command_record(cb, c->x[0], name, NVN_OP_CLEAR_COLOR, &op, sizeof op);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_barrier(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferBarrier");
    NvnOpBarrier op = {(u32)c->x[1], 0};
    if (cb) command_record(cb, c->x[0], "nvnCommandBufferBarrier", NVN_OP_BARRIER, &op, sizeof op);
    pm_unlock(&objs_lock);
}
/* The three stencil setters store the raw face and value; the replay masks them. */
#define COMMAND_STENCIL_HANDLER(name, operation, kind)                                              \
    static void h_command_buffer_##name(CPU* c, u64 entry) {                                        \
        (void)entry;                                                                                \
        pm_lock(&objs_lock);                                                                        \
        CommandBufferRec* cb = command_buffer(c->x[0], operation);                                  \
        NvnOpStencil op = {(u32)(kind), (u32)c->x[1], (u32)c->x[2], 0};                             \
        if (cb) command_record(cb, c->x[0], operation, NVN_OP_SET_STENCIL, &op, sizeof op);         \
        pm_unlock(&objs_lock);                                                                      \
    }
COMMAND_STENCIL_HANDLER(set_stencil_ref, "nvnCommandBufferSetStencilRef", NVN_STENCIL_REF)
COMMAND_STENCIL_HANDLER(set_stencil_value_mask, "nvnCommandBufferSetStencilValueMask", NVN_STENCIL_VALUE_MASK)
COMMAND_STENCIL_HANDLER(set_stencil_mask, "nvnCommandBufferSetStencilMask", NVN_STENCIL_MASK)
static void h_command_buffer_set_blend_color(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetBlendColor";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        NvnOpBlendColor op;
        if (!native_guest_readable(c->x[1], sizeof op.color))
            command_dropped(cb, name, "unreadable color", c->x[1]);
        else {
            for (u32 k = 0; k < 4; k++) op.color[k] = gr32(c->x[1] + k * 4);
            command_record(cb, c->x[0], name, NVN_OP_SET_BLEND_COLOR, &op, sizeof op);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_alpha_ref(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetAlphaRef";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpAlphaRef op = {SNX_VS(c->v[0], 0), 0};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_SET_ALPHA_REF, &op, sizeof op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_call_commands(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferCallCommands";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    u32 count = (u32)c->x[1];
    if (cb && (int32_t)count > 0) {
        if (count > COMMAND_CALL_COUNT_MAX)
            command_dropped(cb, name, "too many handles", count);
        else if (!native_guest_readable(c->x[2], (u64)count * 8))
            command_dropped(cb, name, "unreadable handle array", c->x[2]);
        else
            for (u32 i = 0; i < count; i++) {
                NvnOpCall op = {gr64(c->x[2] + (u64)i * 8)};
                if (!cb->recording)
                    command_record(cb, c->x[0], name, NVN_OP_CALL, &op, sizeof op);
                else if (!nvn_list_ref(&lists, op.handle))
                    command_dropped(cb, name, "handle is not a live list", op.handle);
                else
                    command_record(cb, c->x[0], name, NVN_OP_CALL, &op, sizeof op);
            }
    }
    pm_unlock(&objs_lock);
}
/* ClearDepthStencil(cb, depth, depth_enable, stencil, stencil_mask): the raw depth bits and the three words. */
static void h_command_buffer_clear_depth_stencil(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferClearDepthStencil";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpClearDepthStencil op = {SNX_VS(c->v[0], 0), (u32)c->x[1], (u32)c->x[2], (u32)c->x[3]};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_CLEAR_DEPTH_STENCIL, &op, sizeof op);
    pm_unlock(&objs_lock);
}
/* The three ClearTexture calls share these steps; kind says how the color words are read. */
static void command_clear_texture(CPU* c, const char* operation, uint32_t kind) {
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], operation);
    NvnOpClearTexture op = {.kind = kind, .mask = (uint32_t)c->x[5]};
    if (!cb) {
    } else if (!c->x[1]) {
        command_dropped(cb, operation, "null texture", c->x[1]);
    } else if (!command_target(c->x[1], c->x[2], &op.target, cb, operation)) {
    } else if (!native_guest_readable(c->x[3], sizeof op.region)) {
        command_dropped(cb, operation, "unreadable region", c->x[3]);
    } else if (!native_guest_readable(c->x[4], sizeof op.color)) {
        command_dropped(cb, operation, "unreadable color", c->x[4]);
    } else {
        for (u32 i = 0; i < 6; i++) op.region[i] = gr32(c->x[3] + (u64)i * 4);
        for (u32 i = 0; i < 4; i++) op.color[i] = gr32(c->x[4] + (u64)i * 4);
        command_record(cb, c->x[0], operation, NVN_OP_CLEAR_TEXTURE, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_clear_texture(CPU* c, u64 entry) {
    (void)entry;
    command_clear_texture(c, "nvnCommandBufferClearTexture", NVN_CLEAR_KIND_FLOAT);
}
static void h_command_buffer_clear_texturei(CPU* c, u64 entry) {
    (void)entry;
    command_clear_texture(c, "nvnCommandBufferClearTexturei", NVN_CLEAR_KIND_INT);
}
static void h_command_buffer_clear_textureui(CPU* c, u64 entry) {
    (void)entry;
    command_clear_texture(c, "nvnCommandBufferClearTextureui", NVN_CLEAR_KIND_UINT);
}
static void h_command_buffer_copy_texture_to_texture(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferCopyTextureToTexture";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpCopyTexture op = {0};
    if (!cb) {
    } else if (!c->x[1]) {
        command_dropped(cb, name, "null texture", c->x[1]);
    } else if (!c->x[4]) {
        command_dropped(cb, name, "null texture", c->x[4]);
    } else if (!command_target(c->x[1], c->x[2], &op.src, cb, name)) {
    } else if (!command_target(c->x[4], c->x[5], &op.dst, cb, name)) {
    } else if (!native_guest_readable(c->x[3], sizeof op.src_region)) {
        command_dropped(cb, name, "unreadable region", c->x[3]);
    } else if (!native_guest_readable(c->x[6], sizeof op.dst_region)) {
        command_dropped(cb, name, "unreadable region", c->x[6]);
    } else {
        op.flags = (uint32_t)c->x[7];
        for (u32 i = 0; i < 6; i++) {
            op.src_region[i] = gr32(c->x[3] + (u64)i * 4);
            op.dst_region[i] = gr32(c->x[6] + (u64)i * 4);
        }
        command_record(cb, c->x[0], name, NVN_OP_COPY_TEXTURE, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_report_counter(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferReportCounter";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpReportCounter op = {(uint32_t)c->x[1], 0, c->x[2]};
    if (cb) {
        if (op.type > NVN_REPORT_TYPE_MAX)
            command_dropped(cb, name, "report type", op.type);
        else
            command_record(cb, c->x[0], name, NVN_OP_REPORT_COUNTER, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
/* Begin and End record the control address. The words are built at replay (nvn_xfb_words). */
static void command_xfb(CPU* c, const char* name, NvnXfbKind kind) {
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpTransformFeedback op = {(uint32_t)kind, 0, c->x[1]};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_TRANSFORM_FEEDBACK, &op, sizeof op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_begin_transform_feedback(CPU* c, u64 entry) {
    (void)entry;
    command_xfb(c, "nvnCommandBufferBeginTransformFeedback", NVN_XFB_BEGIN);
}
static void h_command_buffer_end_transform_feedback(CPU* c, u64 entry) {
    (void)entry;
    command_xfb(c, "nvnCommandBufferEndTransformFeedback", NVN_XFB_END);
}
static void h_command_buffer_reset_counter(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferResetCounter";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpResetCounter op = {(uint32_t)c->x[1], 0};
    uint32_t word;
    if (cb) {
        if (!nvn_reset_counter_word(op.type, &word))
            command_dropped(cb, name, "counter type", op.type);
        else
            command_record(cb, c->x[0], name, NVN_OP_RESET_COUNTER, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
/* One BindImage or BindImages call. A NULL host_handles reads the guest array at guest_handles. */
static void command_bind_image(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t stage, uint32_t first,
                               int32_t count, u64 guest_handles, const uint64_t* host_handles) {
    if (count < 1) return;
    uint32_t n = (uint32_t)count;
    if (stage > NVN_BIND_STAGE_MAX) {
        command_dropped(cb, operation, "stage outside the bind stages", stage);
        return;
    }
    if (n > NVN_IMAGE_BINDS_MAX || first >= NVN_BIND_SLOT_MAX || n > NVN_BIND_SLOT_MAX - first) {
        command_dropped(cb, operation, "range outside the image slots", ((u64)first << 32) | n);
        return;
    }
    if (!host_handles && !native_guest_readable(guest_handles, (u64)n * 8)) {
        command_dropped(cb, operation, "unreadable handles", guest_handles);
        return;
    }
    NvnOpBindImage op = {.stage = stage, .first = first, .count = n};
    for (uint32_t i = 0; i < n; i++)
        op.handle[i] = host_handles ? host_handles[i] : gr64(guest_handles + (u64)i * 8);
    command_record(cb, addr, operation, NVN_OP_BIND_IMAGE, &op, sizeof op);
}
static void h_command_buffer_bind_image(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindImage";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_image(cb, c->x[0], name, (uint32_t)c->x[1], (uint32_t)c->x[2], 1, 0, &c->x[3]);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_images(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindImages";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb)
        command_bind_image(cb, c->x[0], name, (uint32_t)c->x[1], (uint32_t)c->x[2], (int32_t)c->x[3], c->x[4], NULL);
    pm_unlock(&objs_lock);
}

/* G7: the callbacks of the replay (decision 6 of the 3c design). They run on the GPU thread in the frame job and read the
   records of the host under objs_lock. A list pointer stays valid because the frame job holds a reference of the list. */
#define REPLAY_LOG_LIMIT 16u
#define REPLAY_METHOD_LAST 1 /* each write is a batch of one: a macro call runs at once */
/* The device word at +0x1A00 is 1, set at device init. nvnCommandBufferInitialize copies it to cb+0x70 bit 4, and
   no writer clears it. So every texture bind sends the descriptor flag word. */
#define NVN_TEXTURE_DESCRIPTOR_FLAG 1
static void replay_host_method(void* ctx, uint32_t m, uint32_t v) {
    maxwell3d_method(((NvnNativeReplayHost*)ctx)->channel, m, v, REPLAY_METHOD_LAST);
}
static uint32_t replay_host_read_register(void* ctx, uint32_t m) {
    const GpuChannel* ch = ((NvnNativeReplayHost*)ctx)->channel;
    return m < GPU_ENGINE_METHODS ? ch->r3d[m] : 0;
}
static const NvnOpList* replay_host_find_list(void* ctx, uint64_t handle) {
    (void)ctx;
    pm_lock(&objs_lock);
    const NvnOpList* list = nvn_list_find(&lists, handle);
    pm_unlock(&objs_lock);
    return list;
}
/* The replay thread owns the bind table; no lock is needed. A bind is state of the command buffer in the game, so the
   table is not cleared between the command lists of one frame job. */
static int replay_host_bind_texture(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle) {
    NvnNativeReplayHost* host = ctx;
    if (stage > NVN_BIND_STAGE_MAX || slot >= NVN_BIND_SLOT_MAX) return 0;
    host->texture_handle[stage][slot] = handle;
    host->texture_bound[stage][slot] = 1;
    host->texture_dirty[stage][slot] = NVN_BIND_DIRTY;
    return 1;
}
static void replay_bind_refuse(NvnNativeReplayHost* host, const char* group, int32_t stage, int32_t slot, uint32_t word);
static int replay_bind_send(NvnNativeReplayHost* host, const uint32_t* words, uint32_t count);
/* The library writes a transform feedback bind at once, before the Begin that may read it, so it is not deferred to
   the draw as the other binds are. */
static int replay_host_send_xfb_bind(NvnNativeReplayHost* host, const NvnOpBindBuffer* op) {
    uint32_t words[NVN_BIND_XFB_WORDS_MAX];
    uint32_t count = nvn_bind_xfb_words(op->slot, op->addr, (uint32_t)op->size, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "transform feedback", -1, (int32_t)op->slot, (uint32_t)op->addr);
        return 0;
    }
    return 1;
}
/* The replay stores addr and size even when the bind is not active, so that a bind that disables the slot replaces the
   old one. The host refuses a kind, stage or slot that its bind table cannot hold. */
static int replay_host_bind_buffer(void* ctx, const NvnOpBindBuffer* op) {
    NvnNativeReplayHost* host = ctx;
    NvnBindEntry* entry;
    uint8_t* dirty = NULL;
    switch ((NvnBindKind)op->kind) {
    case NVN_BIND_UNIFORM:
        if (op->stage > NVN_BIND_STAGE_MAX || op->slot >= NVN_UNIFORM_SLOT_MAX) return 0;
        entry = &host->uniform[op->stage][op->slot];
        dirty = &host->uniform_dirty[op->stage][op->slot];
        break;
    case NVN_BIND_STORAGE:
        if (op->stage > NVN_BIND_STAGE_MAX || op->slot >= NVN_STORAGE_SLOT_MAX) return 0;
        entry = &host->storage[op->stage][op->slot];
        dirty = &host->storage_dirty[op->stage][op->slot];
        break;
    case NVN_BIND_VERTEX:
        if (op->slot >= NVN_VERTEX_SLOT_MAX) return 0;
        entry = &host->vertex[op->slot];
        dirty = &host->vertex_dirty[op->slot];
        break;
    case NVN_BIND_TRANSFORM_FEEDBACK:
        if (op->slot >= NVN_XFB_SLOT_MAX) return 0;
        entry = &host->xfb[op->slot];
        break;
    default:
        return 0;
    }
    entry->addr = op->addr;
    entry->size = op->size;
    entry->bound = (uint8_t)nvn_bind_buffer_active((NvnBindKind)op->kind, op->addr, op->size);
    if (dirty) *dirty = NVN_BIND_DIRTY;
    if (op->kind == NVN_BIND_TRANSFORM_FEEDBACK) return replay_host_send_xfb_bind(host, op);
    return 1;
}
/* The replay stores the words of a state bind. The host refuses a kind or word count that its table cannot hold. */
static int replay_host_bind_state(void* ctx, const NvnOpBindState* op) {
    NvnNativeReplayHost* host = ctx;
    if (op->kind >= (uint32_t)NVN_STATE_COUNT || op->words > NVN_STATE_WORDS_MAX) return 0;
    NvnBoundState* state = &host->state[op->kind];
    memset(state->w, 0, sizeof state->w);
    for (uint32_t i = 0; i < op->words; i++) state->w[i] = op->w[i];
    state->bound = 1;
    host->state_dirty[op->kind] = NVN_BIND_DIRTY;
    if (op->kind == NVN_STATE_MULTISAMPLE)
        LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: multisample state bound, not sent (stage 4): %08x %08x %08x %08x %08x\n",
                  state->w[0], state->w[1], state->w[2], state->w[3], state->w[4]);
    return 1;
}
static const char* const replay_bind_group[NVN_STATE_COUNT] = {
    "color", "depth stencil", "blend", "polygon", "multisample", "channel mask"};
/* One refusal line: the group, stage, slot and first builder input word. The data stays for a later bind. */
static void replay_bind_refuse(NvnNativeReplayHost* host, const char* group, int32_t stage, int32_t slot,
                               uint32_t word) {
    host->bind_refusals++;
    LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: bind %s refused stage=%d slot=%d word=%08x\n", group, stage, slot, word);
}
/* Sends the builder words of one bind group. A damaged stream is a refusal, and the words sent before it stay. */
static int replay_bind_send(NvnNativeReplayHost* host, const uint32_t* words, uint32_t count) {
    NvnWordStream stream = {0};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t method, value;
        int last;
        int result = nvn_word_stream_next(&stream, words[i], &method, &value, &last);
        if (result < 0) return 0;
        if (result == 1) {
            maxwell3d_method(host->channel, method, value, last);
            host->bind_methods++;
        }
    }
    return nvn_word_stream_complete(&stream);
}
/* Multisample has no builder: its bind is only logged, once for the frame job. */
static void replay_bind_send_state(NvnNativeReplayHost* host, uint32_t kind) {
    if (!host->state_dirty[kind]) return;
    host->state_dirty[kind] = 0;
    if (kind == NVN_STATE_MULTISAMPLE) return;
    const uint32_t* w = host->state[kind].w;
    uint32_t out[NVN_BIND_BLEND_WORDS_MAX];
    uint32_t count;
    switch (kind) {
    case NVN_STATE_COLOR: count = nvn_bind_color_words(w[0], out); break;
    case NVN_STATE_CHANNEL_MASK: count = nvn_bind_channel_mask_words(w[0], out); break;
    case NVN_STATE_DEPTH_STENCIL: count = nvn_bind_depth_stencil_words(w[0], w[1], out); break;
    case NVN_STATE_POLYGON: count = nvn_bind_polygon_words(w[0], out); break;
    case NVN_STATE_BLEND: count = nvn_bind_blend_words(w[0], w[1], out); break;
    default: return;
    }
    if (!count || !replay_bind_send(host, out, count))
        replay_bind_refuse(host, replay_bind_group[kind], -1, -1, w[0]);
}
/* Only a format-set attribute reads the game's format table; the others get format word 0, which the builder
   replaces with its default. */
static void replay_bind_send_vertex_attrib(NvnNativeReplayHost* host) {
    if (!host->vertex_attrib_dirty) return;
    host->vertex_attrib_dirty = 0;
    uint8_t enabled[NVN_VERTEX_ATTRIB_MAX];
    uint32_t s[NVN_VERTEX_ATTRIB_MAX], fmt[NVN_VERTEX_ATTRIB_MAX];
    for (uint32_t i = 0; i < NVN_VERTEX_ATTRIB_MAX; i++) {
        enabled[i] = host->vertex_attrib[i].enabled;
        s[i] = host->vertex_attrib[i].w;
        fmt[i] = 0;
    }
    uint32_t count = nvn_highest_enabled_index(enabled, NVN_VERTEX_ATTRIB_MAX);
    for (uint32_t i = 0; i < count; i++) {
        if (!enabled[i]) continue;
        if (!vertex_format_word(nvn_vertex_attrib_format_index(s[i]), &fmt[i])) {
            replay_bind_refuse(host, "vertex attrib", -1, (int32_t)i, s[i]);
            return;
        }
    }
    uint32_t out[NVN_BIND_VERTEX_ATTRIB_WORDS_MAX];
    uint32_t words = nvn_bind_vertex_attrib_words(count, s, fmt, out);
    if (!words || !replay_bind_send(host, out, words))
        replay_bind_refuse(host, "vertex attrib", -1, -1, s[0]);
}
/* The builder zeroes the stride of each stream above the highest enabled one, so the group is always sent whole. */
static void replay_bind_send_vertex_stream(NvnNativeReplayHost* host) {
    if (!host->vertex_stream_dirty) return;
    host->vertex_stream_dirty = 0;
    uint8_t enabled[NVN_VERTEX_STREAM_MAX];
    uint32_t stride[NVN_VERTEX_STREAM_MAX], divisor[NVN_VERTEX_STREAM_MAX];
    for (uint32_t i = 0; i < NVN_VERTEX_STREAM_MAX; i++) {
        enabled[i] = host->vertex_stream[i].enabled;
        stride[i] = host->vertex_stream[i].stride;
        divisor[i] = host->vertex_stream[i].divisor;
    }
    uint32_t count = nvn_highest_enabled_index(enabled, NVN_VERTEX_STREAM_MAX);
    uint32_t out[NVN_BIND_VERTEX_STREAM_WORDS_MAX];
    uint32_t words = nvn_bind_vertex_stream_words(count, stride, divisor, out);
    if (!words || !replay_bind_send(host, out, words))
        replay_bind_refuse(host, "vertex stream", -1, -1, count ? stride[0] : 0);
}
/* Decision 7: only a bound slot is sent. A later size 0 bind disables the slot in the host table but sends nothing. */
static void replay_bind_send_vertex_buffers(NvnNativeReplayHost* host) {
    for (uint32_t i = 0; i < NVN_VERTEX_SLOT_MAX; i++) {
        if (!host->vertex_dirty[i]) continue;
        host->vertex_dirty[i] = 0;
        if (!host->vertex[i].bound) continue;
        uint32_t out[NVN_BIND_VERTEX_BUFFER_WORDS_MAX];
        uint32_t words =
            nvn_bind_vertex_buffer_words(i, host->vertex[i].addr, host->vertex[i].size, out);
        if (!words || !replay_bind_send(host, out, words))
            replay_bind_refuse(host, "vertex buffer", -1, (int32_t)i, (uint32_t)host->vertex[i].addr);
    }
}
/* A size 0 uniform bind is sent too: the builder writes the invalid bind word for it. */
static void replay_bind_send_uniforms(NvnNativeReplayHost* host) {
    for (uint32_t stage = 0; stage < NVN_BIND_STAGE_COMPUTE; stage++) {
        for (uint32_t slot = 0; slot < NVN_UNIFORM_SLOT_MAX; slot++) {
            if (!host->uniform_dirty[stage][slot]) continue;
            host->uniform_dirty[stage][slot] = 0;
            const NvnBindEntry* entry = &host->uniform[stage][slot];
            if (entry->size > UINT32_MAX) {
                replay_bind_refuse(host, "uniform buffer", (int32_t)stage, (int32_t)slot, (uint32_t)entry->addr);
                continue;
            }
            uint32_t out[NVN_BIND_UNIFORM_BUFFER_WORDS_MAX];
            uint32_t words =
                nvn_bind_uniform_buffer_words(stage, slot, entry->addr, (uint32_t)entry->size, out);
            if (!words || !replay_bind_send(host, out, words))
                replay_bind_refuse(host, "uniform buffer", (int32_t)stage, (int32_t)slot, (uint32_t)entry->addr);
        }
    }
}
/* A size 0 storage bind is always sent: the builder writes its parameter word and a zero data block. */
static void replay_bind_send_storages(NvnNativeReplayHost* host) {
    for (uint32_t stage = 0; stage < NVN_BIND_STAGE_COMPUTE; stage++) {
        for (uint32_t slot = 0; slot < NVN_STORAGE_SLOT_MAX; slot++) {
            if (!host->storage_dirty[stage][slot]) continue;
            host->storage_dirty[stage][slot] = 0;
            const NvnBindEntry* entry = &host->storage[stage][slot];
            if (entry->size > UINT32_MAX) {
                replay_bind_refuse(host, "storage buffer", (int32_t)stage, (int32_t)slot, (uint32_t)entry->addr);
                continue;
            }
            uint32_t out[NVN_BIND_STORAGE_BUFFER_WORDS_MAX];
            uint32_t words =
                nvn_bind_storage_buffer_words(stage, slot, entry->addr, (uint32_t)entry->size, out);
            if (!words || !replay_bind_send(host, out, words))
                replay_bind_refuse(host, "storage buffer", (int32_t)stage, (int32_t)slot, (uint32_t)entry->addr);
        }
    }
}
static void replay_bind_send_textures(NvnNativeReplayHost* host) {
    for (uint32_t stage = 0; stage < NVN_BIND_STAGE_COMPUTE; stage++) {
        for (uint32_t slot = 0; slot < NVN_BIND_SLOT_MAX; slot++) {
            if (!host->texture_dirty[stage][slot]) continue;
            host->texture_dirty[stage][slot] = 0;
            uint64_t handle = host->texture_handle[stage][slot];
            uint32_t out[NVN_BIND_TEXTURE_WORDS_MAX];
            uint32_t words = nvn_bind_texture_words(stage, slot, handle, NVN_TEXTURE_DESCRIPTOR_FLAG, out);
            if (!words || !replay_bind_send(host, out, words))
                replay_bind_refuse(host, "texture", (int32_t)stage, (int32_t)slot, (uint32_t)handle);
        }
    }
}
/* Sends each dirty graphics group once. The compute stage stays dirty for the dispatch work. */
static void replay_bind_send_all(NvnNativeReplayHost* host) {
    replay_bind_send_state(host, NVN_STATE_COLOR);
    replay_bind_send_state(host, NVN_STATE_CHANNEL_MASK);
    replay_bind_send_state(host, NVN_STATE_DEPTH_STENCIL);
    replay_bind_send_state(host, NVN_STATE_POLYGON);
    replay_bind_send_state(host, NVN_STATE_BLEND);
    replay_bind_send_vertex_attrib(host);
    replay_bind_send_vertex_stream(host);
    replay_bind_send_vertex_buffers(host);
    replay_bind_send_uniforms(host);
    replay_bind_send_storages(host);
    replay_bind_send_textures(host);
}
/* The 3D register at +0xD18 holds the GPU address of the driver constants buffer that macro 0xE20 reads. */
#define NVN_MME_DRIVER_CONSTANTS_REGISTER 0xD18u

/* The replay thread writes these without a lock. The first draw and the first dispatch of the run get one log line. */
static int replay_host_draw(void* ctx, const NvnOpDraw* op) {
    NvnNativeReplayHost* host = ctx;
    if (op->kind >= (uint32_t)NVN_DRAW_KIND_COUNT) return 0;
    replay_bind_send_all(host);
    uint32_t words[NVN_DRAW_WORDS_MAX];
    int uses_constants = 0;
    uint32_t count = nvn_draw_words(op->kind, op->mode, op->type, op->count, op->address, op->first,
                                    op->base_vertex, op->base_instance, op->instances, words, &uses_constants);
    if (!count) {
        replay_bind_refuse(host, "draw", -1, -1, op->kind);
        return 1;
    }
    if (uses_constants && host->channel->r3d[NVN_MME_DRIVER_CONSTANTS_REGISTER] == 0) {
        replay_bind_refuse(host, "draw constants", -1, -1, op->kind);
        return 1;
    }
    if (!replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "draw words", -1, -1, op->kind);
        return 1;
    }
    host->draws[op->kind]++;
    host->last_draw = *op;
    static int logged; /* only the replay thread runs the frame jobs */
    if (!logged) {
        logged = 1;
        LOG("nvn-native: first draw sent, bind methods=%u refusals=%u r3d[0xD18]=%08x\n",
            host->bind_methods, host->bind_refusals, host->channel->r3d[NVN_MME_DRIVER_CONSTANTS_REGISTER]);
    }
    return 1;
}
/* The dispatch arena: one guest page that holds the QMD and the driver constant block of every dispatch, mapped once
   at the first dispatch and kept for the run. vk_dispatch reads both during the launch call, so one slot is enough. */
#define NVN_DISPATCH_SCRATCH_BYTES NVN_POOL_PAGE_SIZE
#define NVN_DISPATCH_QMD_OFFSET 0u
#define NVN_DISPATCH_CBUF0_OFFSET 0x100u
#define NVN_COMPUTE_INVALIDATE_VALUE 0x1000u /* the library's I(0x87,0x1000) */
#define NVN_COMPUTE_LAUNCH_VALUE 3u
static uint64_t dispatch_scratch_gpu;
static uint8_t dispatch_scratch_ready, dispatch_scratch_failed;

/* Allocates the arena at the first dispatch. It takes objs_lock for the shared GPU cursor. */
static int dispatch_scratch_get(uint64_t* gpu) {
    pm_lock(&objs_lock);
    if (!dispatch_scratch_ready && !dispatch_scratch_failed) {
        uint64_t cpu = kmem_alloc(NVN_DISPATCH_SCRATCH_BYTES, MS_STATIC, PERM_RW);
        uint64_t address = 0, next = 0;
        if (!cpu || !nvn_pool_gpu_allocate(pool_gpu_cursor, NVN_DISPATCH_SCRATCH_BYTES, NVN_POOL_PAGE_SIZE,
                                           &address, &next) ||
            !nvn_gpu_map_range(address, cpu, NVN_DISPATCH_SCRATCH_BYTES)) {
            dispatch_scratch_failed = 1;
            LOG_LIMIT(REPLAY_LOG_LIMIT,
                      "nvn-native: dispatch arena of %u bytes cannot be allocated and mapped\n",
                      (unsigned)NVN_DISPATCH_SCRATCH_BYTES);
        } else {
            pool_gpu_cursor = next;
            dispatch_scratch_gpu = address;
            dispatch_scratch_ready = 1;
        }
    }
    if (gpu) *gpu = dispatch_scratch_gpu;
    int ready = dispatch_scratch_ready;
    pm_unlock(&objs_lock);
    return ready;
}

/* One dispatch refusal line, in the style of replay_bind_refuse. The frame line shows the count next to the sent one. */
static void replay_dispatch_refuse(NvnNativeReplayHost* host, const char* what, uint32_t word) {
    host->dispatch_refusals++;
    LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: dispatch %s refused word=%08x\n", what, word);
}

/* Logs a QMD refusal when its reason or its values differ from the last one. The replay thread is the only caller. */
static void replay_dispatch_qmd_refused(const NvnOpDispatch* op, const NvnProgramComputeIn* prog, const NvnComputeBinds* binds,
                                        uint32_t reason, uint32_t slot) {
    uint32_t bound_mask = 0;
    for (uint32_t i = 0; i < NVN_COMPUTE_UNIFORM_SLOTS; i++)
        if (binds->uniform[i].bound) bound_mask |= 1u << i;
    const NvnComputeBindEntry* entry = &binds->uniform[slot];
    uint64_t key[] = {reason, op->x, op->y, op->z, slot, entry->size, bound_mask, prog->h6fc};
    static uint64_t last[sizeof key / sizeof key[0]];
    static int have_last;
    if (have_last && memcmp(last, key, sizeof key) == 0) return;
    memcpy(last, key, sizeof key);
    have_last = 1;
    LOG("nvn-native: dispatch qmd refused (%s) x=%u y=%u z=%u slot=%u size=%llx uniform_bound=%08x h6fc=%x\n",
        nvn_compute_qmd_reason_name(reason), op->x, op->y, op->z, slot, (unsigned long long)entry->size, bound_mask,
        prog->h6fc);
}

/* Copies the stage 5 binds of the host into the pure builder inputs. The caller gives the scratch cbuf0 address. */
static void replay_dispatch_binds(const NvnNativeReplayHost* host, NvnComputeBinds* binds, uint64_t cbuf0_address) {
    memset(binds, 0, sizeof *binds);
    binds->cbuf0_address = cbuf0_address;
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_SLOTS; slot++) {
        const NvnBindEntry* entry = &host->uniform[NVN_BIND_STAGE_COMPUTE][slot];
        if (!entry->bound) continue;
        binds->uniform[slot] = (NvnComputeBindEntry){entry->addr, entry->size, 1};
    }
    for (uint32_t slot = 0; slot < NVN_COMPUTE_STORAGE_SLOTS; slot++) {
        const NvnBindEntry* entry = &host->storage[NVN_BIND_STAGE_COMPUTE][slot];
        if (!entry->bound) continue;
        binds->storage[slot] = (NvnComputeBindEntry){entry->addr, entry->size, 1};
    }
}

/* Clears the stage 5 binds that the dispatch sent. */
static void replay_dispatch_clear_dirty(NvnNativeReplayHost* host) {
    for (uint32_t slot = 0; slot < NVN_UNIFORM_SLOT_MAX; slot++)
        host->uniform_dirty[NVN_BIND_STAGE_COMPUTE][slot] = 0;
    for (uint32_t slot = 0; slot < NVN_STORAGE_SLOT_MAX; slot++)
        host->storage_dirty[NVN_BIND_STAGE_COMPUTE][slot] = 0;
    for (uint32_t slot = 0; slot < NVN_BIND_SLOT_MAX; slot++)
        host->texture_dirty[NVN_BIND_STAGE_COMPUTE][slot] = 0;
}

static int replay_host_dispatch(void* ctx, const NvnOpDispatch* op) {
    NvnNativeReplayHost* host = ctx;
    if (!host->program[NVN_PROGRAM_STAGE_COMPUTE] || !host->compute_valid) {
        replay_dispatch_refuse(host, "compute program", 0);
        return 1;
    }
    /* v1 has no texture handle block (constant buffer 2), so a bound compute texture stops the dispatch. */
    for (uint32_t slot = 0; slot < NVN_BIND_SLOT_MAX; slot++)
        if (host->texture_bound[NVN_BIND_STAGE_COMPUTE][slot]) {
            replay_dispatch_refuse(host, "compute texture", (uint32_t)host->texture_handle[NVN_BIND_STAGE_COMPUTE][slot]);
            return 1;
        }
    NvnProgramComputeIn prog;
    if (!nvn_program_compute_from_words(host->compute_words, NVN_PROGRAM_COMPUTE_WORDS, &prog)) {
        replay_dispatch_refuse(host, "compute words", 0);
        return 1;
    }
    uint64_t scratch_gpu = 0;
    if (!dispatch_scratch_get(&scratch_gpu)) {
        replay_dispatch_refuse(host, "arena", 0);
        return 1;
    }
    NvnComputeBinds binds;
    replay_dispatch_binds(host, &binds, scratch_gpu + NVN_DISPATCH_CBUF0_OFFSET);
    uint32_t qmd[NVN_COMPUTE_QMD_WORDS];
    uint32_t qmd_slot = 0;
    uint32_t qmd_reason = nvn_compute_qmd_check(&prog, op->x, op->y, op->z, &binds, &qmd_slot);
    if (qmd_reason != NVN_QMD_OK || !nvn_compute_qmd_build(&prog, op->x, op->y, op->z, &binds, qmd)) {
        host->dispatch_refusals++;
        replay_dispatch_qmd_refused(op, &prog, &binds, qmd_reason, qmd_slot);
        return 1;
    }
    uint32_t cbuf0[NVN_COMPUTE_CBUF0_WORDS];
    if (!nvn_compute_cbuf0_build(binds.uniform, binds.storage, cbuf0)) {
        replay_dispatch_refuse(host, "constant block", 0);
        return 1;
    }
    uint64_t qmd_address = scratch_gpu + NVN_DISPATCH_QMD_OFFSET;
    if (gmem_write(qmd_address, qmd, sizeof qmd) < 0 ||
        gmem_write(binds.cbuf0_address, cbuf0, sizeof cbuf0) < 0) {
        replay_dispatch_refuse(host, "scratch write", (uint32_t)qmd_address);
        return 1;
    }
    compute_method(host->channel, KEPLER_COMPUTE_CODE_LOC_HIGH, (uint32_t)(NVN_SHADER_REGION_BASE >> 32),
                   REPLAY_METHOD_LAST);
    compute_method(host->channel, KEPLER_COMPUTE_CODE_LOC_LOW, (uint32_t)NVN_SHADER_REGION_BASE, REPLAY_METHOD_LAST);
    /* The library sends I(0x87,0x1000) at each dispatch in v1; bit 0 invalidates the instruction cache. */
    compute_method(host->channel, KEPLER_COMPUTE_INVALIDATE_SHADER_CACHES, NVN_COMPUTE_INVALIDATE_VALUE,
                   REPLAY_METHOD_LAST);
    compute_method(host->channel, KEPLER_COMPUTE_LAUNCH_DESC_LOC,
                   (uint32_t)(qmd_address >> KEPLER_COMPUTE_LAUNCH_DESC_LOC_SHIFT), REPLAY_METHOD_LAST);
    compute_method(host->channel, KEPLER_COMPUTE_LAUNCH, NVN_COMPUTE_LAUNCH_VALUE, REPLAY_METHOD_LAST);
    host->dispatches++;
    host->last_dispatch[0] = op->x;
    host->last_dispatch[1] = op->y;
    host->last_dispatch[2] = op->z;
    replay_dispatch_clear_dirty(host);
    static int logged; /* only the replay thread runs the frame jobs */
    if (!logged) {
        logged = 1;
        LOG("nvn-native: first compute dispatch sent x=%u y=%u z=%u qmd=%llx program=%x\n", op->x, op->y, op->z,
            (unsigned long long)qmd_address, prog.code_offset);
    }
    return 1;
}
/* Sends the method words of a graphics program with last marked on the last word of each batch. A damaged stream (a
   refusal or a run left open) gives 0. */
static int replay_host_program_words(NvnNativeReplayHost* host, const NvnOpBindProgram* op) {
    NvnWordStream stream = {0};
    for (uint32_t i = 0; i < op->word_count; i++) {
        uint32_t method, value;
        int last;
        int result = nvn_word_stream_next(&stream, op->words[i], &method, &value, &last);
        if (result < 0) return 0;
        if (result == 1) maxwell3d_method(host->channel, method, value, last);
    }
    return nvn_word_stream_complete(&stream);
}
/* The shader pools of the frame job lie in one region: the library writes its base once, before the first program. */
static void replay_host_region_base(NvnNativeReplayHost* host) {
    if (host->region_base_sent) return;
    host->region_base_sent = 1;
    maxwell3d_method(host->channel, NVN_SHADER_REGION_BASE_HIGH_METHOD,
                     (uint32_t)(NVN_SHADER_REGION_BASE >> 32), REPLAY_METHOD_LAST);
    maxwell3d_method(host->channel, NVN_SHADER_REGION_BASE_LOW_METHOD,
                     (uint32_t)NVN_SHADER_REGION_BASE, REPLAY_METHOD_LAST);
}
static void replay_host_disable_stages(NvnNativeReplayHost* host, const NvnOpBindProgram* op) {
    NvnProgramMethod pair[NVN_PROGRAM_DISABLE_MAX];
    uint32_t count = nvn_program_disable_methods(op->stage_mask, op->program_mask, pair);
    for (uint32_t i = 0; i < count; i++)
        maxwell3d_method(host->channel, pair[i].method, pair[i].value, REPLAY_METHOD_LAST);
}
static int replay_host_bind_program(void* ctx, const NvnOpBindProgram* op) {
    NvnNativeReplayHost* host = ctx;
    if (op->stage_mask > NVN_PROGRAM_STAGE_MASK_ALL) return 0;
    if (op->word_count > NVN_PROGRAM_WORDS_MAX) return 0;
    for (uint32_t stage = 0; stage < NVN_PROGRAM_STAGE_COUNT; stage++)
        if (op->stage_mask & (1u << stage)) host->program[stage] = op->program;
    if (op->stage_mask & (1u << NVN_PROGRAM_STAGE_COMPUTE)) {
        if (op->word_count != NVN_PROGRAM_COMPUTE_WORDS) return 0;
        memcpy(host->compute_words, op->words, sizeof host->compute_words);
        host->compute_valid = 1;
        return 1;
    }
    replay_host_region_base(host);
    if (!replay_host_program_words(host, op)) return 0;
    replay_host_disable_stages(host, op);
    return 1;
}
/* The raw attribute word stays for the pipeline stage; enabled is the format-set bit. Unused entries go back to zero. */
static int replay_host_bind_vertex_attrib(void* ctx, const NvnOpBindVertexAttrib* op) {
    NvnNativeReplayHost* host = ctx;
    if (op->count > NVN_VERTEX_ATTRIB_MAX) return 0;
    for (uint32_t i = 0; i < NVN_VERTEX_ATTRIB_MAX; i++) {
        uint32_t w = i < op->count ? op->w[i] : 0;
        host->vertex_attrib[i].w = w;
        host->vertex_attrib[i].enabled = (uint8_t)((w >> 6) & 1u);
    }
    host->vertex_attrib_dirty = NVN_BIND_DIRTY;
    host->vattrib_binds++;
    return 1;
}
/* The stride is masked as the library masks it; the divisor keeps its raw value. Unused entries go back to zero. */
static int replay_host_bind_vertex_stream(void* ctx, const NvnOpBindVertexStream* op) {
    NvnNativeReplayHost* host = ctx;
    if (op->count > NVN_VERTEX_STREAM_MAX) return 0;
    for (uint32_t i = 0; i < NVN_VERTEX_STREAM_MAX; i++) {
        host->vertex_stream[i].stride = i < op->count ? op->stride[i] & 0xFFFu : 0;
        host->vertex_stream[i].divisor = i < op->count ? op->divisor[i] : 0;
        host->vertex_stream[i].enabled = (uint8_t)(i < op->count);
    }
    host->vertex_stream_dirty = NVN_BIND_DIRTY;
    host->vstream_binds++;
    return 1;
}
static int replay_host_descriptor_pool(void* ctx, NvnOpCode op, const NvnOpPool* pool, uint64_t* gpu_base, uint32_t* count) {
    (void)ctx;
    NvnObjKind kind = op == NVN_OP_SET_TEXTURE_POOL ? NVN_OBJ_TEXTURE_POOL : NVN_OBJ_SAMPLER_POOL;
    int ok = 0;
    pm_lock(&objs_lock);
    const DescriptorPoolRec* descriptors = nvn_objtab_valid(&objs, pool->pool, pool->generation)
                                               ? known_record(pool->pool, kind) : NULL;
    const MemoryPoolRec* memory = descriptors ? known_record(descriptors->memory_pool, NVN_OBJ_MEMORY_POOL) : NULL;
    if (memory && memory->gpu_address && descriptors->offset <= UINT64_MAX - memory->gpu_address) {
        *gpu_base = memory->gpu_address + descriptors->offset;
        *count = descriptors->count;
        ok = 1;
    }
    pm_unlock(&objs_lock);
    return ok;
}
/* Counts of the refusals by cause in one frame. The log lines are limited, so the frame line shows these counts. Only the
   GPU thread (the frame job) calls the replay callbacks. Causes are string literals. */
#define REPLAY_CAUSES_MAX 16u
static struct { const char* what; uint32_t count; } replay_causes[REPLAY_CAUSES_MAX];
static uint32_t replay_cause_overflow;
static void replay_cause_count(const char* what) {
    for (uint32_t i = 0; i < REPLAY_CAUSES_MAX; i++) {
        if (!replay_causes[i].what) replay_causes[i].what = what;
        if (replay_causes[i].what == what || strcmp(replay_causes[i].what, what) == 0) {
            replay_causes[i].count++;
            return;
        }
    }
    replay_cause_overflow++;
}
/* Writes "cause=count" pairs of the frame into out and clears the counts. out is empty when there was no refusal. */
static void replay_causes_take(char* out, size_t size) {
    size_t used = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < REPLAY_CAUSES_MAX && replay_causes[i].what; i++) {
        int n = snprintf(out + used, size - used, "%s%s=%u", used ? "; " : "", replay_causes[i].what, replay_causes[i].count);
        if (n > 0 && (size_t)n < size - used) used += (size_t)n;
        replay_causes[i].what = NULL;
        replay_causes[i].count = 0;
    }
    if (replay_cause_overflow) {
        snprintf(out + used, size - used, "%sother=%u", used ? "; " : "", replay_cause_overflow);
        replay_cause_overflow = 0;
    }
}
static void replay_host_rt_fault(void* ctx, const char* what, uint64_t value) {
    (void)ctx;
    replay_cause_count(what);
    LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: replay render targets refused: %s (%llx)\n", what, (unsigned long long)value);
}
/* The textures are copied under the lock, so that the words are made without it.
   0 when a color or the depth texture is gone. depth is filled when op->depth.texture is not 0. */
static int replay_host_targets(const NvnOpRenderTargets* op, NvnTexture textures[NVN_RENDER_TARGETS_MAX],
                               NvnRtColor color[NVN_RENDER_TARGETS_MAX], NvnTexture* depth_texture,
                               NvnRtColor* depth) {
    int ok = 1;
    *depth = (NvnRtColor){0};
    pm_lock(&objs_lock);
    for (uint32_t i = 0; ok && i < op->count; i++) {
        const NvnOpTarget* target = &op->color[i];
        color[i] = (NvnRtColor){0};
        if (!target->texture) continue;
        const TextureRec* t = nvn_objtab_valid(&objs, target->texture, target->generation)
                                  ? known_record(target->texture, NVN_OBJ_TEXTURE) : NULL;
        if (!t) {
            ok = 0;
            break;
        }
        textures[i] = *t;
        color[i].texture = &textures[i];
        color[i].view = target->has_view ? &target->view : NULL;
    }
    if (ok && op->depth.texture) {
        const NvnOpTarget* target = &op->depth;
        const TextureRec* t = nvn_objtab_valid(&objs, target->texture, target->generation)
                                  ? known_record(target->texture, NVN_OBJ_TEXTURE) : NULL;
        if (!t) {
            ok = 0;
        } else {
            *depth_texture = *t;
            depth->texture = depth_texture;
            depth->view = target->has_view ? &target->view : NULL;
        }
    }
    pm_unlock(&objs_lock);
    return ok;
}
static int replay_host_render_targets(void* ctx, const NvnOpRenderTargets* op) {
    NvnNativeReplayHost* host = ctx;
    NvnTexture textures[NVN_RENDER_TARGETS_MAX];
    NvnRtColor color[NVN_RENDER_TARGETS_MAX];
    NvnTexture depth_texture;
    NvnRtColor depth;
    if (op->count > NVN_RENDER_TARGETS_MAX) {
        replay_host_rt_fault(ctx, "count", op->count);
        return 0;
    }
    if (!host->clip_limit) {
        replay_host_rt_fault(ctx, "clip limit unknown", 0);
        return 0;
    }
    if (!replay_host_targets(op, textures, color, &depth_texture, &depth)) {
        replay_host_rt_fault(ctx, "texture gone", op->count);
        return 0;
    }
    NvnRtEnv env = {ctx, replay_host_method, replay_host_rt_fault};
    return nvn_rt_emit(&env, color, op->count, depth.texture ? &depth : NULL, host->clip_limit);
}
/* The game's device word +0x1A04 is 0, not 0x08C8B771, so the zcull clear word is written. The device word +0x1A08
   is unknown, so the scratch macro 0xE26 stays off until its meaning is known. */
#define NVN_REPLAY_ZCULL_CLEAR 1
#define NVN_REPLAY_MACRO_E26 0

static int replay_host_report_counter(NvnNativeReplayHost* host, const NvnOpReportCounter* op) {
    uint32_t words[NVN_REPORT_WORDS_MAX];
    uint32_t count = nvn_report_counter_words(op->type, op->address, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "report counter", -1, -1, op->type);
        return 1;
    }
    return 1;
}

static int replay_host_bind_image(NvnNativeReplayHost* host, const NvnOpBindImage* op) {
    uint32_t words[NVN_BIND_IMAGE_WORDS_MAX];
    uint32_t count = nvn_bind_image_words(op->stage, op->first, op->count, op->handle,
                                          NVN_TEXTURE_DESCRIPTOR_FLAG, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "image bind", (int32_t)op->stage, (int32_t)op->first, op->count);
        return 1;
    }
    return 1;
}

static int replay_host_clear_depth_stencil(NvnNativeReplayHost* host, const NvnOpClearDepthStencil* op) {
    if (nvn_clear_depth_stencil_nothing(op->depth_enable, op->stencil_mask)) return 1;
    uint32_t words[NVN_CLEAR_DEPTH_STENCIL_WORDS_MAX];
    uint32_t count = nvn_clear_depth_stencil_words(op->depth_bits, op->depth_enable, op->stencil,
                                                   op->stencil_mask, NVN_REPLAY_ZCULL_CLEAR,
                                                   NVN_REPLAY_MACRO_E26, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "clear depth stencil", -1, -1, op->depth_bits);
        return 1;
    }
    return 1;
}

static int replay_host_transform_feedback(NvnNativeReplayHost* host, const NvnOpTransformFeedback* op) {
    uint32_t words[NVN_XFB_WORDS_MAX];
    uint32_t count = nvn_xfb_words(op->kind, op->address, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "transform feedback", -1, -1, op->kind);
        return 1;
    }
    return 1;
}

/* The texture record is copied under the lock, so that the words are made without it. */
static int replay_host_clear_texture(NvnNativeReplayHost* host, const NvnOpClearTexture* op) {
    if (!host->clip_limit) {
        replay_host_rt_fault(host, "clip limit unknown", 0);
        return 1;
    }
    NvnTexture texture = {0};
    int found = 0;
    pm_lock(&objs_lock);
    const TextureRec* t = op->target.texture &&
                          nvn_objtab_valid(&objs, op->target.texture, op->target.generation)
                              ? known_record(op->target.texture, NVN_OBJ_TEXTURE) : NULL;
    if (t) {
        texture = *t;
        found = 1;
    }
    pm_unlock(&objs_lock);
    if (!found) {
        replay_bind_refuse(host, "clear texture", -1, -1, (uint32_t)op->target.texture);
        return 1;
    }
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    int nothing = 0;
    NvnRtEnv env = {host, replay_host_method, replay_host_rt_fault};
    uint32_t count = nvn_rt_clear_texture_words(&env, &texture,
                                                op->target.has_view ? &op->target.view : NULL,
                                                op->region, op->color, op->mask, host->clip_limit,
                                                &nothing, words);
    if (nothing) return 1;
    if (!count || !replay_bind_send(host, words, count))
        replay_bind_refuse(host, "clear texture", -1, -1, (uint32_t)op->target.texture);
    return 1;
}

/* The two texture records are copied under the lock, so that the words are made without it. */
static int replay_host_copy_texture(NvnNativeReplayHost* host, const NvnOpCopyTexture* op) {
    NvnTexture src = {0}, dst = {0};
    int found = 0;
    pm_lock(&objs_lock);
    const TextureRec* s = op->src.texture &&
                          nvn_objtab_valid(&objs, op->src.texture, op->src.generation)
                              ? known_record(op->src.texture, NVN_OBJ_TEXTURE) : NULL;
    const TextureRec* d = op->dst.texture &&
                          nvn_objtab_valid(&objs, op->dst.texture, op->dst.generation)
                              ? known_record(op->dst.texture, NVN_OBJ_TEXTURE) : NULL;
    if (s && d) {
        src = *s;
        dst = *d;
        found = 1;
    }
    pm_unlock(&objs_lock);
    if (!found) {
        replay_bind_refuse(host, "copy texture", -1, -1, (uint32_t)op->src.texture);
        return 1;
    }
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = 0;
    NvnRtCopyEnv env = {{host, replay_host_method, replay_host_rt_fault}, copy_format_word_cb};
    uint32_t count = nvn_rt_copy_texture_words(&env, &src,
                                               op->src.has_view ? &op->src.view : NULL, &dst,
                                               op->dst.has_view ? &op->dst.view : NULL,
                                               op->src_region, op->dst_region, op->flags,
                                               &nothing, words);
    if (nothing) return 1;
    if (!count || !replay_bind_send(host, words, count))
        replay_bind_refuse(host, "copy texture", -1, -1, (uint32_t)op->src.texture);
    return 1;
}

static int replay_host_gpu_op(void* ctx, NvnOpCode code, const void* payload) {
    NvnNativeReplayHost* host = ctx;
    if (code >= NVN_OP_COUNT) return 0;
    host->gpu_ops[code]++;
    if (code == NVN_OP_REPORT_COUNTER) {
        host->last_report = *(const NvnOpReportCounter*)payload;
        return replay_host_report_counter(host, &host->last_report);
    }
    if (code == NVN_OP_BIND_IMAGE) {
        host->last_bind_image = *(const NvnOpBindImage*)payload;
        return replay_host_bind_image(host, &host->last_bind_image);
    }
    if (code == NVN_OP_CLEAR_DEPTH_STENCIL)
        return replay_host_clear_depth_stencil(host, (const NvnOpClearDepthStencil*)payload);
    if (code == NVN_OP_TRANSFORM_FEEDBACK)
        return replay_host_transform_feedback(host, (const NvnOpTransformFeedback*)payload);
    if (code == NVN_OP_CLEAR_TEXTURE)
        return replay_host_clear_texture(host, (const NvnOpClearTexture*)payload);
    if (code == NVN_OP_COPY_TEXTURE)
        return replay_host_copy_texture(host, (const NvnOpCopyTexture*)payload);
    return 1;
}
static void replay_host_fault(void* ctx, const char* what, uint64_t value) {
    (void)ctx;
    replay_cause_count(what);
    LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: replay fault: %s (%llx)\n", what, (unsigned long long)value);
}
void nvn_native_replay_env(NvnNativeReplayHost* host, int window_origin_flip, NvnReplayEnv* env) {
    *env = (NvnReplayEnv){.ctx = host, .method = replay_host_method, .read_register = replay_host_read_register,
                          .find_list = replay_host_find_list, .descriptor_pool = replay_host_descriptor_pool,
                          .bind_texture = replay_host_bind_texture, .bind_buffer = replay_host_bind_buffer,
                          .bind_state = replay_host_bind_state, .draw = replay_host_draw,
                          .dispatch = replay_host_dispatch, .bind_program = replay_host_bind_program,
                          .bind_vertex_attrib = replay_host_bind_vertex_attrib,
                          .bind_vertex_stream = replay_host_bind_vertex_stream,
                          .render_targets = replay_host_render_targets, .gpu_op = replay_host_gpu_op,
                          .fault = replay_host_fault, .window_origin_flip = window_origin_flip};
}

/* G4: window and sync state stays in host records; guest object bytes stay untouched. */
#define WINDOW_BUILDER_HANDLER(name, call) \
    static void h_##name(CPU* c, u64 entry) { \
        (void)entry; pm_lock(&objs_lock); \
        NvnWindowBuilder* b = rec_get(c->x[0], NVN_OBJ_WINDOW_BUILDER, sizeof *b); \
        call; pm_unlock(&objs_lock); \
    }
WINDOW_BUILDER_HANDLER(window_builder_set_defaults, nvn_window_builder_defaults(b))
WINDOW_BUILDER_HANDLER(window_builder_set_device, nvn_window_builder_set_device(b, c->x[1]))
WINDOW_BUILDER_HANDLER(window_builder_set_native_window, nvn_window_builder_set_native_window(b, c->x[1]))
WINDOW_BUILDER_HANDLER(window_builder_set_textures, nvn_window_builder_set_textures(b, (u32)c->x[1], c->x[2]))
WINDOW_BUILDER_HANDLER(window_builder_set_present_interval, nvn_window_builder_set_interval(b, (u32)c->x[1]))
#undef WINDOW_BUILDER_HANDLER

static void h_window_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnWindowBuilder* b = known_record(c->x[1], NVN_OBJ_WINDOW_BUILDER);
    if (!b) {
        LOG("nvn-native: window initialize failed: unknown builder (%llx)\n", (unsigned long long)c->x[1]);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    if (!nvn_window_initialize_check(b->texture_count)) {
        LOG("nvn-native: window initialize failed: invalid texture count (%u)\n", b->texture_count);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    uint64_t bytes = (uint64_t)b->texture_count * sizeof(uint64_t);
    if (!native_guest_readable(b->textures, bytes)) {
        LOG("nvn-native: window initialize failed: unreadable texture array (%llx, %u)\n",
            (unsigned long long)b->textures, b->texture_count);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    WindowRec next = {0};
    next.device = b->device; next.native_window = b->native_window;
    next.texture_count = b->texture_count; next.interval = b->interval; next.last_index = -1;
    for (uint32_t i = 0; i < b->texture_count; i++) {
        uint64_t addr = gr64(b->textures + (uint64_t)i * sizeof(uint64_t));
        TextureRec* texture = known_record(addr, NVN_OBJ_TEXTURE);
        if (!texture) {
            LOG("nvn-native: window initialize failed: unknown texture (%u, %llx)\n", i, (unsigned long long)addr);
            c->x[0] = 0; pm_unlock(&objs_lock); return;
        }
        next.textures[i] = addr;
        next.states[i] = NVN_IMAGE_FREE;
        if (!i) next.crop = nvn_window_crop(0, 0, 0, 0, texture->builder.width, texture->builder.height);
    }
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_WINDOW, sizeof next);
    if (!o) fatal("nvn-native: out of memory for the record of a window\n");
    memcpy(o->data, &next, sizeof next);
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}
static void h_window_set_present_interval(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
    if (!w) LOG("nvn-native: nvnWindowSetPresentInterval unknown window=%llx\n", (unsigned long long)c->x[0]);
    else w->interval = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_window_set_crop(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
    if (!w) { LOG("nvn-native: nvnWindowSetCrop unknown window=%llx\n", (unsigned long long)c->x[0]); pm_unlock(&objs_lock); return; }
    TextureRec* t = known_record(w->textures[0], NVN_OBJ_TEXTURE);
    if (!t) {
        LOG("nvn-native: nvnWindowSetCrop first texture of window %llx is gone\n", (unsigned long long)c->x[0]);
        pm_unlock(&objs_lock);
        return;
    }
    w->crop = nvn_window_crop((u32)c->x[1], (u32)c->x[2], (u32)c->x[3], (u32)c->x[4],
                              t->builder.width, t->builder.height);
    pm_unlock(&objs_lock);
}
static void h_sync_initialize(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    SyncRec* s = rec_get(c->x[0], NVN_OBJ_SYNC, sizeof *s);
    s->device = c->x[1]; s->state = NVN_SYNC_UNSIGNALED; s->fences = 0;
    c->x[0] = 1; pm_unlock(&objs_lock);
}

enum { NVN_ACQUIRE_OK = 0, NVN_ACQUIRE_FAILED = 1 };
/* Ends a failed nvnWindowAcquireTexture: the reason, the invalid index and the result. objs_lock is held. */
static void window_acquire_failed(CPU* c, u64 window_addr, u64 sync_addr, u64 index_addr, const char* reason) {
    LOG("nvn-native: nvnWindowAcquireTexture failed: %s (window=%llx sync=%llx index=%llx)\n",
        reason, (unsigned long long)window_addr, (unsigned long long)sync_addr, (unsigned long long)index_addr);
    if (native_guest_writable(index_addr, sizeof(uint32_t))) gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
    c->x[0] = NVN_ACQUIRE_FAILED;
}
static void h_window_acquire_texture(CPU* c, u64 entry) {
    (void)entry;
    const u64 window_addr = c->x[0], sync_addr = c->x[1], index_addr = c->x[2];
    pm_lock(&objs_lock);
    WindowRec* window = known_record(window_addr, NVN_OBJ_WINDOW);
    SyncRec* sync = known_record(sync_addr, NVN_OBJ_SYNC);
    if (!window || !sync || !native_guest_writable(index_addr, sizeof(uint32_t))) {
        const char* reason = !window ? "unknown window" : !sync ? "unknown sync" : "unwritable index";
        window_acquire_failed(c, window_addr, sync_addr, index_addr, reason);
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t interval = window->interval;
    pm_unlock(&objs_lock);
    present_pace(interval);
    pm_lock(&objs_lock);
    window = known_record(window_addr, NVN_OBJ_WINDOW);
    sync = known_record(sync_addr, NVN_OBJ_SYNC);
    if (!window || !sync || !native_guest_writable(index_addr, sizeof(uint32_t))) {
        const char* reason = !window ? "window changed" : !sync ? "sync changed" : "index changed";
        window_acquire_failed(c, window_addr, sync_addr, index_addr, reason);
        pm_unlock(&objs_lock);
        return;
    }
    int32_t index = nvn_window_pick_image(window->states, window->texture_count, window->last_index);
    /* A PRESENTED image comes back when its frame job finishes. */
    while (index < 0 && nvn_window_has_presented(window->states, window->texture_count)) {
        kthread_wait(&frame_done_cv, &objs_lock, -1);
        window = known_record(window_addr, NVN_OBJ_WINDOW);
        sync = known_record(sync_addr, NVN_OBJ_SYNC);
        if (!window || !sync || !native_guest_writable(index_addr, sizeof(uint32_t))) {
            const char* reason = !window ? "window changed" : !sync ? "sync changed" : "index changed";
            window_acquire_failed(c, window_addr, sync_addr, index_addr, reason);
            pm_unlock(&objs_lock);
            return;
        }
        index = nvn_window_pick_image(window->states, window->texture_count, window->last_index);
    }
    if (index < 0) {
        LOG("nvn-native: nvnWindowAcquireTexture failed: no free window image (window=%llx states=%u,%u,%u,%u)\n",
            (unsigned long long)window_addr, window->states[0], window->states[1], window->states[2], window->states[3]);
        gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
        c->x[0] = NVN_ACQUIRE_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    window->states[index] = NVN_IMAGE_ACQUIRED;
    window->last_index = index;
    /* A free image means its previous frame job has finished. */
    sync->state = NVN_SYNC_SIGNALED;
    gw32(index_addr, (uint32_t)index);
    c->x[0] = NVN_ACQUIRE_OK;
    frame_acquires++;
    pm_unlock(&objs_lock);
}
static void h_sync_wait(CPU* c, u64 entry) {
    (void)entry;
    const u64 sync_addr = c->x[0];
    const NvnWaitDeadline limit = nvn_wait_deadline(c->x[1], plat_ns());
    pm_lock(&objs_lock);
    SyncRec* sync = known_record(sync_addr, NVN_OBJ_SYNC);
    if (!sync) {
        LOG("nvn-native: nvnSyncWait unknown sync=%llx\n", (unsigned long long)sync_addr);
        c->x[0] = NVN_SYNC_WAIT_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    NvnSyncAction action = nvn_sync_wait_action(sync->state);
    if (action == NVN_SYNC_ACTION_REFUSE) {
        LOG_LIMIT(32, "nvn-native: nvnSyncWait sync=%llx state=%u timeout=%llx is not signaled, fails (caller lr=%llx)\n",
                  (unsigned long long)sync_addr, (unsigned)sync->state, (unsigned long long)c->x[1],
                  (unsigned long long)c->x[30]);
        c->x[0] = NVN_SYNC_WAIT_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    NvnSyncWaitResult result = NVN_SYNC_WAIT_SIGNALED;
    while (action == NVN_SYNC_ACTION_WAIT) {
        i64 left = nvn_wait_remaining_ns(limit, plat_ns());
        if (left == 0) {
            result = NVN_SYNC_WAIT_TIMEOUT;
            break;
        }
        int timed_out = kthread_wait(&frame_done_cv, &objs_lock, left);
        sync = known_record(sync_addr, NVN_OBJ_SYNC);
        if (!sync) {
            LOG_LIMIT(32, "nvn-native: nvnSyncWait sync=%llx is gone while it waited, fails\n",
                      (unsigned long long)sync_addr);
            result = NVN_SYNC_WAIT_FAILED;
            break;
        }
        action = nvn_sync_wait_action(sync->state);
        if (action == NVN_SYNC_ACTION_SIGNALED) {
            result = NVN_SYNC_WAIT_SIGNALED;
            break;
        }
        if (action == NVN_SYNC_ACTION_REFUSE) {
            LOG_LIMIT(32, "nvn-native: nvnSyncWait sync=%llx state=%u is not fenced after the wait, fails\n",
                      (unsigned long long)sync_addr, (unsigned)sync->state);
            result = NVN_SYNC_WAIT_FAILED;
            break;
        }
        if (timed_out) {
            result = NVN_SYNC_WAIT_TIMEOUT;
            break;
        }
    }
    c->x[0] = result;
    pm_unlock(&objs_lock);
}

/* The frame that Present closes (decision 8). The GPU thread replays it on the private channel, shows the window
   image, frees it and signals the tokens (decisions 9 to 12). */
typedef struct {
    NvnPendingFrame frame;
    uint64_t queue, window, texture;
    uint32_t window_generation, texture_generation, index;
    int window_origin_flip;
    GpuChannel* channel; /* the private channel of the queue, used on the GPU thread only */
    uint32_t* init_words; /* the queue's macro code, copied so that it outlives the record */
    uint32_t init_word_count;
} FrameJob;
#define FRAME_LOG_LIMIT 4u           /* log lines for the faults of the frame job */
#define NVN_FRAME_ABNORMAL_LIMIT 200u /* abnormal frame lines, so that a broken frame does not flood the log */
/* The device word at +0x18 was 0x4000 in one run; its source is not found.
   The selftest S case of step 4 checks it. */
#define NVN_REPLAY_CLIP_LIMIT 0x4000u

/* What the job needs from the records, copied under objs_lock. A part whose record was gone stays 0. */
typedef struct {
    int window_valid, texture_valid, pool_valid;
    NvnTexture texture;
    uint64_t pool_storage;
    NvnWindowCrop crop;
} FrameParts;

/* Runs on the GPU thread. */
static void frame_begin(void) {
    if (g_renderer->stream_run) g_renderer->stream_run(1);
    if (g_renderer->mem_may_have_changed) g_renderer->mem_may_have_changed();
}
static void frame_read_parts(const FrameJob* job, FrameParts* parts) {
    *parts = (FrameParts){0};
    pm_lock(&objs_lock);
    NvnObj* wo = nvn_objtab_valid(&objs, job->window, job->window_generation) ? nvn_objtab_find(&objs, job->window) : NULL;
    WindowRec* window = wo && wo->kind == NVN_OBJ_WINDOW ? wo->data : NULL;
    if (window) {
        parts->window_valid = 1;
        parts->crop = window->crop;
    }
    NvnObj* to = nvn_objtab_valid(&objs, job->texture, job->texture_generation)
                     ? nvn_objtab_find(&objs, job->texture) : NULL;
    TextureRec* texture = to && to->kind == NVN_OBJ_TEXTURE ? to->data : NULL;
    if (texture) {
        parts->texture = *texture;
        parts->texture_valid = 1;
        MemoryPoolRec* pool = known_record(texture->builder.pool, NVN_OBJ_MEMORY_POOL);
        if (pool) {
            parts->pool_valid = 1;
            parts->pool_storage = pool->storage;
        }
    }
    pm_unlock(&objs_lock);
    if (!parts->window_valid)
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame window=%llx generation=%u is gone\n",
                  (unsigned long long)job->window, job->window_generation);
    if (!parts->texture_valid)
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame texture=%llx generation=%u is gone\n",
                  (unsigned long long)job->texture, job->texture_generation);
    if (parts->texture_valid && !parts->pool_valid)
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame texture=%llx memory pool=%llx is gone\n",
                  (unsigned long long)job->texture, (unsigned long long)parts->texture.builder.pool);
}
static void frame_first_log(const FrameJob* job, const FrameParts* parts) {
    static int logged; /* only the GPU thread runs the frame jobs */
    if (logged) return;
    logged = 1;
    LOG("nvn-native: frame job channel=%u window=%ux%u format=%x\n", job->channel->id,
        parts->texture_valid ? parts->texture.builder.width : 0,
        parts->texture_valid ? parts->texture.builder.height : 0,
        parts->texture_valid ? parts->texture.builder.format : 0);
}
/* Sets the window image as color target 0 through the replay callback, as the library's tail entry does. */
static int frame_set_target(const FrameJob* job, const FrameParts* parts) {
    if (!parts->texture_valid) return 0;
    NvnOpRenderTargets op = {0};
    op.count = 1;
    op.color[0].texture = job->texture;
    op.color[0].generation = job->texture_generation;
    op.color[0].has_view = 0;
    NvnNativeReplayHost host = {job->channel, NVN_REPLAY_CLIP_LIMIT};
    NvnReplayEnv env;
    nvn_native_replay_env(&host, job->window_origin_flip, &env);
    return env.render_targets(env.ctx, &op);
}
static void frame_replay_lists(const FrameJob* job, NvnReplayStats* stats, uint64_t* dispatches,
                               uint32_t* dispatch_refusals) {
    NvnNativeReplayHost host = {job->channel, NVN_REPLAY_CLIP_LIMIT};
    NvnReplayEnv env;
    nvn_native_replay_env(&host, job->window_origin_flip, &env);
    for (uint32_t i = 0; i < job->frame.handle_count; i++) {
        pm_lock(&objs_lock);
        const NvnOpList* list = nvn_list_find(&lists, job->frame.handles[i]);
        pm_unlock(&objs_lock);
        if (!list) {
            stats->faults++;
            LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame handle=%llx is not a live list\n",
                      (unsigned long long)job->frame.handles[i]);
            continue;
        }
        nvn_replay_list(&env, list, stats);
    }
    if (dispatches) *dispatches = host.dispatches;
    if (dispatch_refusals) *dispatch_refusals = host.dispatch_refusals;
}
static void frame_end(const FrameJob* job) {
    r3d_const_flush(job->channel);
    r3d_pending_flush();
    if (g_renderer->stream_end) g_renderer->stream_end();
    if (g_renderer->stream_run) g_renderer->stream_run(0);
}
static int frame_show(const FrameJob* job, const FrameParts* parts) {
    if (!parts->window_valid || !parts->texture_valid || !parts->pool_valid) return 0;
    NvnPresentArgs args;
    if (!nvn_window_present_args(&parts->texture, parts->pool_storage, &parts->crop, &args)) {
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame texture=%llx cannot be shown (format=%x width=%u height=%u)\n",
                  (unsigned long long)job->texture, parts->texture.builder.format, parts->texture.builder.width,
                  parts->texture.builder.height);
        return 0;
    }
    if (!g_renderer->present_buffer) {
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: frame texture=%llx is not shown: the renderer has no present_buffer\n",
                  (unsigned long long)job->texture);
        return 0;
    }
    g_renderer->present_buffer(args.cpu_addr, args.w, args.h, args.pitch, args.layout, args.bhl, args.fmt, args.crop, 0);
    __atomic_add_fetch(&g_frames, 1, __ATOMIC_RELAXED);
    cheats_frame();
    SNX_FRAME_MARK();
    return 1;
}
static void frame_finish(FrameJob* job) {
    pm_lock(&objs_lock);
    NvnObj* wo = nvn_objtab_valid(&objs, job->window, job->window_generation) ? nvn_objtab_find(&objs, job->window) : NULL;
    WindowRec* window = wo && wo->kind == NVN_OBJ_WINDOW ? wo->data : NULL;
    if (window && job->index < window->texture_count && window->states[job->index] == NVN_IMAGE_PRESENTED)
        window->states[job->index] = NVN_IMAGE_FREE;
    for (uint32_t i = 0; i < job->frame.token_count; i++) {
        const NvnFrameToken* token = &job->frame.tokens[i];
        NvnObj* so = nvn_objtab_valid(&objs, token->sync, token->generation)
                         ? nvn_objtab_find(&objs, token->sync) : NULL;
        if (!so || so->kind != NVN_OBJ_SYNC) continue;
        SyncRec* sync = so->data;
        sync->state = nvn_sync_after_token(sync->state, &sync->fences);
    }
    nvn_frame_release(&job->frame, &lists);
    pc_broadcast(&frame_done_cv); /* the image and the syncs are up to date */
    pm_unlock(&objs_lock);
}
static void frame_line(const FrameJob* job, const NvnReplayStats* stats, int shown, uint32_t fences,
                       uint32_t frame_number, uint64_t dispatches, uint32_t dispatch_refusals) {
    uint32_t ops = 0;
    for (uint32_t i = 0; i < NVN_OP_COUNT; i++) ops += stats->replayed[i];
    uint32_t missing = __atomic_load_n(&missing_calls, __ATOMIC_RELAXED);
    uint32_t dropped = __atomic_load_n(&dropped_ops, __ATOMIC_RELAXED);
    static uint32_t last_missing_calls, last_dropped_ops; /* only the GPU thread runs the frame jobs */
    uint32_t missing_delta = missing - last_missing_calls, dropped_delta = dropped - last_dropped_ops;
    pm_lock(&objs_lock);
    uint32_t submitted = frame_submitted_lists, skipped = frame_skipped_lists, acquires = frame_acquires;
    uint32_t live = nvn_list_live(&lists);
    pm_unlock(&objs_lock);
    uint32_t gops = stats->replayed[NVN_OP_CLEAR_DEPTH_STENCIL] + stats->replayed[NVN_OP_CLEAR_TEXTURE] +
                    stats->replayed[NVN_OP_COPY_TEXTURE] + stats->replayed[NVN_OP_REPORT_COUNTER] +
                    stats->replayed[NVN_OP_BIND_IMAGE];
    int abnormal = stats->faults != 0 || dropped_delta != 0 || missing_delta != 0 || !shown;
    char causes[256];
    replay_causes_take(causes, sizeof causes);
    if ((abnormal && nvn_frame_log_due(frame_number, 1)) || (!abnormal && nvn_frame_log_due(frame_number, 0))) {
        if (causes[0]) LOG("nvn-native: frame %u refusals: %s\n", frame_number, causes);
        last_missing_calls = missing; /* the deltas of the next line are from this one */
        last_dropped_ops = dropped;
        if (abnormal)
            LOG_LIMIT(NVN_FRAME_ABNORMAL_LIMIT,
                      "nvn-native: frame %u window=%llx index=%u lists=%u (live %u) ops=%u faults=%u methods=%u "
                      "draws=%u dispatches=%llu dispatch_refusals=%u vbinds=%u gops=%u dropped=%u missing=%u "
                      "skipped_lists=%u acquires=%u fences=%u shown=%d\n",
                      frame_number, (unsigned long long)job->window, job->index, submitted, live, ops, stats->faults,
                      stats->methods, stats->replayed[NVN_OP_DRAW], (unsigned long long)dispatches, dispatch_refusals,
                      stats->replayed[NVN_OP_BIND_VERTEX_ATTRIB] + stats->replayed[NVN_OP_BIND_VERTEX_STREAM], gops,
                      dropped_delta, missing_delta, skipped, acquires, fences, shown);
        else
            LOG("nvn-native: frame %u window=%llx index=%u lists=%u (live %u) ops=%u faults=%u methods=%u draws=%u "
                "dispatches=%llu dispatch_refusals=%u vbinds=%u gops=%u dropped=%u missing=%u skipped_lists=%u "
                "acquires=%u fences=%u shown=%d\n",
                frame_number, (unsigned long long)job->window, job->index, submitted, live, ops, stats->faults,
                stats->methods, stats->replayed[NVN_OP_DRAW], (unsigned long long)dispatches, dispatch_refusals,
                stats->replayed[NVN_OP_BIND_VERTEX_ATTRIB] + stats->replayed[NVN_OP_BIND_VERTEX_STREAM], gops,
                dropped_delta, missing_delta, skipped, acquires, fences, shown);
    }
}
typedef struct {
    GpuChannel* channel;
    uint32_t methods;
} FrameInitReplay;

static void frame_init_method(void* ctx, uint32_t method, uint32_t value) {
    FrameInitReplay* replay = ctx;
    maxwell3d_method(replay->channel, method, value, REPLAY_METHOD_LAST);
    replay->methods++;
}

/* Replays the macro code that the library builder wrote at QueueInitialize, before the frame's own lists. */
static void frame_replay_init(const FrameJob* job) {
    if (!job->init_word_count) return;
    FrameInitReplay replay = {job->channel, 0};
    uint32_t bad = 0;
    if (nvn_pushbuf_decode(job->init_words, job->init_word_count, frame_init_method, &replay, &bad) != 0) {
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: queue init stream refused at word=%u of %u\n", bad,
                  job->init_word_count);
        return;
    }
    LOG("nvn-native: queue init stream replayed methods=%u\n", replay.methods);
}

/* Runs on the GPU thread: it frees the job that Present made. */
static void frame_run(void* arg) {
    FrameJob* job = arg;
    static uint32_t frame_jobs; /* only the GPU thread runs the frame jobs */
    frame_begin();
    FrameParts parts;
    frame_read_parts(job, &parts);
    frame_first_log(job, &parts);
    frame_replay_init(job);
    frame_set_target(job, &parts);
    NvnReplayStats stats = {0};
    uint64_t dispatches = 0;
    uint32_t dispatch_refusals = 0;
    frame_replay_lists(job, &stats, &dispatches, &dispatch_refusals);
    frame_end(job);
    int shown = frame_show(job, &parts);
    uint32_t fences = job->frame.token_count;
    uint32_t frame_number = ++frame_jobs;
    frame_finish(job);
    frame_line(job, &stats, shown, fences, frame_number, dispatches, dispatch_refusals);
    free(job->init_words);
    free(job);
}

/* nvnQueueSubmitCommands(queue, count, handles): copies the handle array at once and adds the lists to the pending
   frame (decision 8). The game alternates two arrays and reuses them, so the copy is not optional. */
static void h_queue_submit_commands(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueSubmitCommands";
    pm_lock(&objs_lock);
    QueueRec* q = queue_rec(c->x[0], name);
    int32_t count = (int32_t)c->x[1];
    if (!q || count <= 0) {
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t handle_count = (uint32_t)count;
    if (handle_count > COMMAND_CALL_COUNT_MAX) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx count=%u is above the limit, nothing submitted\n",
                  name, (unsigned long long)c->x[0], handle_count);
        pm_unlock(&objs_lock);
        return;
    }
    if (!native_guest_readable(c->x[2], (u64)handle_count * 8)) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx has an unreadable handle array=%llx\n",
                  name, (unsigned long long)c->x[0], (unsigned long long)c->x[2]);
        pm_unlock(&objs_lock);
        return;
    }
    uint64_t* handles = malloc((size_t)handle_count * sizeof *handles);
    if (!handles) fatal("nvn-native: out of memory for the submitted handles of a queue\n");
    for (uint32_t i = 0; i < handle_count; i++) handles[i] = gr64(c->x[2] + (u64)i * 8);
    uint32_t skipped = 0;
    int result = nvn_frame_add_lists(&q->pending, &lists, handles, handle_count, &skipped);
    free(handles);
    if (result == NVN_FRAME_MEMORY) fatal("nvn-native: out of memory for the pending frame of a queue\n");
    if (result == NVN_FRAME_LIMIT) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx count=%u does not fit in the pending frame\n",
                  name, (unsigned long long)c->x[0], handle_count);
    } else {
        if (skipped)
            LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx skipped %u handles that are not live lists\n",
                      name, (unsigned long long)c->x[0], skipped);
        frame_submitted_lists += handle_count - skipped;
        frame_skipped_lists += skipped;
    }
    pm_unlock(&objs_lock);
}

/* nvnQueueFenceSync(queue, sync, condition, flags): the sync is signaled when the pending frame job runs, or at once
   when the queue has no pending list (decision 10). */
static void h_queue_fence_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueFenceSync";
    pm_lock(&objs_lock);
    QueueRec* q = queue_rec(c->x[0], name);
    if (!q) {
        pm_unlock(&objs_lock);
        return;
    }
    NvnObj* so = nvn_objtab_find(&objs, c->x[1]);
    SyncRec* sync = so && so->kind == NVN_OBJ_SYNC ? so->data : NULL;
    if (!sync) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown sync=%llx\n", name, (unsigned long long)c->x[1]);
        pm_unlock(&objs_lock);
        return;
    }
    if ((uint32_t)c->x[2] != NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s condition=%u is not supported\n", name, (uint32_t)c->x[2]);
        pm_unlock(&objs_lock);
        return;
    }
    if (c->x[3])
        LOG_LIMIT(8, "nvn-native: %s flags=%llx (bit 0 FLUSH_FOR_CPU is done by the host)\n",
                  name, (unsigned long long)c->x[3]);
    if (nvn_frame_is_empty(&q->pending)) {
        sync->state = NVN_SYNC_SIGNALED; /* no GPU work of this queue waits for the sync */
    } else if (nvn_frame_add_token(&q->pending, c->x[1], so->generation)) {
        sync->fences++;
        sync->state = NVN_SYNC_FENCED;
    } else {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s sync=%llx does not fit in the pending frame\n",
                  name, (unsigned long long)c->x[1]);
    }
    pm_unlock(&objs_lock);
}

/* nvnQueuePresentTexture(queue, window, index): closes the pending frame and hands it to the GPU (decision 11). A
   present that names an image that is not ACQUIRED is a fault of the game: nothing is shown and the frame stays. */
static void h_queue_present_texture(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueuePresentTexture";
    const u64 queue_addr = c->x[0], window_addr = c->x[1];
    int32_t index = (int32_t)c->x[2];
    pm_lock(&objs_lock);
    QueueRec* q = known_record(queue_addr, NVN_OBJ_QUEUE);
    NvnObj* wo = nvn_objtab_find(&objs, window_addr);
    WindowRec* window = wo && wo->kind == NVN_OBJ_WINDOW ? wo->data : NULL;
    const char* reason = NULL;
    if (!q)
        reason = "unknown queue";
    else if (!window)
        reason = "unknown window";
    else if (index < 0 || (uint32_t)index >= window->texture_count)
        reason = "index outside the window textures";
    else if (window->states[index] != NVN_IMAGE_ACQUIRED)
        reason = "window image is not acquired";
    if (reason) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s failed: %s (queue=%llx window=%llx index=%d)\n",
                  name, reason, (unsigned long long)queue_addr, (unsigned long long)window_addr, index);
        pm_unlock(&objs_lock);
        return;
    }
    window->states[index] = NVN_IMAGE_PRESENTED;
    FrameJob job = {0};
    job.frame = nvn_frame_take(&q->pending);
    job.queue = queue_addr;
    job.window = window_addr;
    job.window_generation = wo->generation;
    job.texture = window->textures[index];
    NvnObj* to = nvn_objtab_find(&objs, job.texture);
    job.texture_generation = to ? to->generation : 0;
    job.index = (uint32_t)index;
    job.channel = q->channel;
    if (!q->init_replayed) {
        if (q->init_word_count) {
            job.init_words = malloc((size_t)q->init_word_count * sizeof *job.init_words);
            if (!job.init_words) fatal("nvn-native: out of memory for the macro code of a frame job\n");
            memcpy(job.init_words, q->init_words, (size_t)q->init_word_count * sizeof *job.init_words);
            job.init_word_count = q->init_word_count;
        }
        q->init_replayed = 1; /* this job carries the replay, even when the builder left nothing */
    }
    const DeviceRec* device = known_record(window->device, NVN_OBJ_DEVICE);
    job.window_origin_flip = device && device->window_origin == 1;
    FrameJob* dispatched = malloc(sizeof *dispatched);
    if (!dispatched) fatal("nvn-native: out of memory for the frame job of a queue\n");
    *dispatched = job;
    pm_unlock(&objs_lock);
    gpu_call(frame_run, dispatched, NULL, 0);
}

/* The host handlers by NVN function name. Later steps add their entries here (G2 to G5 of docs/nvn-design.md, 2-6). */
typedef struct {
    const char* name;
    GuestFn handler;
} NativeProc;
static const NativeProc native_procs[] = {
    {"nvnDeviceBuilderSetDefaults", h_device_builder_set_defaults},
    {"nvnDeviceBuilderSetFlags", h_device_builder_set_flags},
    {"nvnDeviceBuilderGetFlags", h_device_builder_get_flags},
    {"nvnDeviceInitialize", h_device_initialize},
    {"nvnDeviceSetWindowOriginMode", h_device_set_window_origin_mode},
    {"nvnDeviceGetWindowOriginMode", h_device_get_window_origin_mode},
    {"nvnDeviceSetDepthMode", h_device_set_depth_mode},
    {"nvnDeviceGetDepthMode", h_device_get_depth_mode},
    {"nvnDeviceGetInteger", h_device_get_integer},
    {"nvnDeviceGetTimestampInNanoseconds", h_device_get_timestamp_ns},
    {"nvnDeviceGetTextureHandle", h_device_get_texture_handle},
    {"nvnDeviceGetImageHandle", h_device_get_image_handle},
    {"nvnQueueBuilderSetDevice", h_queue_builder_set_device},
    {"nvnQueueBuilderGetDevice", h_queue_builder_get_device},
    {"nvnQueueBuilderSetDefaults", h_queue_builder_set_defaults},
    {"nvnQueueBuilderSetFlags", h_queue_builder_set_flags},
    {"nvnQueueBuilderGetFlags", h_queue_builder_get_flags},
    {"nvnQueueBuilderSetQueuePriority", h_queue_builder_set_priority},
    {"nvnQueueBuilderGetQueuePriority", h_queue_builder_get_priority},
    {"nvnQueueBuilderSetQueueMemory", h_queue_builder_set_queue_memory},
    {"nvnQueueBuilderGetMemory", h_queue_builder_get_memory},
    {"nvnQueueBuilderGetMemorySize", h_queue_builder_get_memory_size},
    {"nvnQueueBuilderSetCommandMemorySize", h_queue_builder_set_command_memory_size},
    {"nvnQueueBuilderGetCommandMemorySize", h_queue_builder_get_command_memory_size},
    {"nvnQueueBuilderSetComputeMemorySize", h_queue_builder_set_compute_memory_size},
    {"nvnQueueBuilderGetComputeMemorySize", h_queue_builder_get_compute_memory_size},
    {"nvnQueueBuilderSetControlMemorySize", h_queue_builder_set_control_memory_size},
    {"nvnQueueBuilderGetControlMemorySize", h_queue_builder_get_control_memory_size},
    {"nvnQueueBuilderSetCommandFlushThreshold", h_queue_builder_set_command_flush_threshold},
    {"nvnQueueBuilderGetCommandFlushThreshold", h_queue_builder_get_command_flush_threshold},
    {"nvnQueueBuilderGetQueueMemorySize", h_queue_builder_get_queue_memory_size},
    {"nvnQueueInitialize", h_queue_initialize},
    {"nvnQueueSubmitCommands", h_queue_submit_commands},
    {"nvnQueueFenceSync", h_queue_fence_sync},
    {"nvnQueuePresentTexture", h_queue_present_texture},
    {"nvnMemoryPoolBuilderSetDefaults", h_memory_pool_builder_set_defaults},
    {"nvnMemoryPoolBuilderSetDevice", h_memory_pool_builder_set_device},
    {"nvnMemoryPoolBuilderSetFlags", h_memory_pool_builder_set_flags},
    {"nvnMemoryPoolBuilderSetStorage", h_memory_pool_builder_set_storage},
    {"nvnMemoryPoolInitialize", h_memory_pool_initialize},
    {"nvnMemoryPoolFinalize", h_memory_pool_finalize},
    {"nvnMemoryPoolMap", h_memory_pool_map},
    {"nvnMemoryPoolGetFlags", h_memory_pool_get_flags},
    {"nvnMemoryPoolGetBufferAddress", h_memory_pool_get_buffer_address},
    {"nvnTexturePoolInitialize", h_texture_pool_initialize},
    {"nvnTexturePoolGetSize", h_texture_pool_get_size},
    {"nvnSamplerPoolInitialize", h_sampler_pool_initialize},
    {"nvnSamplerPoolGetSize", h_sampler_pool_get_size},
    {"nvnSamplerBuilderSetDevice", h_sampler_builder_set_device},
    {"nvnSamplerBuilderSetDefaults", h_sampler_builder_set_defaults},
    {"nvnSamplerBuilderSetMinMagFilter", h_sampler_builder_set_min_mag_filter},
    {"nvnSamplerBuilderSetWrapMode", h_sampler_builder_set_wrap_mode},
    {"nvnSamplerBuilderSetLodClamp", h_sampler_builder_set_lod_clamp},
    {"nvnSamplerBuilderSetLodBias", h_sampler_builder_set_lod_bias},
    {"nvnSamplerBuilderSetCompare", h_sampler_builder_set_compare},
    {"nvnSamplerBuilderSetBorderColor", h_sampler_builder_set_border_color},
    {"nvnSamplerBuilderSetMaxAnisotropy", h_sampler_builder_set_max_anisotropy},
    {"nvnSamplerBuilderSetReductionFilter", h_sampler_builder_set_reduction_filter},
    {"nvnSamplerBuilderSetLodSnap", h_sampler_builder_set_lod_snap},
    {"nvnTextureBuilderSetDevice", h_texture_builder_set_device},
    {"nvnTextureBuilderSetDefaults", h_texture_builder_set_defaults},
    {"nvnTextureBuilderSetFlags", h_texture_builder_set_flags},
    {"nvnTextureBuilderSetTarget", h_texture_builder_set_target},
    {"nvnTextureBuilderSetSize2D", h_texture_builder_set_size_2d},
    {"nvnTextureBuilderSetSize3D", h_texture_builder_set_size_3d},
    {"nvnTextureBuilderSetLevels", h_texture_builder_set_levels},
    {"nvnTextureBuilderSetFormat", h_texture_builder_set_format},
    {"nvnTextureBuilderSetSwizzle", h_texture_builder_set_swizzle},
    {"nvnTextureBuilderSetDepthStencilMode", h_texture_builder_set_depth_stencil_mode},
    {"nvnTextureBuilderSetStorage", h_texture_builder_set_storage},
    {"nvnTextureBuilderSetStride", h_texture_builder_set_stride},
    {"nvnTextureBuilderGetWidth", h_texture_builder_get_width},
    {"nvnTextureBuilderGetHeight", h_texture_builder_get_height},
    {"nvnTextureBuilderGetStorageAlignment", h_texture_builder_get_storage_alignment},
    {"nvnTextureBuilderGetStorageSize", h_texture_builder_get_storage_size},
    {"nvnSamplerInitialize", h_sampler_initialize},
    {"nvnSamplerFinalize", h_sampler_finalize},
    {"nvnSamplerPoolRegisterSampler", h_sampler_pool_register_sampler},
    {"nvnSamplerCompare", h_sampler_compare},
    {"nvnTextureInitialize", h_texture_initialize},
    {"nvnTextureFinalize", h_texture_finalize},
    {"nvnTextureFlushTexels", h_texture_flush_texels},
    {"nvnWindowBuilderSetDefaults", h_window_builder_set_defaults},
    {"nvnWindowBuilderSetDevice", h_window_builder_set_device},
    {"nvnWindowBuilderSetNativeWindow", h_window_builder_set_native_window},
    {"nvnWindowBuilderSetTextures", h_window_builder_set_textures},
    {"nvnWindowBuilderSetPresentInterval", h_window_builder_set_present_interval},
    {"nvnWindowInitialize", h_window_initialize},
    {"nvnWindowSetPresentInterval", h_window_set_present_interval},
    {"nvnWindowSetCrop", h_window_set_crop},
    {"nvnSyncInitialize", h_sync_initialize},
    {"nvnWindowAcquireTexture", h_window_acquire_texture},
    {"nvnSyncWait", h_sync_wait},
    {"nvnTextureBuilderSetWidth", h_texture_builder_set_width},
    {"nvnTextureBuilderSetHeight", h_texture_builder_set_height},
    {"nvnTextureBuilderSetDepth", h_texture_builder_set_depth},
    {"nvnTextureBuilderSetPackagedTextureLayout", h_texture_builder_set_packaged_texture_layout},
    {"nvnTextureViewSetDefaults", h_texture_view_set_defaults},
    {"nvnTextureViewSetLevels", h_texture_view_set_levels},
    {"nvnTextureViewSetLayers", h_texture_view_set_layers},
    {"nvnTextureViewSetFormat", h_texture_view_set_format},
    {"nvnTextureViewSetSwizzle", h_texture_view_set_swizzle},
    {"nvnTextureViewSetDepthStencilMode", h_texture_view_set_depth_stencil_mode},
    {"nvnTextureViewSetTarget", h_texture_view_set_target},
    {"nvnTextureViewGetLevels", h_texture_view_get_levels},
    {"nvnTextureViewGetLayers", h_texture_view_get_layers},
    {"nvnTextureViewGetTarget", h_texture_view_get_target},
    {"nvnTextureViewGetFormat", h_texture_view_get_format},
    {"nvnTextureViewGetSwizzle", h_texture_view_get_swizzle},
    {"nvnTextureViewGetDepthStencilMode", h_texture_view_get_depth_stencil_mode},
    {"nvnTextureGetWidth", h_texture_get_width},
    {"nvnTextureGetHeight", h_texture_get_height},
    {"nvnTextureGetStride", h_texture_get_stride},
    {"nvnTextureGetTarget", h_texture_get_target},
    {"nvnTextureGetDepth", h_texture_get_depth},
    {"nvnTexturePoolRegisterTexture", h_texture_pool_register_texture},
    {"nvnTexturePoolRegisterImage", h_texture_pool_register_image},
    {"nvnTextureWriteTexels", h_texture_write_texels},
    {"nvnBufferBuilderSetDevice", h_buffer_builder_set_device},
    {"nvnBufferBuilderSetDefaults", h_buffer_builder_set_defaults},
    {"nvnBufferBuilderSetStorage", h_buffer_builder_set_storage},
    {"nvnBufferBuilderGetSize", h_buffer_builder_get_size},
    {"nvnBufferInitialize", h_buffer_initialize},
    {"nvnBufferFinalize", h_buffer_finalize},
    {"nvnBufferMap", h_buffer_map},
    {"nvnBufferGetAddress", h_buffer_get_address},
    {"nvnBufferGetSize", h_buffer_get_size},
    {"nvnBufferSetDebugLabel", h_buffer_set_debug_label},
    {"nvnDeviceSetDebugLabel", h_set_debug_label},
    {"nvnCommandBufferSetDebugLabel", h_set_debug_label},
    {"nvnMemoryPoolSetDebugLabel", h_set_debug_label},
    {"nvnProgramSetDebugLabel", h_set_debug_label},
    {"nvnQueueSetDebugLabel", h_set_debug_label},
    {"nvnSamplerSetDebugLabel", h_set_debug_label},
    {"nvnSyncSetDebugLabel", h_set_debug_label},
    {"nvnTextureSetDebugLabel", h_set_debug_label},
    {"nvnWindowSetDebugLabel", h_set_debug_label},
    {"nvnTexturePoolSetDebugLabel", h_set_debug_label},
    {"nvnSamplerPoolSetDebugLabel", h_set_debug_label},
    {"nvnProgramInitialize", h_program_initialize},
    {"nvnProgramSetShadersExt", h_program_set_shaders_ext},
    {"nvnProgramFinalize", h_program_finalize},
    {"nvnCommandBufferInitialize", h_command_buffer_initialize},
    {"nvnCommandBufferFinalize", h_command_buffer_finalize},
    {"nvnCommandBufferSetMemoryCallback", h_command_buffer_set_memory_callback},
    {"nvnCommandBufferSetMemoryCallbackData", h_command_buffer_set_memory_callback_data},
    {"nvnCommandBufferAddCommandMemory", h_command_buffer_add_command_memory},
    {"nvnCommandBufferAddControlMemory", h_command_buffer_add_control_memory},
    {"nvnCommandBufferBeginRecording", h_command_buffer_begin_recording},
    {"nvnCommandBufferEndRecording", h_command_buffer_end_recording},
    {"nvnCommandBufferCallCommands", h_command_buffer_call_commands},
    {"nvnCommandBufferSetTexturePool", h_command_buffer_set_texture_pool},
    {"nvnCommandBufferSetSamplerPool", h_command_buffer_set_sampler_pool},
    {"nvnCommandBufferBindTexture", h_command_buffer_bind_texture},
    {"nvnCommandBufferBindUniformBuffer", h_command_buffer_bind_uniform_buffer},
    {"nvnCommandBufferBindStorageBuffer", h_command_buffer_bind_storage_buffer},
    {"nvnCommandBufferBindVertexBuffer", h_command_buffer_bind_vertex_buffer},
    {"nvnCommandBufferBindTransformFeedbackBuffer", h_command_buffer_bind_transform_feedback_buffer},
    {"nvnCommandBufferBindUniformBuffers", h_command_buffer_bind_uniform_buffers},
    {"nvnCommandBufferBindStorageBuffers", h_command_buffer_bind_storage_buffers},
    {"nvnCommandBufferBindVertexBuffers", h_command_buffer_bind_vertex_buffers},
    {"nvnCommandBufferBindTransformFeedbackBuffers", h_command_buffer_bind_transform_feedback_buffers},
    {"nvnCommandBufferBindVertexAttribState", h_command_buffer_bind_vertex_attrib_state},
    {"nvnCommandBufferBindVertexStreamState", h_command_buffer_bind_vertex_stream_state},
    {"nvnCommandBufferDrawArrays", h_command_buffer_draw_arrays},
    {"nvnCommandBufferDrawArraysInstanced", h_command_buffer_draw_arrays_instanced},
    {"nvnCommandBufferDrawElements", h_command_buffer_draw_elements},
    {"nvnCommandBufferDrawElementsBaseVertex", h_command_buffer_draw_elements_base_vertex},
    {"nvnCommandBufferDrawElementsInstanced", h_command_buffer_draw_elements_instanced},
    {"nvnCommandBufferDispatchCompute", h_command_buffer_dispatch_compute},
    {"nvnCommandBufferBindProgram", h_command_buffer_bind_program},
    {"nvnCommandBufferBindColorState", h_command_buffer_bind_color_state},
    {"nvnCommandBufferBindDepthStencilState", h_command_buffer_bind_depth_stencil_state},
    {"nvnCommandBufferBindBlendState", h_command_buffer_bind_blend_state},
    {"nvnCommandBufferBindPolygonState", h_command_buffer_bind_polygon_state},
    {"nvnCommandBufferBindMultisampleState", h_command_buffer_bind_multisample_state},
    {"nvnCommandBufferBindChannelMaskState", h_command_buffer_bind_channel_mask_state},
    {"nvnBlendStateSetDefaults", h_blend_state_set_defaults},
    {"nvnBlendStateSetBlendTarget", h_blend_state_set_blend_target},
    {"nvnBlendStateSetBlendFunc", h_blend_state_set_blend_func},
    {"nvnBlendStateSetBlendEquation", h_blend_state_set_blend_equation},
    {"nvnBlendStateGetBlendTarget", h_blend_state_get_blend_target},
    {"nvnBlendStateGetBlendFunc", h_blend_state_get_blend_func},
    {"nvnBlendStateGetBlendEquation", h_blend_state_get_blend_equation},
    {"nvnChannelMaskStateSetDefaults", h_channel_mask_state_set_defaults},
    {"nvnChannelMaskStateSetChannelMask", h_channel_mask_state_set_channel_mask},
    {"nvnChannelMaskStateGetChannelMask", h_channel_mask_state_get_channel_mask},
    {"nvnColorStateSetDefaults", h_color_state_set_defaults},
    {"nvnColorStateSetBlendEnable", h_color_state_set_blend_enable},
    {"nvnColorStateSetLogicOp", h_color_state_set_logic_op},
    {"nvnColorStateSetAlphaTest", h_color_state_set_alpha_test},
    {"nvnColorStateGetBlendEnable", h_color_state_get_blend_enable},
    {"nvnColorStateGetAlphaTest", h_color_state_get_alpha_test},
    {"nvnColorStateGetLogicOp", h_color_state_get_logic_op},
    {"nvnDepthStencilStateSetDefaults", h_depth_stencil_state_set_defaults},
    {"nvnDepthStencilStateSetDepthTestEnable", h_depth_stencil_state_set_depth_test_enable},
    {"nvnDepthStencilStateSetDepthWriteEnable", h_depth_stencil_state_set_depth_write_enable},
    {"nvnDepthStencilStateSetDepthFunc", h_depth_stencil_state_set_depth_func},
    {"nvnDepthStencilStateSetStencilTestEnable", h_depth_stencil_state_set_stencil_test_enable},
    {"nvnDepthStencilStateSetStencilFunc", h_depth_stencil_state_set_stencil_func},
    {"nvnDepthStencilStateSetStencilOp", h_depth_stencil_state_set_stencil_op},
    {"nvnDepthStencilStateGetDepthTestEnable", h_depth_stencil_state_get_depth_test_enable},
    {"nvnDepthStencilStateGetDepthWriteEnable", h_depth_stencil_state_get_depth_write_enable},
    {"nvnDepthStencilStateGetDepthFunc", h_depth_stencil_state_get_depth_func},
    {"nvnDepthStencilStateGetStencilTestEnable", h_depth_stencil_state_get_stencil_test_enable},
    {"nvnDepthStencilStateGetStencilFunc", h_depth_stencil_state_get_stencil_func},
    {"nvnDepthStencilStateGetStencilOp", h_depth_stencil_state_get_stencil_op},
    {"nvnMultisampleStateSetDefaults", h_multisample_state_set_defaults},
    {"nvnMultisampleStateSetMultisampleEnable", h_multisample_state_set_multisample_enable},
    {"nvnMultisampleStateSetAlphaToCoverageEnable", h_multisample_state_set_alpha_to_coverage_enable},
    {"nvnMultisampleStateSetAlphaToCoverageDither", h_multisample_state_set_alpha_to_coverage_dither},
    {"nvnMultisampleStateSetSamples", h_multisample_state_set_samples},
    {"nvnMultisampleStateSetSampleLocationsEnable", h_multisample_state_set_sample_locations_enable},
    {"nvnMultisampleStateSetSampleLocationsGridEnable", h_multisample_state_set_sample_locations_grid_enable},
    {"nvnMultisampleStateSetRasterSamples", h_multisample_state_set_raster_samples},
    {"nvnMultisampleStateSetCoverageModulationMode", h_multisample_state_set_coverage_modulation_mode},
    {"nvnMultisampleStateSetCoverageToColorEnable", h_multisample_state_set_coverage_to_color_enable},
    {"nvnMultisampleStateSetCoverageToColorOutput", h_multisample_state_set_coverage_to_color_output},
    {"nvnMultisampleStateGetMultisampleEnable", h_multisample_state_get_multisample_enable},
    {"nvnMultisampleStateGetSamples", h_multisample_state_get_samples},
    {"nvnMultisampleStateGetAlphaToCoverageEnable", h_multisample_state_get_alpha_to_coverage_enable},
    {"nvnMultisampleStateGetAlphaToCoverageDither", h_multisample_state_get_alpha_to_coverage_dither},
    {"nvnMultisampleStateGetRasterSamples", h_multisample_state_get_raster_samples},
    {"nvnMultisampleStateGetCoverageModulationMode", h_multisample_state_get_coverage_modulation_mode},
    {"nvnMultisampleStateGetCoverageToColorEnable", h_multisample_state_get_coverage_to_color_enable},
    {"nvnMultisampleStateGetCoverageToColorOutput", h_multisample_state_get_coverage_to_color_output},
    {"nvnPolygonStateSetDefaults", h_polygon_state_set_defaults},
    {"nvnPolygonStateSetCullFace", h_polygon_state_set_cull_face},
    {"nvnPolygonStateSetFrontFace", h_polygon_state_set_front_face},
    {"nvnPolygonStateSetPolygonMode", h_polygon_state_set_polygon_mode},
    {"nvnPolygonStateSetPolygonOffsetEnables", h_polygon_state_set_polygon_offset_enables},
    {"nvnPolygonStateGetCullFace", h_polygon_state_get_cull_face},
    {"nvnPolygonStateGetFrontFace", h_polygon_state_get_front_face},
    {"nvnPolygonStateGetPolygonMode", h_polygon_state_get_polygon_mode},
    {"nvnPolygonStateGetPolygonOffsetEnables", h_polygon_state_get_polygon_offset_enables},
    {"nvnVertexAttribStateSetDefaults", h_vertex_attrib_state_set_defaults},
    {"nvnVertexAttribStateSetFormat", h_vertex_attrib_state_set_format},
    {"nvnVertexAttribStateSetStreamIndex", h_vertex_attrib_state_set_stream_index},
    {"nvnVertexStreamStateSetDefaults", h_vertex_stream_state_set_defaults},
    {"nvnVertexStreamStateSetStride", h_vertex_stream_state_set_stride},
    {"nvnVertexStreamStateSetDivisor", h_vertex_stream_state_set_divisor},
    {"nvnVertexStreamStateGetStride", h_vertex_stream_state_get_stride},
    {"nvnVertexStreamStateGetDivisor", h_vertex_stream_state_get_divisor},
    {"nvnVertexAttribStateGetFormat", h_vertex_attrib_state_get_format},
    {"nvnVertexAttribStateGetStreamIndex", h_vertex_attrib_state_get_stream_index},
    {"nvnCommandBufferSetRenderTargets", h_command_buffer_set_render_targets},
    {"nvnCommandBufferSetViewports", h_command_buffer_set_viewports},
    {"nvnCommandBufferSetScissors", h_command_buffer_set_scissors},
    {"nvnCommandBufferClearColor", h_command_buffer_clear_color},
    {"nvnCommandBufferBarrier", h_command_buffer_barrier},
    {"nvnCommandBufferSetStencilRef", h_command_buffer_set_stencil_ref},
    {"nvnCommandBufferSetStencilValueMask", h_command_buffer_set_stencil_value_mask},
    {"nvnCommandBufferSetStencilMask", h_command_buffer_set_stencil_mask},
    {"nvnCommandBufferSetBlendColor", h_command_buffer_set_blend_color},
    {"nvnCommandBufferSetAlphaRef", h_command_buffer_set_alpha_ref},
    {"nvnCommandBufferSetDepthBounds", h_command_buffer_set_depth_bounds},
    {"nvnCommandBufferSetRasterizerDiscard", h_command_buffer_set_rasterizer_discard},
    {"nvnCommandBufferSetPolygonOffsetClamp", h_command_buffer_set_polygon_offset_clamp},
    {"nvnCommandBufferSetDepthClamp", h_command_buffer_set_depth_clamp},
    {"nvnCommandBufferSetLineWidth", h_command_buffer_set_line_width},
    {"nvnCommandBufferSetPointSize", h_command_buffer_set_point_size},
    {"nvnCommandBufferSetDepthRanges", h_command_buffer_set_depth_ranges},
    {"nvnCommandBufferSetDepthRange", h_command_buffer_set_depth_range},
    {"nvnCommandBufferClearDepthStencil", h_command_buffer_clear_depth_stencil},
    {"nvnCommandBufferClearTexture", h_command_buffer_clear_texture},
    {"nvnCommandBufferClearTexturei", h_command_buffer_clear_texturei},
    {"nvnCommandBufferClearTextureui", h_command_buffer_clear_textureui},
    {"nvnCommandBufferCopyTextureToTexture", h_command_buffer_copy_texture_to_texture},
    {"nvnCommandBufferReportCounter", h_command_buffer_report_counter},
    {"nvnCommandBufferResetCounter", h_command_buffer_reset_counter},
    {"nvnCommandBufferBeginTransformFeedback", h_command_buffer_begin_transform_feedback},
    {"nvnCommandBufferEndTransformFeedback", h_command_buffer_end_transform_feedback},
    {"nvnCommandBufferBindImage", h_command_buffer_bind_image},
    {"nvnCommandBufferBindImages", h_command_buffer_bind_images},
    {NULL, NULL},
};

typedef struct {
    u64 addr, calls;
    GuestFn handler;
    int missing; /* the handler is the missing-function handler */
    char name[128];
} NativeSlot;
#define NATIVE_SLOTS 1024 /* the game resolves 523 addresses */
static NativeSlot slots[NATIVE_SLOTS];
static u32 nslots;
/* Registration takes the lock and publishes a slot (addr, release ordering) once it is filled in. Calls read the slots
   without the lock. */
static PMutex slots_lock = PMUTEX_INIT;
static GuestFn bootstrap_original, getproc_original;
static u64 getproc_addr; /* the address of nvnDeviceGetProcAddress, hooked */
static int active;
static void getproc_native(CPU* c, u64 entry);

static u32 slot_hash(u64 addr) { return (u32)((addr >> 2) * 2654435761u) & (NATIVE_SLOTS - 1); }
static NativeSlot* slot_find(u64 addr) {
    for (u32 n = 0, i = slot_hash(addr); n < NATIVE_SLOTS; n++, i = (i + 1) & (NATIVE_SLOTS - 1)) {
        u64 a = __atomic_load_n(&slots[i].addr, __ATOMIC_ACQUIRE);
        if (a == addr) return &slots[i];
        if (!a) return NULL;
    }
    return NULL;
}

/* The function that g_disp holds for every host handler. A guest function ends with RET, which sets c->pc to x30, and
   the caller's check_return compares c->pc with its return address. A handler leaves c->pc at the caller's last block
   entry, so without this step the caller runs that block again. The handlers do not change x30. */
static void native_call(CPU* c, u64 entry) {
    NativeSlot* s = slot_find(entry);
    if (!s) fatal("nvn-native: a host handler runs for an address it never registered (%llx)\n", (unsigned long long)entry);
    u64 lr = c->x[30];
    s->handler(c, entry);
    c->pc = lr;
}

static void missing_handler(CPU* c, u64 entry) {
    NativeSlot* s = slot_find(entry);
    if (!s) fatal("nvn-native: the missing-function handler runs for an address it never registered (%llx)\n",
                  (unsigned long long)entry);
    __atomic_fetch_add(&missing_calls, 1, __ATOMIC_RELAXED);
    if (!__atomic_fetch_add(&s->calls, 1, __ATOMIC_RELAXED)) /* the first call: what the game asked for */
        LOG("nvn-native: not implemented: %s (x0=%llx x1=%llx x2=%llx x3=%llx), returns 0\n", s->name,
            (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2],
            (unsigned long long)c->x[3]);
    c->x[0] = 0;
}

static GuestFn handler_for(const char* name, int* missing) {
    for (const NativeProc* p = native_procs; p->name; p++)
        if (!strcmp(p->name, name)) {
            *missing = 0;
            return p->handler;
        }
    *missing = 1;
    return missing_handler;
}

/* The handler of the function that the game resolved as name at addr */
static void install(const char* name, u64 addr) {
    if (addr < g_code_lo || addr >= g_code_hi || (addr & 3)) {
        LOG_LIMIT(8, "nvn-native: %s resolved to %llx, which is not a recompiled function\n", name,
                  (unsigned long long)addr);
        return;
    }
    pm_lock(&slots_lock);
    NativeSlot* s = slot_find(addr);
    if (addr == getproc_addr) { /* the lookup function keeps its hook */
        pm_unlock(&slots_lock);
        return;
    }
    if (s) {
        /* identical functions folded to one address: their calls count under the first name */
        if (strcmp(s->name, name)) LOG_LIMIT(8, "nvn-native: %s shares its address with %s\n", name, s->name);
    } else if (nslots < NATIVE_SLOTS) {
        u32 i = slot_hash(addr);
        while (slots[i].addr) i = (i + 1) & (NATIVE_SLOTS - 1);
        s = &slots[i];
        memcpy(s->name, name, strnlen(name, sizeof s->name - 1) + 1);
        s->handler = handler_for(s->name, &s->missing);
        if (!s->missing) LOG_LIMIT(64, "nvn-native: host handler for %s at %llx\n", s->name, (unsigned long long)addr);
        __atomic_store_n(&s->addr, addr, __ATOMIC_RELEASE);
        nslots++;
        __atomic_store_n(&g_disp[(addr - g_code_lo) / 4], native_call, __ATOMIC_RELEASE);
    } else {
        fatal("nvn-native: more than %u resolved NVN functions\n", NATIVE_SLOTS);
    }
    pm_unlock(&slots_lock);
}

/* nvnDeviceGetProcAddress(device, name). The library's own function runs for the lookup: it returns the address, and
   the host puts its handler there. Open (docs/nvn-design.md, 2-3, item 6): if it reads the device object, which the
   library never initialized in this mode. The first run shows it. */
static void getproc_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[1], name);
    GuestFn original = __atomic_load_n(&getproc_original, __ATOMIC_ACQUIRE);
    if (!original) fatal("nvn-native: the nvnDeviceGetProcAddress hook runs before its original is known\n");
    original(c, entry);
    if (valid && c->x[0]) install(name, c->x[0]);
}

static void bootstrap_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[0], name); /* nvnBootstrapLoader(name) */
    bootstrap_original(c, entry);
    if (!valid || !c->x[0]) return;
    if (strcmp(name, "nvnDeviceGetProcAddress")) {
        install(name, c->x[0]);
        return;
    }
    u64 resolver = c->x[0];
    if (resolver < g_code_lo || resolver >= g_code_hi || (resolver & 3)) return;
    pm_lock(&slots_lock);
    GuestFn* slot = &g_disp[(resolver - g_code_lo) / 4];
    if (*slot && !getproc_original) { /* hook once: with nvn_trace the slot may hold the trace hook, which calls back here */
        __atomic_store_n(&getproc_original, *slot, __ATOMIC_RELEASE);
        getproc_addr = resolver;
        __atomic_store_n(slot, getproc_native, __ATOMIC_RELEASE);
    }
    pm_unlock(&slots_lock);
}

void nvn_native_init(void) {
    if (!g_settings.nvn_native) return;
    pc_init(&frame_done_cv);
    u64 entry = kernel_find_symbol_exact("nvnBootstrapLoader");
    if (!entry || entry < g_code_lo || entry >= g_code_hi || (entry & 3)) {
        WARN("nvn-native: bootstrap loader symbol is unavailable\n");
        return;
    }
    GuestFn* slot = &g_disp[(entry - g_code_lo) / 4];
    if (!*slot) {
        WARN("nvn-native: bootstrap loader has no recompiled entry\n");
        return;
    }
    bootstrap_original = *slot;
    *slot = bootstrap_native; /* before any guest thread runs */
    active = 1;
    LOG("nvn-native: on, the host answers the NVN functions that the game resolves (%u handlers)\n",
        (u32)(sizeof native_procs / sizeof native_procs[0] - 1));
}

int nvn_native_active(void) { return active; }

GuestFn nvn_native_handler(const char* name) {
    int missing;
    GuestFn h = handler_for(name, &missing);
    return missing ? NULL : h;
}

void nvn_native_report(void) {
    if (!active) return;
    pm_lock(&slots_lock);
    u32 missing = 0, called = 0;
    for (u32 i = 0; i < NATIVE_SLOTS; i++) {
        if (!slots[i].addr) continue;
        u64 calls = __atomic_load_n(&slots[i].calls, __ATOMIC_RELAXED);
        if (!slots[i].missing) continue; /* a real handler: the trace counts its calls */
        missing++;
        if (!calls) continue;
        called++;
        LOG("nvn-native: missing %s %llu\n", slots[i].name, (unsigned long long)calls);
    }
    LOG("nvn-native: %u resolved addresses, %u without a handler, %u of them called\n", nslots, missing, called);
    pm_unlock(&slots_lock);
}
