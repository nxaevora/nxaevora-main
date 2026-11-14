/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* nvn_native=1: the host answers the NVN functions that the game resolves through nvnBootstrapLoader and
   nvnDeviceGetProcAddress. Each resolved address gets a host handler in its dispatch slot. A function without a
   handler stops the game unless nvn_native_missing=0. The hook order and the trace are in nvn-design.md, 2-3 and
   2-6. */
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

/* The host's records of the NVN objects. The game's object memory is never written (nvn-design.md, 2-1). Handlers
   take objs_lock for the whole call, and their pointers into a record end with the call. */
static NvnObjTab objs;
static PMutex objs_lock = PMUTEX_INIT;
/* The frame job tells the guest waits that it freed an image and updated the syncs. */
static PCond frame_done_cv;
static NvnListStore lists; /* the command lists of the game, guarded by objs_lock */
/* The frame line counters (3c decision 12). The four guarded by objs_lock are read and written with it; the other two
   are atomic because missing_handler and the recorder run without it. */
static uint32_t frame_acquires, frame_submitted_lists, frame_skipped_lists, frame_unmet_event_waits;
static uint32_t frames_in_flight; /* frame jobs that a post made and frame_finish has not ended */
static uint32_t missing_calls, dropped_ops;

typedef struct {
    uint32_t flags;
} DeviceBuilderRec;
typedef struct {
    uint32_t flags, window_origin, depth_mode;
    uint32_t program_ids; /* the library's counter at device+0x1b30: the id of the last program set, guarded by objs_lock */
} DeviceRec;
typedef struct {
    NvnQueueBuilder builder; /* the builder as it was at QueueInitialize */
    NvnPendingFrame pending; /* the frame that Present closes */
    uint32_t channel_id;     /* the syncpoint id of the private channel of the frame jobs */
    GpuChannel* channel;     /* the private channel of the frame jobs, made at QueueInitialize */
    uint32_t* init_words;    /* the macro code that the library builder wrote at QueueInitialize, malloc'd */
    uint32_t init_word_count;
    uint64_t pool_storage, pool_size, pool_gpu; /* the queue pool of the init words, mapped once and kept */
    uint8_t init_replayed; /* a frame job took the words: the record hands them out once */
    uint32_t last_posted;  /* the syncpoint value that the last posted frame job reaches */
    uint8_t has_posted;    /* a frame job was posted: last_posted has a value */
} QueueRec;
typedef struct {
    uint64_t device, native_window, textures[NVN_WINDOW_MAX_TEXTURES];
    uint32_t texture_count, interval, active_textures;
    NvnWindowCrop crop;
    uint8_t states[NVN_WINDOW_MAX_TEXTURES];
    int32_t last_index;
} WindowRec;
typedef struct {
    uint64_t device;
    NvnSyncState state;
    uint32_t syncpoint; /* the syncpoint id of the queue with the newest fence */
    uint32_t value;     /* what that queue promises to reach for the newest fence */
    uint32_t serial;    /* the newest fence of this sync */
} SyncRec;
/* nvnQueueFenceSync flag bit 0: the call also posts the pending frame. Other bits have no host meaning. */
#define NVN_QUEUE_FENCE_FLUSH_FOR_CPU 1u
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

/* G1: device builder and device (nvn-design.md, 2-6). The library's functions are plain field accesses. */
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
/* DeviceFinalize would destroy the device, its queues, pools and programs; the host keeps them to the process end. */
static void h_device_finalize(CPU* c, u64 entry) {
    (void)entry;
    LOG_LIMIT(8, "nvn-native: nvnDeviceFinalize device=%llx is not supported, the records stay\n",
              (unsigned long long)c->x[0]);
}
/* The library does nothing (sdk d82cdd0, d826410). */
static void h_device_apply_deferred_finalizes(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}
static void h_device_finalize_command_handle(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}
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
/* nvnDeviceGetSeparateTextureHandle and nvnDeviceGetTexelFetchHandle: both are the combined handle with sampler 0. */
static void h_device_get_separate_texture_handle(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_texture_handle((u32)c->x[1], 0);
}
static void h_device_get_texel_fetch_handle(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_texture_handle((u32)c->x[1], 0);
}
static void h_device_get_separate_sampler_handle(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_separate_sampler_handle((u32)c->x[1]);
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
#define QUEUE_LOG_LIMIT 32u /* log lines for faults of the game in the queue functions */
/* The shader pools lie in one 4 GiB window of the GPU address space, so the library subtraction of low32(P) is the
   identity and the entry word is the delta d. */
#define NVN_SHADER_REGION_BASE 0x400000000ull

/* The library builders of QueueInitialize. The host replaces QueueInitialize, so it runs the builders on a host
   arena and keeps the words for the first frame job. The addresses are the sdk addresses of this game version. */
#define NVN_QUEUE_POOL_BUILDER_ADDRESS 0xD838F70ull /* writes the queue pool layout into the pool object */
#define NVN_QUEUE_INIT_BUILDER_ADDRESS 0xD851880ull /* calls d84f800, then the driver constants setup */
#define NVN_QUEUE_POOL_VTABLE_METHOD 0xD840B60ull    /* ldr x0,[x0,#0x40]; ret: the queue pool GPU base */
#define NVN_BUILDER_STACK_BYTES 0x200u               /* the builder's measured stack use is 0x120 */
#define NVN_BUILDER_QUEUE_BYTES 0x1740u              /* the queue image ends at the reserve word +0x1734 */
#define NVN_BUILDER_DEVICE_BYTES 0x1B40u             /* the device image ends after the word +0x1AF4 */
#define NVN_BUILDER_IMAGE_BYTES 0x20u                /* the builder fields that d838f70 reads */
#define NVN_BUILDER_VTABLE_BYTES 0x40u               /* the pool object's vtable; the GPU base method is at +0x38 */
#define NVN_BUILDER_VTABLE_METHOD_OFFSET 0x38u
#define NVN_BUILDER_SHADER_OBJECT_BYTES 0x20u        /* the shader region object; its +0x18 is the region base */
#define NVN_BUILDER_PROGRAM_OFFSET_BYTES 0x130u      /* the program offset object; its +0x120 is the offset */
#define NVN_BUILDER_COMMAND_BYTES 0x2400u            /* the words of both builders; 0x21F4 measured at most */
#define NVN_BUILDER_RESERVE_WORDS 12u
#define NVN_BUILDER_RETURN_LR 0x4E5640ull /* a known, word-aligned return address outside the code */
#define NVN_QUEUE_DEVICE_OFFSET 0x00u
#define NVN_QUEUE_CURSOR_OFFSET 0x16D8u
#define NVN_QUEUE_END_OFFSET 0x16E0u
#define NVN_QUEUE_RESERVE_OFFSET 0x1734u
#define NVN_QUEUE_CLASS_3D_OFFSET 0xECu          /* the class ids that d84f630 stores, by subchannel */
#define NVN_QUEUE_CLASS_COMPUTE_OFFSET 0xF0u
#define NVN_QUEUE_CLASS_DMA_OFFSET 0xF4u
#define NVN_QUEUE_CLASS_INLINE_OFFSET 0xF8u
#define NVN_QUEUE_CLASS_2D_OFFSET 0xFCu
#define NVN_QUEUE_POOL_OBJECT_OFFSET 0x1568u     /* the queue memory pool object inside the queue image */
#define NVN_QUEUE_POOL_SIZE_OFFSET 0x167Cu       /* the pool size that d838f70 writes (obj+0x114) */
#define NVN_QUEUE_POOL_GPU_OFFSET 0x40u          /* the pool GPU base that the vtable method reads (obj+0x40) */
#define NVN_QUEUE_POOL_MAX_BYTES 0x100000u       /* the largest pool size that the host accepts */
#define NVN_BUILDER_IMAGE_FLAGS_OFFSET 0x10u
#define NVN_BUILDER_IMAGE_COMPUTE_HAS_OFFSET 0x15u
#define NVN_BUILDER_IMAGE_COMPUTE_OFFSET 0x1Cu
#define NVN_DEVICE_SHADER_OBJECT_OFFSET 0x1AF8u         /* a pointer to the shader region object */
#define NVN_DEVICE_PROGRAM_OFFSET_OBJECT_OFFSET 0x1B00u /* a pointer to the program offset object */
#define NVN_SHADER_OBJECT_BASE_OFFSET 0x18u             /* the shader region base inside that object */
#define NVN_BUILDER_ARENA_BYTES                                                                            \
    (NVN_BUILDER_STACK_BYTES + NVN_BUILDER_QUEUE_BYTES + NVN_BUILDER_DEVICE_BYTES + NVN_BUILDER_IMAGE_BYTES + \
     NVN_BUILDER_VTABLE_BYTES + NVN_BUILDER_SHADER_OBJECT_BYTES + NVN_BUILDER_PROGRAM_OFFSET_BYTES +       \
     NVN_BUILDER_COMMAND_BYTES)

/* The host arena of the queue init builders: the builder stack, the images and the command buffer. It is allocated
   at the first QueueInitialize and kept for the run. */
typedef struct {
    u64 stack_top, queue, device, builder, vtable, shader_object, program_offset, command;
} BuilderArena;
static BuilderArena builder_arena;
static uint8_t builder_arena_ready;
static PMutex builder_lock = PMUTEX_INIT;

/* The caller holds builder_lock. Each part is 16-byte aligned, so the start of one part is the end of the last. */
static int builder_arena_get(BuilderArena* out) {
    if (!builder_arena_ready) {
        const u64 base = kmem_alloc(NVN_BUILDER_ARENA_BYTES, MS_STATIC, PERM_RW);
        if (!base || !native_guest_readable(base, NVN_BUILDER_ARENA_BYTES) ||
            !native_guest_writable(base, NVN_BUILDER_ARENA_BYTES)) {
            LOG("nvn-native: queue builder arena of %llu bytes cannot be allocated\n",
                (unsigned long long)NVN_BUILDER_ARENA_BYTES);
            return 0;
        }
        builder_arena.stack_top = base + NVN_BUILDER_STACK_BYTES;
        builder_arena.queue = builder_arena.stack_top;
        builder_arena.device = builder_arena.queue + NVN_BUILDER_QUEUE_BYTES;
        builder_arena.builder = builder_arena.device + NVN_BUILDER_DEVICE_BYTES;
        builder_arena.vtable = builder_arena.builder + NVN_BUILDER_IMAGE_BYTES;
        builder_arena.shader_object = builder_arena.vtable + NVN_BUILDER_VTABLE_BYTES;
        builder_arena.program_offset = builder_arena.shader_object + NVN_BUILDER_SHADER_OBJECT_BYTES;
        builder_arena.command = builder_arena.program_offset + NVN_BUILDER_PROGRAM_OFFSET_BYTES;
        builder_arena_ready = 1;
    }
    *out = builder_arena;
    return 1;
}

/* Zeroes a guest range in 8-byte writes; the images here are multiples of 8 bytes. */
static void builder_zero(u64 addr, u64 size) {
    for (u64 at = 0; at < size; at += 8u) gw64(addr + at, 0);
}

/* A byte store into the zero-filled images; the guest has no byte write helper. */
static void builder_byte(u64 addr, uint8_t value) {
    const u64 word_addr = addr & ~3ull;
    const u32 shift = (u32)(addr & 3ull) * 8u;
    gw32(word_addr, (gr32(word_addr) & ~(0xFFu << shift)) | ((u32)value << shift));
}

/* Fills the images that the builders read: the device table, the queue image, the builder fields and the pool
   object with its vtable. Returns 0 after a log line. */
static int builder_images(const BuilderArena* a, const NvnQueueBuilder* b, const NvnQueueDeviceWords* dev,
                          uint32_t window_origin, uint32_t depth_mode) {
    builder_zero(a->queue, NVN_BUILDER_QUEUE_BYTES);
    builder_zero(a->device, NVN_BUILDER_DEVICE_BYTES);
    builder_zero(a->builder, NVN_BUILDER_IMAGE_BYTES);
    builder_zero(a->vtable, NVN_BUILDER_VTABLE_BYTES);
    builder_zero(a->shader_object, NVN_BUILDER_SHADER_OBJECT_BYTES);
    builder_zero(a->program_offset, NVN_BUILDER_PROGRAM_OFFSET_BYTES);
    builder_zero(a->command, NVN_BUILDER_COMMAND_BYTES);

    gw64(a->queue + NVN_QUEUE_DEVICE_OFFSET, a->device);
    gw64(a->queue + NVN_QUEUE_CURSOR_OFFSET, a->command);
    gw64(a->queue + NVN_QUEUE_END_OFFSET, a->command + NVN_BUILDER_COMMAND_BYTES);
    gw32(a->queue + NVN_QUEUE_RESERVE_OFFSET, NVN_BUILDER_RESERVE_WORDS);
    gw32(a->queue + NVN_QUEUE_CLASS_3D_OFFSET, CLASS_MAXWELL_B);
    gw32(a->queue + NVN_QUEUE_CLASS_COMPUTE_OFFSET, CLASS_KEPLER_COMPUTE_B);
    gw32(a->queue + NVN_QUEUE_CLASS_DMA_OFFSET, CLASS_MAXWELL_DMA_COPY_A);
    gw32(a->queue + NVN_QUEUE_CLASS_INLINE_OFFSET, CLASS_KEPLER_INLINE_TO_MEMORY_B);
    gw32(a->queue + NVN_QUEUE_CLASS_2D_OFFSET, CLASS_FERMI_TWOD_A);
    gw64(a->queue + NVN_QUEUE_POOL_OBJECT_OFFSET, a->vtable);
    gw64(a->vtable + NVN_BUILDER_VTABLE_METHOD_OFFSET, NVN_QUEUE_POOL_VTABLE_METHOD);

    NvnQueueInitWord words[NVN_QUEUE_INIT_DEVICE_WORD_COUNT];
    const uint32_t count =
        nvn_queue_init_device_words(dev, window_origin, depth_mode, words, NVN_QUEUE_INIT_DEVICE_WORD_COUNT);
    if (!count) {
        LOG("nvn-native: queue init device words: the table is empty\n");
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) gw32(a->device + words[i].offset, words[i].value);
    gw64(a->device + NVN_DEVICE_SHADER_OBJECT_OFFSET, a->shader_object);
    gw64(a->device + NVN_DEVICE_PROGRAM_OFFSET_OBJECT_OFFSET, a->program_offset);
    gw64(a->shader_object + NVN_SHADER_OBJECT_BASE_OFFSET, NVN_SHADER_REGION_BASE);

    builder_byte(a->builder + NVN_BUILDER_IMAGE_FLAGS_OFFSET, (uint8_t)b->flags);
    builder_byte(a->builder + NVN_BUILDER_IMAGE_COMPUTE_HAS_OFFSET, b->has_compute);
    gw32(a->builder + NVN_BUILDER_IMAGE_COMPUTE_OFFSET, (u32)b->compute);
    return 1;
}

/* Calls a library builder with x0..x2 and the stack top. The callee keeps x19..x29 and returns to the host LR. */
static int builder_call(CPU* c, u64 function, u64 x0, u64 x1, u64 x2, u64 sp) {
    if (function < g_code_lo || function >= g_code_hi || (function & 3u)) {
        LOG("nvn-native: queue builder function=%llx is outside [%llx, %llx)\n", (unsigned long long)function,
            (unsigned long long)g_code_lo, (unsigned long long)g_code_hi);
        return 0;
    }
    GuestFn fn = g_disp[(function - g_code_lo) / 4u];
    if (!fn) fn = g_rtapi.miss; /* an empty slot, as trace_proc uses */
    const u64 old_sp = c->x[31], old_lr = c->x[30];
    c->x[0] = x0;
    c->x[1] = x1;
    c->x[2] = x2;
    c->x[30] = NVN_BUILDER_RETURN_LR;
    c->x[31] = sp;
    fn(c, function);
    const int ok = c->pc == NVN_BUILDER_RETURN_LR;
    c->x[30] = old_lr;
    c->x[31] = old_sp;
    if (!ok)
        LOG("nvn-native: queue builder function=%llx returned pc=%llx\n", (unsigned long long)function,
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

/* The words that the builders wrote and the queue pool that the words point into. */
typedef struct {
    uint32_t* words;
    uint32_t word_count;
    uint64_t pool_storage, pool_size, pool_gpu;
} QueueInitBuild;

static int queue_pool_create(uint64_t size, uint64_t* storage, uint64_t* gpu);

/* Runs both library builders on the host arena. The caller holds builder_lock and no objs_lock. Returns 0 with a log
   line when a stage fails; the caller then drops the words. */
static int queue_init_build(CPU* c, const NvnQueueBuilder* b, u64 queue_addr, QueueInitBuild* out) {
    *out = (QueueInitBuild){0};
    BuilderArena arena;
    if (!builder_arena_get(&arena)) return 0;

    pm_lock(&objs_lock);
    const DeviceRec* device = known_record(b->device, NVN_OBJ_DEVICE);
    const uint32_t window_origin = device ? device->window_origin : 0;
    const uint32_t depth_mode = device ? device->depth_mode : 0;
    pm_unlock(&objs_lock);
    if (!device) {
        LOG("nvn-native: queue init: no device record for device=%llx\n", (unsigned long long)b->device);
        return 0;
    }

    if (!builder_images(&arena, b, &nvn_queue_device_words, window_origin, depth_mode)) return 0;
    if (!builder_call(c, NVN_QUEUE_POOL_BUILDER_ADDRESS, arena.queue + NVN_QUEUE_POOL_OBJECT_OFFSET, arena.device,
                      arena.builder, arena.stack_top))
        return 0;

    const uint64_t pool_size = gr32(arena.queue + NVN_QUEUE_POOL_SIZE_OFFSET);
    if (!pool_size || pool_size > NVN_QUEUE_POOL_MAX_BYTES) {
        LOG("nvn-native: queue pool size=%llu is not in [1, %u]\n", (unsigned long long)pool_size,
            NVN_QUEUE_POOL_MAX_BYTES);
        return 0;
    }

    uint64_t old_pool_size = 0;
    pm_lock(&objs_lock);
    const QueueRec* old = known_record(queue_addr, NVN_OBJ_QUEUE);
    const int reuse = old && old->pool_size >= pool_size;
    if (reuse) {
        out->pool_storage = old->pool_storage;
        out->pool_size = old->pool_size;
        out->pool_gpu = old->pool_gpu;
    } else if (old) {
        old_pool_size = old->pool_size;
    }
    pm_unlock(&objs_lock);
    if (!reuse) {
        if (old_pool_size)
            LOG("nvn-native: queue=%llx pool of %llu bytes is below the new size=%llu, a new pool replaces it\n",
                (unsigned long long)queue_addr, (unsigned long long)old_pool_size, (unsigned long long)pool_size);
        if (!queue_pool_create(pool_size, &out->pool_storage, &out->pool_gpu)) return 0;
        out->pool_size = pool_size;
    }

    gw64(arena.queue + NVN_QUEUE_POOL_OBJECT_OFFSET + NVN_QUEUE_POOL_GPU_OFFSET, out->pool_gpu);
    if (!builder_call(c, NVN_QUEUE_INIT_BUILDER_ADDRESS, arena.queue, 0, 0, arena.stack_top)) return 0;

    const u64 cursor = gr64(arena.queue + NVN_QUEUE_CURSOR_OFFSET);
    if (!builder_read_words(arena.command, cursor, &out->words, &out->word_count)) return 0;
    LOG("nvn-native: queue builder words=%u cursor=%llx\n", out->word_count, (unsigned long long)cursor);
    return 1;
}

/* The selftest's channels are fresh and have no macro code, but a command buffer's ClearColor is a macro call. This
   builds the queue init stream that a real channel runs first: the library builders on the host arena, as the frame
   job does. The selftest has no device record (nvn_native=0), so its device supplies the two words instead. */
int nvn_native_selftest_init_words(CPU* c, uint32_t window_origin, uint32_t depth_mode, uint32_t** words,
                                   uint32_t* count) {
    *words = NULL;
    *count = 0;
    int ok = 0;
    pm_lock(&builder_lock);
    BuilderArena arena;
    if (!builder_arena_get(&arena)) goto out;
    const NvnQueueBuilder builder = {.priority = NVN_QUEUE_PRIORITY_DEFAULT};
    if (!builder_images(&arena, &builder, &nvn_queue_device_words, window_origin, depth_mode)) goto out;
    if (!builder_call(c, NVN_QUEUE_POOL_BUILDER_ADDRESS, arena.queue + NVN_QUEUE_POOL_OBJECT_OFFSET, arena.device,
                      arena.builder, arena.stack_top))
        goto out;
    const uint64_t pool_size = gr32(arena.queue + NVN_QUEUE_POOL_SIZE_OFFSET);
    if (!pool_size || pool_size > NVN_QUEUE_POOL_MAX_BYTES) {
        LOG("nvn-native: selftest queue init: pool size=%llu is not in [1, %u]\n", (unsigned long long)pool_size,
            NVN_QUEUE_POOL_MAX_BYTES);
        goto out;
    }
    uint64_t storage = 0, gpu = 0;
    if (!queue_pool_create(pool_size, &storage, &gpu)) goto out;
    (void)storage; /* the selftest never submits work that reads the pool; it lives like a queue's pool does */
    gw64(arena.queue + NVN_QUEUE_POOL_OBJECT_OFFSET + NVN_QUEUE_POOL_GPU_OFFSET, gpu);
    if (!builder_call(c, NVN_QUEUE_INIT_BUILDER_ADDRESS, arena.queue, 0, 0, arena.stack_top)) goto out;
    if (!builder_read_words(arena.command, gr64(arena.queue + NVN_QUEUE_CURSOR_OFFSET), words, count)) goto out;
    if (!*count) {
        LOG("nvn-native: selftest queue init: the builders wrote no words\n");
        goto out;
    }
    LOG("nvn-native: selftest queue init words=%u\n", *count);
    ok = 1;
out:
    pm_unlock(&builder_lock);
    return ok;
}

/* Fails the syncs of the tokens of a frame that a new QueueInitialize drops: a dropped frame never posts, so a waiter
   must not stay PENDING (SyncWait then refuses with 3). A token names a sync only when its generation and its serial
   still match. Caller holds objs_lock. */
static void queue_drop_frame_syncs(u64 queue_addr, const NvnPendingFrame* frame) {
    for (uint32_t i = 0; i < frame->token_count; i++) {
        const NvnFrameToken* token = &frame->tokens[i];
        NvnObj* so = nvn_objtab_valid(&objs, token->sync, token->generation)
                         ? nvn_objtab_find(&objs, token->sync) : NULL;
        if (!so || so->kind != NVN_OBJ_SYNC) continue;
        SyncRec* sync = so->data;
        if (!nvn_frame_token_matches(token, so->generation, sync->serial)) continue;
        sync->state = NVN_SYNC_UNSIGNALED;
        LOG("nvn-native: queue=%llx drops the fence of sync=%llx, the queue is initialized again\n",
            (unsigned long long)queue_addr, (unsigned long long)token->sync);
    }
}
/* nvnQueueInitialize(queue, builder): runs the library builders, records the builder and opens the private channel
   of the frame jobs. A failed builder stage leaves the macro code empty. */
static void h_queue_initialize(CPU* c, u64 entry) {
    (void)entry;
    const u64 queue_addr = c->x[0]; /* the builder calls may change x0 */
    pm_lock(&objs_lock);
    NvnQueueBuilder b = *queue_builder(c->x[1]);
    pm_unlock(&objs_lock);
    QueueInitBuild build;
    pm_lock(&builder_lock);
    const int built = queue_init_build(c, &b, queue_addr, &build); /* runs guest code, so no objs_lock */
    pm_unlock(&builder_lock);
    pm_lock(&objs_lock);
    QueueRec* old = known_record(queue_addr, NVN_OBJ_QUEUE);
    uint32_t channel_id = 0;
    GpuChannel* channel = NULL;
    if (old) { /* initialized again without Finalize */
        queue_drop_frame_syncs(queue_addr, &old->pending);
        nvn_frame_release(&old->pending, &lists);
        pc_broadcast(&frame_done_cv); /* a waiter of a dropped fence must see its state */
        channel_id = old->channel_id;
        channel = old->channel;
        free(old->init_words);
        if (!built) { /* a failed rebuild keeps the pool of the recorded init */
            build.pool_storage = old->pool_storage;
            build.pool_size = old->pool_size;
            build.pool_gpu = old->pool_gpu;
        }
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
    q->init_words = build.words;
    q->init_word_count = build.word_count;
    q->pool_storage = build.pool_storage;
    q->pool_size = build.pool_size;
    q->pool_gpu = build.pool_gpu;
    pm_unlock(&objs_lock);
    c->x[0] = 1;
}
/* Caller holds objs_lock. */
static QueueRec* queue_rec(u64 addr, const char* operation) {
    QueueRec* q = known_record(addr, NVN_OBJ_QUEUE);
    if (!q) LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown queue=%llx\n", operation, (unsigned long long)addr);
    return q;
}
/* The device of the queue record, for the execution test (nvn_exectest.c). 0 when the queue is unknown. */
uint64_t nvn_native_queue_device(uint64_t queue) {
    pm_lock(&objs_lock);
    const QueueRec* q = known_record(queue, NVN_OBJ_QUEUE);
    const uint64_t device = q ? q->builder.device : 0;
    pm_unlock(&objs_lock);
    return device;
}

/* G2 and G5: pool state stays in host records. A pool maps its storage at its GPU address from Initialize to
   Finalize (nvn_gpu_map_range), so the renderer reads the pool by GPU address. */
typedef struct {
    uint64_t device, storage, size, gpu_address;
    /* The block linear range of a normal pool: a second range over the same storage. 0 for a virtual pool. */
    uint64_t block_linear_address;
    uint32_t flags;
    /* A virtual pool owns one entry for each 64 KiB page: the CPU address that the page maps, 0 unmapped.
       A normal pool has no table (page_cpu NULL, page_count 0). */
    uint64_t* page_cpu;
    uint32_t page_count;
} MemoryPoolRec;
typedef struct {
    uint64_t memory_pool, offset;
    uint32_t count;
} DescriptorPoolRec;

/* Makes a GPU-visible queue pool of size bytes and maps its storage at its GPU address. The storage and its GPU
   range are never freed: QueueFinalize has no handler. Returns 0 after a log line. */
static int queue_pool_create(uint64_t size, uint64_t* storage, uint64_t* gpu) {
    const uint64_t bytes = (size + NVN_POOL_PAGE_SIZE - 1u) & ~(uint64_t)(NVN_POOL_PAGE_SIZE - 1u);
    pm_lock(&objs_lock);
    const uint64_t cpu = kmem_alloc(bytes, MS_STATIC, PERM_RW);
    const uint64_t address = nvn_gpu_va_alloc(bytes, NVN_POOL_PAGE_SIZE);
    const int ok = cpu && address && nvn_gpu_map_range(address, cpu, bytes);
    if (!ok && address) nvn_gpu_va_free(address, bytes);
    pm_unlock(&objs_lock);
    if (!ok) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: queue pool of %llu bytes cannot be allocated and mapped\n",
                  (unsigned long long)bytes);
        return 0;
    }
    *storage = cpu;
    *gpu = address;
    return 1;
}

static NvnMemoryPoolBuilder* memory_pool_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL_BUILDER, sizeof(NvnMemoryPoolBuilder));
}
static MemoryPoolRec* memory_pool_rec(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL, sizeof(MemoryPoolRec));
}
/* The GPU bytes of a pool: one range, or two for a normal pool (the pitch and block linear ranges). */
static uint64_t memory_pool_va_size(uint32_t flags, uint64_t size) {
    return (flags & NVN_POOL_VIRTUAL) ? size : size * 2u;
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
static void h_memory_pool_builder_get_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = memory_pool_builder(c->x[0])->device;
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_get_memory(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = memory_pool_builder(c->x[0])->storage;
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_get_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = memory_pool_builder(c->x[0])->size;
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_get_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = memory_pool_builder(c->x[0])->flags;
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
    if (!(flags & NVN_POOL_VIRTUAL) && b.size > UINT64_MAX / 2u) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx size=%llx overflows two GPU ranges\n",
            (unsigned long long)c->x[0], (unsigned long long)b.size);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    /* MapVirtual maps whole 64 KiB pages, so a virtual range starts on one. */
    const uint64_t gpu_page = (flags & NVN_POOL_VIRTUAL) ? NVN_POOL_VIRTUAL_PAGE_SIZE : NVN_POOL_PAGE_SIZE;
    const uint64_t va_size = memory_pool_va_size(flags, b.size);
    const uint64_t address = nvn_gpu_va_alloc(va_size, gpu_page);
    if (!address) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx size=%llx GPU address allocation failed\n",
            (unsigned long long)c->x[0], (unsigned long long)b.size);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    MemoryPoolRec* p = o->data;
    if (flags & NVN_POOL_VIRTUAL) {
        /* A virtual pool reserves the range and maps nothing: nvnMemoryPoolMapVirtual fills the pages. */
        const uint32_t pages = (uint32_t)(b.size / NVN_POOL_VIRTUAL_PAGE_SIZE);
        uint64_t* table = calloc(pages ? pages : 1, sizeof *table);
        if (!table) {
            LOG("nvn-native: MemoryPoolInitialize pool=%llx virtual page table of %u pages cannot be allocated\n",
                (unsigned long long)c->x[0], pages);
            nvn_gpu_va_free(address, va_size);
            nvn_objtab_remove(&objs, c->x[0]);
            c->x[0] = 0;
            pm_unlock(&objs_lock);
            return;
        }
        *p = (MemoryPoolRec){.device = b.device, .storage = 0, .size = b.size,
                             .gpu_address = address, .flags = flags,
                             .page_cpu = table, .page_count = pages};
        c->x[0] = 1;
        pm_unlock(&objs_lock);
        return;
    }
    /* A normal pool maps its storage at two ranges, the pitch range and the block linear range. The library maps a
       third range for pool kinds with bit 7; the host does not. */
    const uint64_t block_linear = address + b.size;
    if (!nvn_gpu_map_range(address, b.storage, b.size)) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx GPU range %llx size=%llx storage=%llx cannot be mapped\n",
            (unsigned long long)c->x[0], (unsigned long long)address, (unsigned long long)b.size,
            (unsigned long long)b.storage);
        nvn_gpu_va_free(address, va_size);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    if (!nvn_gpu_map_range(block_linear, b.storage, b.size)) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx block linear range %llx size=%llx storage=%llx cannot be mapped\n",
            (unsigned long long)c->x[0], (unsigned long long)block_linear, (unsigned long long)b.size,
            (unsigned long long)b.storage);
        nvn_gpu_unmap_range(address, b.storage, b.size);
        nvn_gpu_va_free(address, va_size);
        nvn_objtab_remove(&objs, c->x[0]);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    *p = (MemoryPoolRec){.device = b.device, .storage = b.storage, .size = b.size,
                         .gpu_address = address, .block_linear_address = block_linear, .flags = flags};
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}

static void h_memory_pool_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    if (o && o->kind == NVN_OBJ_MEMORY_POOL) {
        MemoryPoolRec* p = o->data;
        if (p->page_cpu) {
            /* A physical pool finalized while a virtual pool maps it stays mapped here, as the library does not check it. */
            for (uint32_t i = 0; i < p->page_count; i++)
                if (p->page_cpu[i])
                    nvn_gpu_unmap_range(p->gpu_address + (u64)i * NVN_POOL_VIRTUAL_PAGE_SIZE, p->page_cpu[i],
                                        NVN_POOL_VIRTUAL_PAGE_SIZE);
            free(p->page_cpu);
        } else {
            nvn_gpu_unmap_range(p->gpu_address, p->storage, p->size);
            nvn_gpu_unmap_range(p->block_linear_address, p->storage, p->size);
        }
        nvn_gpu_va_free(p->gpu_address, memory_pool_va_size(p->flags, p->size));
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

static void h_memory_pool_get_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = memory_pool_rec(c->x[0])->size;
    pm_unlock(&objs_lock);
}

/* U3: nvnMemoryPoolMapVirtual. A virtual pool maps 64 KiB pages of a physical pool, or unmaps them when the request
   names no physical pool. The handler owns the page table that finalize clears. */
#define POOL_MAP_VIRTUAL_LOG_LIMIT 32u /* log lines for the faults of nvnMemoryPoolMapVirtual */

/* Caller holds objs_lock. Applies one checked request to the page table and the GPU page table. */
static int apply_virtual_range(MemoryPoolRec* pool, const NvnMapVirtualRange* range) {
    static const char* const name = "nvnMemoryPoolMapVirtual";
    for (uint32_t i = 0; i < range->pages; i++) {
        const uint64_t offset = (uint64_t)i * NVN_POOL_VIRTUAL_PAGE_SIZE;
        const uint64_t gva = range->virtual_base + offset;
        uint64_t* slot = &pool->page_cpu[(gva - pool->gpu_address) / NVN_POOL_VIRTUAL_PAGE_SIZE];
        if (*slot) {
            nvn_gpu_unmap_range(gva, *slot, NVN_POOL_VIRTUAL_PAGE_SIZE);
            *slot = 0;
        }
        if (range->physical_base) {
            const uint64_t cpu = range->physical_base + offset;
            if (!nvn_gpu_map_range(gva, cpu, NVN_POOL_VIRTUAL_PAGE_SIZE)) {
                LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT, "nvn-native: %s gva=%llx cpu=%llx cannot be mapped\n",
                          name, (unsigned long long)gva, (unsigned long long)cpu);
                return 0;
            }
            *slot = cpu;
        }
    }
    return 1;
}

/* Caller holds objs_lock and confirmed that requests[0, batch) is readable. Checks the whole batch before it changes a
   page, so a bad request leaves the batch unapplied; batches before it stay. */
static int map_virtual_batch(MemoryPoolRec* pool, u64 pool_addr, u64 requests, u32 batch, u32 first_index) {
    static const char* const name = "nvnMemoryPoolMapVirtual";
    NvnMapVirtualRange ranges[NVN_POOL_MAP_VIRTUAL_BATCH];
    for (u32 i = 0; i < batch; i++) {
        const u64 at = requests + (u64)i * NVN_POOL_MAP_VIRTUAL_REQUEST_BYTES;
        const NvnMapVirtualRequest request = {
            gr64(at), gr64(at + 8), gr64(at + 0x10), gr64(at + 0x18), gr32(at + 0x20)
        };
        const NvnMapVirtualResult result =
            nvn_memory_pool_map_virtual_range(&request, pool->gpu_address, pool->size, &ranges[i]);
        if (result == NVN_MAP_VIRTUAL_SKIP) continue;
        if (result != NVN_MAP_VIRTUAL_OK) {
            LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT,
                      "nvn-native: %s pool=%llx request=%u physical=%llx offset=%llx virtual=%llx size=%llx "
                      "result=%u\n",
                      name, (unsigned long long)pool_addr, first_index + i, (unsigned long long)request.physical_pool,
                      (unsigned long long)request.physical_offset, (unsigned long long)request.virtual_offset,
                      (unsigned long long)request.size, (unsigned)result);
            return 0;
        }
        if (!ranges[i].physical_pool) continue;
        const MemoryPoolRec* physical = known_record(ranges[i].physical_pool, NVN_OBJ_MEMORY_POOL);
        if (!physical || (physical->flags & NVN_POOL_VIRTUAL)) {
            LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT,
                      "nvn-native: %s pool=%llx request=%u physical=%llx is not a physical pool\n",
                      name, (unsigned long long)pool_addr, first_index + i, (unsigned long long)ranges[i].physical_pool);
            return 0;
        }
        if (!nvn_pool_range_fits(physical->size, ranges[i].physical_offset, ranges[i].size)) {
            LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT,
                      "nvn-native: %s pool=%llx request=%u physical=%llx offset=%llx size=%llx does not fit its "
                      "pool of %llx bytes\n",
                      name, (unsigned long long)pool_addr, first_index + i, (unsigned long long)ranges[i].physical_pool,
                      (unsigned long long)ranges[i].physical_offset, (unsigned long long)ranges[i].size,
                      (unsigned long long)physical->size);
            return 0;
        }
        ranges[i].physical_base = physical->storage + ranges[i].physical_offset;
    }
    for (u32 i = 0; i < batch; i++)
        if (ranges[i].pages && !apply_virtual_range(pool, &ranges[i])) return 0;
    return 1;
}

static void h_memory_pool_map_virtual(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnMemoryPoolMapVirtual";
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    MemoryPoolRec* pool = o && o->kind == NVN_OBJ_MEMORY_POOL ? o->data : NULL;
    const u32 count = (u32)c->x[1];
    const u64 requests = c->x[2];
    int ok = 1;
    /* A count of 0 gives true for a virtual pool, as the library's loop does. A normal pool returns false. */
    if (!pool || !(pool->flags & NVN_POOL_VIRTUAL)) {
        LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT, "nvn-native: %s pool=%llx is not a virtual pool\n",
                  name, (unsigned long long)c->x[0]);
        ok = 0;
    }
    for (u32 done = 0; ok && done < count;) {
        const u32 batch = nvn_pool_map_virtual_batch(count, done);
        const u64 at = requests + (u64)done * NVN_POOL_MAP_VIRTUAL_REQUEST_BYTES;
        if (!native_guest_readable(at, (u64)batch * NVN_POOL_MAP_VIRTUAL_REQUEST_BYTES)) {
            LOG_LIMIT(POOL_MAP_VIRTUAL_LOG_LIMIT,
                      "nvn-native: %s pool=%llx requests=%llx count=%u are unreadable\n",
                      name, (unsigned long long)c->x[0], (unsigned long long)at, batch);
            ok = 0;
            break;
        }
        ok = map_virtual_batch(pool, c->x[0], at, batch, done);
        done += batch;
    }
    c->x[0] = (u64)ok;
    pm_unlock(&objs_lock);
}

/* The event builder (pool, offset). A builder that the game never gave to SetDefaults starts zero, as calloc gives. */
typedef struct { uint64_t pool, offset; } EventBuilderRec;
/* An event holds the pool and the offset that its builder had at EventInitialize. Its value word lives in the pool. */
typedef struct { uint64_t pool, offset; } EventRec;
static EventBuilderRec* event_builder(u64 addr) { return rec_get(addr, NVN_OBJ_EVENT_BUILDER, sizeof(EventBuilderRec)); }
static void h_event_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    *event_builder(c->x[0]) = (EventBuilderRec){0};
    pm_unlock(&objs_lock);
}
static void h_event_builder_set_storage(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    EventBuilderRec* b = event_builder(c->x[0]);
    b->pool = c->x[1];
    b->offset = c->x[2];
    pm_unlock(&objs_lock);
}
static void h_event_builder_get_memory_pool(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = event_builder(c->x[0])->pool;
    pm_unlock(&objs_lock);
}
static void h_event_builder_get_memory_offset(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = event_builder(c->x[0])->offset;
    pm_unlock(&objs_lock);
}
static void h_event_builder_get_storage(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnEventBuilderGetStorage";
    pm_lock(&objs_lock);
    const uint64_t addr = c->x[0];
    const EventBuilderRec* b = event_builder(addr);
    c->x[0] = 0;
    if (!native_guest_writable(c->x[1], sizeof(uint64_t))) {
        LOG("nvn-native: %s unwritable output\n", name);
    } else {
        gw64(c->x[1], b->offset);
        c->x[0] = b->pool;
    }
    pm_unlock(&objs_lock);
}

/* The value word of an event is the u32 at MemoryPoolRec.storage + offset. The library does not check it; the host
   refuses a word that it cannot write or read instead of a fault. Caller holds objs_lock. Returns 0 after a log line. */
static u64 event_value_address(const EventRec* e, const char* operation, u64 event_addr) {
    const MemoryPoolRec* pool = known_record(e->pool, NVN_OBJ_MEMORY_POOL);
    if (!pool) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s event=%llx memory pool=%llx is unknown\n", operation,
                  (unsigned long long)event_addr, (unsigned long long)e->pool);
        return 0;
    }
    if (!nvn_memory_pool_map(pool->flags, pool->storage)) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s event=%llx memory pool=%llx has no CPU access\n", operation,
                  (unsigned long long)event_addr, (unsigned long long)e->pool);
        return 0;
    }
    if (!nvn_pool_range_fits(pool->size, e->offset, sizeof(uint32_t))) {
        LOG_LIMIT(QUEUE_LOG_LIMIT,
                  "nvn-native: %s event=%llx offset=%llx size=4 is outside memory pool=%llx size=%llu\n",
                  operation, (unsigned long long)event_addr, (unsigned long long)e->offset,
                  (unsigned long long)e->pool, (unsigned long long)pool->size);
        return 0;
    }
    return pool->storage + e->offset;
}
static EventRec* event_rec(u64 addr) { return rec_get(addr, NVN_OBJ_EVENT, sizeof(EventRec)); }

static void h_event_initialize(CPU* c, u64 entry) {
    (void)entry;
    const u64 event_addr = c->x[0], builder_addr = c->x[1];
    pm_lock(&objs_lock);
    const EventBuilderRec* b = known_record(builder_addr, NVN_OBJ_EVENT_BUILDER);
    if (!b) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: nvnEventInitialize event=%llx builder=%llx is unknown\n",
                  (unsigned long long)event_addr, (unsigned long long)builder_addr);
        c->x[0] = 0;
    } else {
        *event_rec(event_addr) = (EventRec){b->pool, b->offset};
        c->x[0] = 1;
    }
    pm_unlock(&objs_lock);
}
static void h_event_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (known_record(c->x[0], NVN_OBJ_EVENT)) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
static void h_event_get_value(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnEventGetValue";
    const u64 event_addr = c->x[0];
    pm_lock(&objs_lock);
    const EventRec* e = known_record(event_addr, NVN_OBJ_EVENT);
    c->x[0] = 0;
    if (!e)
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown event=%llx\n", name, (unsigned long long)event_addr);
    else {
        const u64 word = event_value_address(e, name, event_addr);
        if (word) c->x[0] = gr32(word);
    }
    pm_unlock(&objs_lock);
}
static void h_event_get_memory_pool(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const EventRec* e = known_record(c->x[0], NVN_OBJ_EVENT);
    if (!e) LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: nvnEventGetMemoryPool unknown event=%llx\n",
                      (unsigned long long)c->x[0]);
    c->x[0] = e ? e->pool : 0;
    pm_unlock(&objs_lock);
}
static void h_event_get_memory_offset(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const EventRec* e = known_record(c->x[0], NVN_OBJ_EVENT);
    if (!e) LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: nvnEventGetMemoryOffset unknown event=%llx\n",
                      (unsigned long long)c->x[0]);
    c->x[0] = e ? e->offset : 0;
    pm_unlock(&objs_lock);
}
static void h_event_signal(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnEventSignal";
    const u64 event_addr = c->x[0];
    const uint32_t mode = (uint32_t)c->x[1];
    pm_lock(&objs_lock);
    const EventRec* e = known_record(event_addr, NVN_OBJ_EVENT);
    const u64 word = e ? event_value_address(e, name, event_addr) : 0;
    if (!e)
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown event=%llx\n", name, (unsigned long long)event_addr);
    else if (word) {
        const uint32_t old = mode == NVN_EVENT_SIGNAL_ADD ? gr32(word) : 0;
        gw32(word, nvn_event_signal_value(mode, old, (uint32_t)c->x[2]));
    }
    pm_unlock(&objs_lock);
}
/* The library flushes or invalidates the CPU data cache of the range. The host renderer reads the same host memory, so
   this is a no-op: there is no guest cache to clean. */
static void host_cache_range_noop(const char* name) {
    LOG_LIMIT(1, "nvn-native: %s is a no-op (the renderer reads the same host memory)\n", name);
}
static void h_buffer_flush_mapped_range(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
    host_cache_range_noop("nvnBufferFlushMappedRange");
}
static void h_buffer_invalidate_mapped_range(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
    host_cache_range_noop("nvnBufferInvalidateMappedRange");
}
static void h_memory_pool_flush_mapped_range(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
    host_cache_range_noop("nvnMemoryPoolFlushMappedRange");
}
static void h_memory_pool_invalidate_mapped_range(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
    host_cache_range_noop("nvnMemoryPoolInvalidateMappedRange");
}

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

#define DESCRIPTOR_POOL_MEMORY_HANDLERS(name, kind)                                 \
    static void h_##name##_pool_get_memory_pool(CPU* c, u64 entry) {                \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const DescriptorPoolRec* p = known_record(c->x[0], kind);                   \
        if (!p) LOG("nvn-native: " #name " pool getter unknown pool=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = p ? p->memory_pool : 0;                                          \
        pm_unlock(&objs_lock);                                                     \
    }                                                                              \
    static void h_##name##_pool_get_memory_offset(CPU* c, u64 entry) {              \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const DescriptorPoolRec* p = known_record(c->x[0], kind);                   \
        if (!p) LOG("nvn-native: " #name " pool getter unknown pool=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = p ? p->offset : 0;                                               \
        pm_unlock(&objs_lock);                                                     \
    }
DESCRIPTOR_POOL_MEMORY_HANDLERS(texture, NVN_OBJ_TEXTURE_POOL)
DESCRIPTOR_POOL_MEMORY_HANDLERS(sampler, NVN_OBJ_SAMPLER_POOL)
#undef DESCRIPTOR_POOL_MEMORY_HANDLERS

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
static void h_buffer_builder_get_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = buffer_builder(c->x[0])->device;
    pm_unlock(&objs_lock);
}
static void h_buffer_builder_get_memory_pool(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = buffer_builder(c->x[0])->pool;
    pm_unlock(&objs_lock);
}
static void h_buffer_builder_get_memory_offset(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = buffer_builder(c->x[0])->offset;
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
/* The library's function is one `ret`: the object stays readable, so the host keeps its record (U1 decision 2). */
static void h_buffer_finalize(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
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
static void h_buffer_get_memory_pool(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const BufferRec* b = known_record(c->x[0], NVN_OBJ_BUFFER);
    if (!b) buffer_lost("BufferGetMemoryPool", c->x[0]);
    c->x[0] = b ? b->pool : 0;
    pm_unlock(&objs_lock);
}
static void h_buffer_get_memory_offset(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const BufferRec* b = known_record(c->x[0], NVN_OBJ_BUFFER);
    if (!b) buffer_lost("BufferGetMemoryOffset", c->x[0]);
    c->x[0] = b ? b->offset : 0;
    pm_unlock(&objs_lock);
}
static void h_buffer_get_debug_id(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = 0; /* the library body is `mov x0, xzr; ret` */
}
static void h_buffer_set_debug_label(CPU* c, u64 entry) {
    (void)c;
    (void)entry; /* the library stores no label */
}
/* The library functions whose body is one `ret`, which leaves x0 and the object words as they are. */
static void h_library_ret(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}

/* U5: debug, checkpoint and fast clear functions of the device. The library body of a checkpoint is `mov w0, #1; ret`:
   the debugger part is absent. The ZBC table that a fast clear fills is hardware state, and the host clear reads its
   value argument, so a registered color is a logged no-op. */
static void h_debug_checkpoint(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = 1;
}
/* The library's domain id is one process-global counter that all devices share (0xe0f1438). */
static uint32_t debug_domain_id;
static void h_device_generate_debug_domain_id(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = __atomic_fetch_add(&debug_domain_id, 1, __ATOMIC_RELAXED);
}
static void h_device_is_external_debugger_attached(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = 0; /* the library body is `mov w0, wzr`, as in the release */
}
/* The library converts the GPU timer of the device; the host maps it to the same 614.4 MHz guest clock. */
static void h_device_get_current_timestamp_ns(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_timestamp_ns(gpu_ticks());
}
#define FAST_CLEAR_NO_OP_REASON "the ZBC table of the library is hardware state, and the host clear reads its value argument"
/* RegisterFastClearColor takes the format in w2. The library accepts 10..156 for the float form and 11..120 for the
   integer forms, and returns 0 outside that range without a driver call. The depth form has no format check. */
static void fast_clear_color(CPU* c, const char* operation, NvnFastClearKind kind) {
    const uint32_t format = (uint32_t)c->x[2];
    if (!nvn_fast_clear_color_format_ok(kind, format)) {
        LOG_LIMIT(1, "nvn-native: %s format=%u is outside the library range, returns 0\n", operation, format);
        c->x[0] = 0;
        return;
    }
    LOG_LIMIT(1, "nvn-native: %s is a logged no-op: %s\n", operation, FAST_CLEAR_NO_OP_REASON);
    c->x[0] = 1;
}
static void h_device_register_fast_clear_color(CPU* c, u64 entry) {
    (void)entry;
    fast_clear_color(c, "nvnDeviceRegisterFastClearColor", NVN_FAST_CLEAR_KIND_FLOAT);
}
static void h_device_register_fast_clear_colori(CPU* c, u64 entry) {
    (void)entry;
    fast_clear_color(c, "nvnDeviceRegisterFastClearColori", NVN_FAST_CLEAR_KIND_INT);
}
static void h_device_register_fast_clear_colorui(CPU* c, u64 entry) {
    (void)entry;
    fast_clear_color(c, "nvnDeviceRegisterFastClearColorui", NVN_FAST_CLEAR_KIND_INT);
}
static void h_device_register_fast_clear_depth(CPU* c, u64 entry) {
    (void)entry;
    LOG_LIMIT(1, "nvn-native: nvnDeviceRegisterFastClearDepth is a logged no-op: %s\n", FAST_CLEAR_NO_OP_REASON);
    c->x[0] = 1;
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
/* A sparse tile layout goes to CPU memory of the caller, not to the GPU address space (gmem_write). */
static int native_write_tile_layout(u64 dst, const uint32_t out[4]) {
    if (!native_guest_writable(dst, 4 * sizeof(uint32_t))) return 0;
    for (unsigned i = 0; i < 4; i++) gw32(dst + 4 * i, out[i]);
    return 1;
}
static float native_float_arg(const CPU* c, unsigned index) {
    uint32_t bits = SNX_VS(c->v[index], 0);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
static uint32_t native_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
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

RESOURCE_BUILDER_HANDLER(sampler_builder_get_device, NvnSampler, sampler_builder,
    c->x[0] = b->device)
RESOURCE_BUILDER_HANDLER(sampler_builder_get_lod_bias, NvnSampler, sampler_builder,
    SNX_VSET_S(c->v[0], 0, native_float_bits(b->lod_bias)))
RESOURCE_BUILDER_HANDLER(sampler_builder_get_max_anisotropy, NvnSampler, sampler_builder,
    SNX_VSET_S(c->v[0], 0, native_float_bits(b->anisotropy)))
RESOURCE_BUILDER_HANDLER(sampler_builder_get_reduction_filter, NvnSampler, sampler_builder,
    c->x[0] = b->reduction)
RESOURCE_BUILDER_HANDLER(sampler_builder_get_lod_snap, NvnSampler, sampler_builder,
    SNX_VSET_S(c->v[0], 0, native_float_bits(b->lod_snap)))

RESOURCE_BUILDER_HANDLER(texture_builder_get_depth, NvnTextureBuilder, texture_builder,
    c->x[0] = b->depth)
RESOURCE_BUILDER_HANDLER(texture_builder_get_depth_stencil_mode, NvnTextureBuilder, texture_builder,
    c->x[0] = b->depth_stencil_mode)
RESOURCE_BUILDER_HANDLER(texture_builder_get_device, NvnTextureBuilder, texture_builder,
    c->x[0] = b->device)
RESOURCE_BUILDER_HANDLER(texture_builder_get_flags, NvnTextureBuilder, texture_builder,
    c->x[0] = b->flags)
RESOURCE_BUILDER_HANDLER(texture_builder_get_format, NvnTextureBuilder, texture_builder,
    c->x[0] = b->format)
RESOURCE_BUILDER_HANDLER(texture_builder_get_levels, NvnTextureBuilder, texture_builder,
    c->x[0] = b->levels)
RESOURCE_BUILDER_HANDLER(texture_builder_get_samples, NvnTextureBuilder, texture_builder,
    c->x[0] = b->samples)
RESOURCE_BUILDER_HANDLER(texture_builder_get_target, NvnTextureBuilder, texture_builder,
    c->x[0] = b->target)
RESOURCE_BUILDER_HANDLER(texture_builder_get_stride, NvnTextureBuilder, texture_builder,
    c->x[0] = b->stride)
RESOURCE_BUILDER_HANDLER(texture_builder_get_memory_offset, NvnTextureBuilder, texture_builder,
    c->x[0] = b->offset)
RESOURCE_BUILDER_HANDLER(texture_builder_get_memory_pool, NvnTextureBuilder, texture_builder,
    c->x[0] = b->pool)
RESOURCE_BUILDER_HANDLER(texture_builder_get_gl_texture_name, NvnTextureBuilder, texture_builder,
    c->x[0] = b->gl_name)
RESOURCE_BUILDER_HANDLER(texture_builder_get_packaged_texture_data, NvnTextureBuilder, texture_builder,
    c->x[0] = b->packaged)
RESOURCE_BUILDER_HANDLER(texture_builder_set_gl_texture_name, NvnTextureBuilder, texture_builder,
    b->gl_name = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_packaged_texture_data, NvnTextureBuilder, texture_builder,
    b->packaged = c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_samples, NvnTextureBuilder, texture_builder,
    b->samples = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_size_1d, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1]; b->height = 1; b->depth = 1)

#undef RESOURCE_BUILDER_HANDLER

/* 1 when the 32-bit outputs x1.. of a getter are writable, checked before the first store. Caller holds objs_lock. */
static int guest_outputs_writable(CPU* c, const char* name, unsigned outputs) {
    for (unsigned i = 0; i < outputs; i++)
        if (!native_guest_writable(c->x[1 + i], sizeof(uint32_t))) {
            LOG("nvn-native: %s unwritable output\n", name);
            return 0;
        }
    return 1;
}
/* A builder record is made with its defaults; an initialized sampler must be known. */
static const NvnSampler* sampler_getter_record(CPU* c, const char* name, NvnObjKind kind, unsigned outputs) {
    if (!guest_outputs_writable(c, name, outputs)) return NULL;
    const NvnSampler* s = kind == NVN_OBJ_SAMPLER_BUILDER ? sampler_builder(c->x[0]) : known_record(c->x[0], kind);
    if (!s) LOG("nvn-native: %s unknown sampler=%llx\n", name, (unsigned long long)c->x[0]);
    return s;
}
#define SAMPLER_OUT2(name, kind, first, second)                                     \
    static void h_##name(CPU* c, u64 entry) {                                       \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const NvnSampler* b = sampler_getter_record(c, #name, kind, 2);            \
        if (b) { gw32(c->x[1], (u32)(first)); gw32(c->x[2], (u32)(second)); }       \
        pm_unlock(&objs_lock);                                                     \
    }
#define SAMPLER_OUT3(name, kind, first, second, third)                              \
    static void h_##name(CPU* c, u64 entry) {                                       \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const NvnSampler* b = sampler_getter_record(c, #name, kind, 3);            \
        if (b) { gw32(c->x[1], (u32)(first)); gw32(c->x[2], (u32)(second));         \
                 gw32(c->x[3], (u32)(third)); }                                    \
        pm_unlock(&objs_lock);                                                     \
    }
SAMPLER_OUT2(sampler_builder_get_min_mag_filter, NVN_OBJ_SAMPLER_BUILDER, b->min_filter, b->mag_filter)
SAMPLER_OUT2(sampler_builder_get_lod_clamp, NVN_OBJ_SAMPLER_BUILDER,
             native_float_bits(b->min_lod), native_float_bits(b->max_lod))
SAMPLER_OUT2(sampler_builder_get_compare, NVN_OBJ_SAMPLER_BUILDER, b->compare_enable, b->compare)
SAMPLER_OUT3(sampler_builder_get_wrap_mode, NVN_OBJ_SAMPLER_BUILDER, b->wrap[0], b->wrap[1], b->wrap[2])
SAMPLER_OUT2(sampler_get_min_mag_filter, NVN_OBJ_SAMPLER, b->min_filter, b->mag_filter)
SAMPLER_OUT2(sampler_get_lod_clamp, NVN_OBJ_SAMPLER, native_float_bits(b->min_lod), native_float_bits(b->max_lod))
SAMPLER_OUT2(sampler_get_compare, NVN_OBJ_SAMPLER, b->compare_enable, b->compare)
SAMPLER_OUT3(sampler_get_wrap_mode, NVN_OBJ_SAMPLER, b->wrap[0], b->wrap[1], b->wrap[2])
#undef SAMPLER_OUT2
#undef SAMPLER_OUT3

static void sampler_get_border_color(CPU* c, const char* name, NvnObjKind kind) {
    pm_lock(&objs_lock);
    if (!native_guest_writable(c->x[1], 4 * sizeof(uint32_t))) {
        LOG("nvn-native: %s unwritable output\n", name);
        pm_unlock(&objs_lock);
        return;
    }
    const NvnSampler* s = kind == NVN_OBJ_SAMPLER_BUILDER ? sampler_builder(c->x[0]) : known_record(c->x[0], kind);
    if (!s) {
        LOG("nvn-native: %s unknown sampler=%llx\n", name, (unsigned long long)c->x[0]);
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t out[4];
    nvn_sampler_border(s, out);
    for (unsigned i = 0; i < 4; i++) gw32(c->x[1] + i * sizeof(uint32_t), out[i]);
    pm_unlock(&objs_lock);
}
static void h_sampler_builder_get_border_color(CPU* c, u64 entry) {
    (void)entry;
    sampler_get_border_color(c, "nvnSamplerBuilderGetBorderColor", NVN_OBJ_SAMPLER_BUILDER);
}
static void h_sampler_get_border_color(CPU* c, u64 entry) {
    (void)entry;
    sampler_get_border_color(c, "nvnSamplerGetBorderColor", NVN_OBJ_SAMPLER);
}
#define SAMPLER_OBJECT_GET_SCALAR(name, expression)                                 \
    static void h_##name(CPU* c, u64 entry) {                                       \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const NvnSampler* s = known_record(c->x[0], NVN_OBJ_SAMPLER);              \
        if (!s) LOG("nvn-native: " #name " unknown sampler=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = s ? (expression) : 0;                                            \
        pm_unlock(&objs_lock);                                                     \
    }
#define SAMPLER_OBJECT_GET_FLOAT(name, field)                                       \
    static void h_##name(CPU* c, u64 entry) {                                       \
        (void)entry;                                                               \
        pm_lock(&objs_lock);                                                       \
        const NvnSampler* s = known_record(c->x[0], NVN_OBJ_SAMPLER);              \
        if (!s) LOG("nvn-native: " #name " unknown sampler=%llx\n", (unsigned long long)c->x[0]); \
        SNX_VSET_S(c->v[0], 0, s ? native_float_bits(s->field) : 0);               \
        pm_unlock(&objs_lock);                                                     \
    }
SAMPLER_OBJECT_GET_SCALAR(sampler_get_reduction_filter, s->reduction)
SAMPLER_OBJECT_GET_FLOAT(sampler_get_lod_bias, lod_bias)
SAMPLER_OBJECT_GET_FLOAT(sampler_get_max_anisotropy, anisotropy)
#undef SAMPLER_OBJECT_GET_SCALAR
#undef SAMPLER_OBJECT_GET_FLOAT

static void h_sampler_get_debug_id(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = 0; /* the library body is `mov x0, xzr; ret` */
}

/* The texture builder getters with an output pointer and the two derived values. */
static void h_texture_builder_get_swizzle(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureBuilderGetSwizzle";
    pm_lock(&objs_lock);
    if (guest_outputs_writable(c, name, 4)) {
        const NvnTextureBuilder* b = texture_builder(c->x[0]);
        for (unsigned i = 0; i < 4; i++) gw32(c->x[1 + i], b->swizzle[i]);
    }
    pm_unlock(&objs_lock);
}
static void h_texture_builder_get_raw_storage_class(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const NvnTextureBuilder* b = texture_builder(c->x[0]);
    c->x[0] = nvn_texture_raw_storage_class(b->flags, b->format);
    pm_unlock(&objs_lock);
}
static void h_texture_builder_get_packaged_texture_layout(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureBuilderGetPackagedTextureLayout";
    pm_lock(&objs_lock);
    if (native_guest_writable(c->x[1], sizeof(uint32_t))) {
        const NvnTextureBuilder* b = texture_builder(c->x[0]);
        uint32_t layout = 0;
        c->x[0] = nvn_texture_packaged_layout(b->tile, &layout);
        gw32(c->x[1], layout);
    } else {
        LOG("nvn-native: %s unwritable output\n", name);
    }
    pm_unlock(&objs_lock);
}


static void h_texture_builder_get_storage_class(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const NvnTextureBuilder* b = known_record(c->x[0], NVN_OBJ_TEXTURE_BUILDER);
    if (!b)
        LOG("nvn-native: nvnTextureBuilderGetStorageClass unknown builder=%llx\n", (unsigned long long)c->x[0]);
    c->x[0] = b ? nvn_texture_storage_class(b, &nvn_texture_device_words) : 0;
    pm_unlock(&objs_lock);
}
static void h_texture_builder_get_zcull_storage_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const NvnTextureBuilder* b = known_record(c->x[0], NVN_OBJ_TEXTURE_BUILDER);
    if (!b) {
        LOG("nvn-native: nvnTextureBuilderGetZCullStorageSize unknown builder=%llx\n",
            (unsigned long long)c->x[0]);
        c->x[0] = 0;
    } else {
        uint32_t size = 0;
        if (nvn_texture_zcull_size(b, &nvn_zcull_device_words, &size) == NVN_TEXTURE_ZCULL_REFUSAL)
            LOG("nvn-native: nvnTextureBuilderGetZCullStorageSize flags=%x: the device ZCull info path is not "
                "implemented\n", b->flags);
        c->x[0] = size;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_builder_get_sparse_tile_layout(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureBuilderGetSparseTileLayout";
    const u64 dst = c->x[1];
    uint32_t out[4];
    int computed = 0;
    pm_lock(&objs_lock);
    const NvnTextureBuilder* b = known_record(c->x[0], NVN_OBJ_TEXTURE_BUILDER);
    if (!b) {
        LOG("nvn-native: %s unknown builder=%llx\n", name, (unsigned long long)c->x[0]);
    } else if (!dst) {
        LOG("nvn-native: %s null output\n", name);
    } else if (!(computed = nvn_texture_sparse_layout(b, out))) {
        LOG("nvn-native: %s invalid format=%x\n", name, b->format);
    }
    pm_unlock(&objs_lock);
    if (computed && !native_write_tile_layout(dst, out))
        LOG("nvn-native: %s unwritable output=%llx\n", name, (unsigned long long)dst);
}

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
#define PROGRAM_ENTRY_BYTES_PLAIN 0x10u /* one SetShaders entry: ignored word, header pointer */
#define PROGRAM_HEADER_READ_BYTES (NVN_PROGRAM_STAGE_OFFSET + 4u) /* the header words up to the stage word */
#define PROGRAM_CONTROL_BYTES 0x18u     /* the control block that the host copies */

#define PROGRAM_REFUSAL_ENTRY_UNREADABLE "entry is unreadable"
#define PROGRAM_REFUSAL_HEADER_UNREADABLE "header is unreadable"
#define PROGRAM_REFUSAL_HEADER_BAD "header is not a shader"
#define PROGRAM_REFUSAL_STAGE_REPEATED "stage is repeated"
#define PROGRAM_REFUSAL_CONTROL_UNREADABLE "control is unreadable"

#define PROGRAM_BUILD_HEADER_BYTES 0x76Cu /* the header bytes that the word builder reads, up to h+0x76b */
#define PROGRAM_HEADER_CB_OFFSET 0x700u
#define PROGRAM_HEADER_CODE_OFFSET 0x708u
#define PROGRAM_HEADER_VALUE_OFFSET 0x70Cu
#define PROGRAM_HEADER_LAYOUT_OFFSET 0x8u
#define PROGRAM_COMPUTE_HEADER_COPY_BYTES 0x834u /* 0x833 rounded up to a word */
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

/* The two methods that carry the region base once for a frame job: high 32 bits, then low 32 bits. */
#define NVN_SHADER_REGION_BASE_HIGH_METHOD 0x582u
#define NVN_SHADER_REGION_BASE_LOW_METHOD 0x583u

/* Measured with the game's own library (nvn_native=0). The host has no device object, so these are constants. */
static const NvnProgramDeviceIn program_device_words = {
    .d1a38 = 0x087f6080u, .d1a3c = 0u, .d1a40 = 0u, .d1a68 = 0x20u,
    .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u, .da4 = 0u,
};

/* The layout of one SetShaders entry. Ext: word, header pointer, control pointer. Plain: the word is ignored and
   there is no control pointer. */
typedef struct {
    uint32_t bytes;  /* the size of one entry */
    int has_control; /* 1 when the entry has a control pointer */
} ProgramEntryLayout;
static const ProgramEntryLayout program_entry_ext = {PROGRAM_ENTRY_BYTES, 1};
static const ProgramEntryLayout program_entry_plain = {PROGRAM_ENTRY_BYTES_PLAIN, 0};

/* Caller holds objs_lock. Reads one entry and its header. On a refusal returns 0 and sets *reason. */
static int program_read_entry(u64 entries, int32_t index, const ProgramEntryLayout* layout, ProgramStageRec* out,
                              uint32_t* stage_index, const char** reason) {
    u64 offset = (u64)(uint32_t)index * layout->bytes;
    if (entries > UINT64_MAX - offset) {
        *reason = PROGRAM_REFUSAL_ENTRY_UNREADABLE;
        return 0;
    }
    u64 at = entries + offset;
    if (!native_guest_readable(at, layout->bytes)) {
        *reason = PROGRAM_REFUSAL_ENTRY_UNREADABLE;
        return 0;
    }
    uint64_t entry_word = layout->has_control ? gr64(at) : 0;
    uint64_t header = gr64(at + 8);
    uint64_t control = layout->has_control ? gr64(at + 0x10) : 0;
    const char* refusal = nvn_program_entry_refusal(layout->has_control, entry_word, header);
    if (refusal) {
        *reason = refusal;
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
/* The library stores these nine fragment bytes as 0 or 1 (d837c64). */
static void program_read_fragment_in(u64 header, NvnProgramFragmentIn* out) {
    out->g148 = program_guest_byte(header + PROGRAM_FRAGMENT_G148_OFFSET) != 0 ? 1u : 0u;
    out->g150 = program_guest_byte(header + PROGRAM_FRAGMENT_G150_OFFSET) != 0 ? 1u : 0u;
    out->g158 = program_guest_byte(header + PROGRAM_FRAGMENT_G158_OFFSET) != 0 ? 1u : 0u;
    out->g160 = program_guest_byte(header + PROGRAM_FRAGMENT_G160_OFFSET) != 0 ? 1u : 0u;
    out->g168 = program_guest_byte(header + PROGRAM_FRAGMENT_G168_OFFSET) != 0 ? 1u : 0u;
    out->g170 = program_guest_byte(header + PROGRAM_FRAGMENT_G170_OFFSET) != 0 ? 1u : 0u;
    out->g178 = program_guest_byte(header + PROGRAM_FRAGMENT_G178_OFFSET) != 0 ? 1u : 0u;
    out->g180 = program_guest_byte(header + PROGRAM_FRAGMENT_G180_OFFSET) != 0 ? 1u : 0u;
    out->g188 = program_guest_byte(header + PROGRAM_FRAGMENT_G188_OFFSET) != 0 ? 1u : 0u;
    out->g190 = gr32(header + PROGRAM_FRAGMENT_G190_OFFSET);
    out->g198 = gr32(header + PROGRAM_FRAGMENT_G198_OFFSET);
    for (uint32_t i = 0; i < PROGRAM_FRAGMENT_G1A0_WORDS; i++)
        out->g1a0[i] = gr32(header + PROGRAM_FRAGMENT_G1A0_OFFSET + (u64)i * 4u);
}

/* Caller holds objs_lock. Copies the compute header to a host buffer and reads the words at the library offsets, so
   that only the bytes of the layout are touched. */
static int program_read_compute_in(const ProgramStageRec* rec, NvnProgramComputeIn* out) {
    const uint32_t layout = gr32(rec->header + PROGRAM_HEADER_LAYOUT_OFFSET);
    const uint32_t bytes = nvn_program_compute_header_bytes(layout);
    const uint32_t copy_bytes = (bytes + 3u) & ~3u; /* the last gr32 reads a whole word */
    if (!bytes || !native_guest_readable(rec->header, copy_bytes)) return 0;
    uint8_t header[PROGRAM_COMPUTE_HEADER_COPY_BYTES];
    for (uint32_t at = 0; at < copy_bytes; at += 4u) {
        uint32_t word = gr32(rec->header + at);
        memcpy(header + at, &word, sizeof word);
    }
    return nvn_program_compute_in_from_header(header, bytes, (uint32_t)rec->entry_word, out);
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

/* Caller holds objs_lock. The header read check of program_read_entry covered header + 0x6EE. Reads the transform
   feedback blob of the vertex header and appends its words. Returns 0 on a refusal and sets *reason. */
static int program_append_xfb_words(u64 header, uint32_t* words, uint32_t max, uint32_t* count, const char** reason) {
    uint8_t blob[NVN_PROGRAM_XFB_BLOB_BYTES];
    u64 at = header + NVN_PROGRAM_XFB_HEADER_OFFSET;
    for (uint32_t i = 0; i < NVN_PROGRAM_XFB_BLOB_BYTES; i++) blob[i] = program_guest_byte(at + (u64)i);
    return nvn_program_xfb_words(blob, words, max, count, reason);
}

/* nvnProgramSetShadersExt and nvnProgramSetShaders. All entries must be valid: the host writes the new stage table only
   then, and keeps the old content on a refusal. */
static void program_set_shaders(CPU* c, const ProgramEntryLayout* layout) {
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
    const NvnProgramCountCheck count_check = nvn_program_count_check(count, entries);
    if (count_check != NVN_PROGRAM_COUNT_OK) {
        switch (count_check) {
        case NVN_PROGRAM_COUNT_NEGATIVE:
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: count=%d is negative\n",
                      (unsigned long long)addr, count);
            break;
        case NVN_PROGRAM_COUNT_ABOVE:
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: count=%d is above %u\n",
                      (unsigned long long)addr, count, (unsigned)NVN_PROGRAM_STAGES);
            break;
        case NVN_PROGRAM_COUNT_ENTRIES_NULL:
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: entries is null\n",
                      (unsigned long long)addr);
            break;
        case NVN_PROGRAM_COUNT_OK:
            break;
        }
        ok = 0;
    } else if (count > 0 && !native_guest_readable(entries, (u64)count * layout->bytes)) {
        LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: entries=%llx is unreadable\n",
                  (unsigned long long)addr, (unsigned long long)entries);
        ok = 0;
    }
    for (int32_t i = 0; ok && i < count; i++) {
        ProgramStageRec stage;
        uint32_t stage_index = 0;
        const char* reason = NULL;
        if (!program_read_entry(entries, i, layout, &stage, &stage_index, &reason)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx entry=%d refused: %s\n",
                      (unsigned long long)addr, i, reason);
            ok = 0;
            break;
        }
        if (!nvn_program_stage_add(&next.stage_mask, stage_index)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx entry=%d refused: %s\n",
                      (unsigned long long)addr, i, PROGRAM_REFUSAL_STAGE_REPEATED);
            ok = 0;
            break;
        }
        next.stage[stage_index] = stage;
    }
    if (ok) {
        int stage = nvn_program_unsupported_stage(next.stage_mask);
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
        if (next.stage_mask & (1u << NVN_PROGRAM_STAGE_VERTEX)) {
            if (!program_read_stage_in(&next.stage[NVN_PROGRAM_STAGE_VERTEX], &vertex_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)NVN_PROGRAM_STAGE_VERTEX);
                ok = 0;
            } else {
                vertex = &vertex_in;
            }
        }
        if (ok && (next.stage_mask & (1u << NVN_PROGRAM_STAGE_FRAGMENT))) {
            if (!program_read_stage_in(&next.stage[NVN_PROGRAM_STAGE_FRAGMENT], &fragment_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)NVN_PROGRAM_STAGE_FRAGMENT);
                ok = 0;
            } else {
                fragment = &fragment_in;
                frag.present = 1;
                program_read_fragment_in(next.stage[NVN_PROGRAM_STAGE_FRAGMENT].header, &frag);
            }
        }
        if (ok && (next.stage_mask & (1u << NVN_PROGRAM_STAGE_COMPUTE))) {
            if (!program_read_compute_in(&next.stage[NVN_PROGRAM_STAGE_COMPUTE], &compute_in)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT,
                          "nvn-native: program %llx stage=%u refused: header is too short for the word array\n",
                          (unsigned long long)addr, (unsigned)NVN_PROGRAM_STAGE_COMPUTE);
                ok = 0;
            } else {
                compute = &compute_in;
            }
        }
        if (ok && compute && (vertex || fragment)) {
            uint32_t other = vertex ? NVN_PROGRAM_STAGE_VERTEX : NVN_PROGRAM_STAGE_FRAGMENT;
            LOG_LIMIT(PROGRAM_LOG_LIMIT,
                      "nvn-native: program %llx refused: compute stage %u with stage %u is not supported by the host "
                      "builder\n",
                      (unsigned long long)addr, (unsigned)NVN_PROGRAM_STAGE_COMPUTE, (unsigned)other);
            ok = 0;
        }
        uint32_t program_id = 0;
        DeviceRec* device = ok && !compute ? known_record(r->device, NVN_OBJ_DEVICE) : NULL;
        if (ok && !compute && !device) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: its device %llx is not live\n",
                      (unsigned long long)addr, (unsigned long long)r->device);
            ok = 0;
        } else if (device) {
            program_id = ++device->program_ids;
            if (!program_id) program_id = ++device->program_ids; /* the library retries the increment at the carry */
        }
        if (ok && !nvn_program_build_words(compute, vertex, fragment, &frag, &program_device_words, program_id,
                                           NVN_SHADER_REGION_BASE, next.words, &next.word_count)) {
            LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: a word value is above 13 bits\n",
                      (unsigned long long)addr);
            ok = 0;
        }
        /* The transform feedback block follows all graphics words. Only the vertex stage has a blob. */
        if (ok && (next.stage_mask & (1u << NVN_PROGRAM_STAGE_VERTEX))) {
            const char* reason = NULL;
            uint32_t xfb_count = 0;
            if (!program_append_xfb_words(next.stage[NVN_PROGRAM_STAGE_VERTEX].header, next.words + next.word_count,
                                          NVN_PROGRAM_WORDS_MAX - next.word_count, &xfb_count, &reason)) {
                LOG_LIMIT(PROGRAM_LOG_LIMIT, "nvn-native: program %llx refused: %s\n", (unsigned long long)addr,
                          reason);
                ok = 0;
            } else {
                next.word_count += xfb_count;
            }
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
static void h_program_set_shaders_ext(CPU* c, u64 entry) {
    (void)entry;
    program_set_shaders(c, &program_entry_ext);
}
static void h_program_set_shaders(CPU* c, u64 entry) {
    (void)entry;
    program_set_shaders(c, &program_entry_plain);
}
/* nvnProgramSetSampleShading(prog, value): the host renders one sample, so the value has no effect. */
static void h_program_set_sample_shading(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    u64 addr = c->x[0];
    if (known_record(addr, NVN_OBJ_PROGRAM))
        LOG_LIMIT(1, "nvn-native: nvnProgramSetSampleShading is a logged no-op: "
                     "the host renders one sample, so sample shading has no effect\n");
    else
        LOG_LIMIT(1, "nvn-native: program set sample shading %llx is not a live program\n",
                  (unsigned long long)addr);
    pm_unlock(&objs_lock);
}
/* nvnProgramSetSubroutineLinkage(program, count, maps): the host has no subroutine table, so it returns 0. */
static void h_program_set_subroutine_linkage(CPU* c, u64 entry) {
    (void)entry;
    LOG_LIMIT(1, "nvn-native: nvnProgramSetSubroutineLinkage refused: the host has no subroutine table "
                 "(program=%llx count=%llx maps=%llx)\n",
              (unsigned long long)c->x[0], (unsigned long long)c->x[1],
              (unsigned long long)c->x[2]);
    c->x[0] = 0;
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
static void sampler_pool_register(CPU* c, NvnObjKind source_kind, const char* source_name) {
    uint64_t at;
    if (!descriptor_destination(c, NVN_OBJ_SAMPLER_POOL, "sampler registration", NVN_SAMPLER_DESCRIPTOR_BYTES, &at)) return;
    NvnSampler* sampler = known_record(c->x[2], source_kind);
    if (!sampler) {
        LOG("nvn-native: sampler registration unknown %s=%llx\n", source_name, (unsigned long long)c->x[2]);
        return;
    }
    uint32_t words[NVN_SAMPLER_DESCRIPTOR_WORDS];
    if (!nvn_sampler_descriptor(sampler, &nvn_sampler_device_words, words)) {
        LOG("nvn-native: sampler registration unsupported wrap modes %u,%u,%u %s=%llx\n", sampler->wrap[0],
            sampler->wrap[1], sampler->wrap[2], source_name, (unsigned long long)c->x[2]);
        return;
    }
    for (unsigned i = 0; i < NVN_SAMPLER_DESCRIPTOR_WORDS; i++) gw32(at + i * sizeof(uint32_t), words[i]);
}
static void h_sampler_pool_register_sampler(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    sampler_pool_register(c, NVN_OBJ_SAMPLER, "sampler");
    pm_unlock(&objs_lock);
}
static void h_sampler_pool_register_sampler_builder(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    sampler_pool_register(c, NVN_OBJ_SAMPLER_BUILDER, "sampler builder");
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
TEXTURE_GETTER(depth_stencil_mode, t->builder.depth_stencil_mode)
TEXTURE_GETTER(flags, t->builder.flags)
TEXTURE_GETTER(format, t->builder.format)
TEXTURE_GETTER(levels, t->builder.levels)
TEXTURE_GETTER(memory_offset, t->builder.offset)
TEXTURE_GETTER(memory_pool, t->builder.pool)
TEXTURE_GETTER(samples, t->builder.samples)
TEXTURE_GETTER(storage_size, nvn_texture_storage_size(t))
TEXTURE_GETTER(texture_address, t->address)
#undef TEXTURE_GETTER

static void h_texture_get_debug_id(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = 0; /* the library body is `mov x0, xzr; ret` */
}
static void h_texture_get_raw_storage_class(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!t) LOG("nvn-native: nvnTextureGetRawStorageClass unknown texture=%llx\n", (unsigned long long)c->x[0]);
    c->x[0] = t ? nvn_texture_raw_storage_class(t->builder.flags, t->builder.format) : 0;
    pm_unlock(&objs_lock);
}
static void h_texture_get_storage_class(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!t) LOG("nvn-native: nvnTextureGetStorageClass unknown texture=%llx\n", (unsigned long long)c->x[0]);
    c->x[0] = t ? nvn_texture_storage_class(&t->builder, &nvn_texture_device_words) : 0;
    pm_unlock(&objs_lock);
}
static void h_texture_get_zcull_storage_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!t) {
        LOG("nvn-native: nvnTextureGetZCullStorageSize unknown texture=%llx\n", (unsigned long long)c->x[0]);
        c->x[0] = 0;
    } else {
        uint32_t size = 0;
        if (nvn_texture_zcull_size(&t->builder, &nvn_zcull_device_words, &size) == NVN_TEXTURE_ZCULL_REFUSAL)
            LOG("nvn-native: nvnTextureGetZCullStorageSize flags=%x: the device ZCull info path is not "
                "implemented\n", t->builder.flags);
        c->x[0] = size;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_get_sparse_tile_layout(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureGetSparseTileLayout";
    const u64 dst = c->x[1];
    uint32_t out[4];
    int computed = 0;
    pm_lock(&objs_lock);
    const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!t) {
        LOG("nvn-native: %s unknown texture=%llx\n", name, (unsigned long long)c->x[0]);
    } else if (!dst) {
        LOG("nvn-native: %s null output\n", name);
    } else if (!(computed = nvn_texture_sparse_layout(&t->builder, out))) {
        LOG("nvn-native: %s invalid format=%x\n", name, t->builder.format);
    }
    pm_unlock(&objs_lock);
    if (computed && !native_write_tile_layout(dst, out))
        LOG("nvn-native: %s unwritable output=%llx\n", name, (unsigned long long)dst);
}
static void h_texture_get_view_offset(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureGetViewOffset";
    pm_lock(&objs_lock);
    const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!t) {
        LOG("nvn-native: %s unknown texture=%llx\n", name, (unsigned long long)c->x[0]);
        c->x[0] = 0;
    } else {
        const NvnTextureView* v = NULL;
        int view_known = 1;
        if (c->x[1]) {
            v = known_record(c->x[1], NVN_OBJ_TEXTURE_VIEW);
            if (!v) {
                LOG("nvn-native: %s unknown view=%llx\n", name, (unsigned long long)c->x[1]);
                view_known = 0;
            }
        }
        uint64_t offset = 0;
        if (!view_known || !nvn_texture_view_offset(t, v, &offset)) {
            if (view_known) LOG("nvn-native: %s base level reaches 32\n", name);
            offset = 0;
        }
        c->x[0] = offset;
    }
    pm_unlock(&objs_lock);
}
static void h_texture_get_swizzle(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnTextureGetSwizzle";
    pm_lock(&objs_lock);
    if (guest_outputs_writable(c, name, 4)) {
        const TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE);
        if (!t)
            LOG("nvn-native: %s unknown texture=%llx\n", name, (unsigned long long)c->x[0]);
        else
            for (unsigned i = 0; i < 4; i++) gw32(c->x[1 + i], t->builder.swizzle[i]);
    }
    pm_unlock(&objs_lock);
}
static void h_texture_compare(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const TextureRec* a = known_record(c->x[0], NVN_OBJ_TEXTURE);
    const TextureRec* b = known_record(c->x[1], NVN_OBJ_TEXTURE);
    if (!a || !b)
        LOG("nvn-native: nvnTextureCompare unknown texture=%llx\n",
            (unsigned long long)(a ? c->x[1] : c->x[0]));
    c->x[0] = a && b ? nvn_texture_equal(a, b) : 0;
    pm_unlock(&objs_lock);
}
static void h_texture_view_compare(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const NvnTextureView* a = known_record(c->x[0], NVN_OBJ_TEXTURE_VIEW);
    const NvnTextureView* b = known_record(c->x[1], NVN_OBJ_TEXTURE_VIEW);
    if (!a || !b)
        LOG("nvn-native: nvnTextureViewCompare unknown view=%llx\n",
            (unsigned long long)(a ? c->x[1] : c->x[0]));
    c->x[0] = a && b ? nvn_texture_view_equal(a, b) : 0;
    pm_unlock(&objs_lock);
}

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

/* The GPU address of a texture in its pool: the pitch range for a linear or BUFFER texture, else the block linear
   range. A virtual pool has no block linear range, so it keeps range 0 (d83f418). */
static uint64_t texture_pool_base(const MemoryPoolRec* pool, const NvnTextureBuilder* b) {
    const int pitch = nvn_texture_uses_pitch_range(b->flags, b->target);
    const uint64_t base = (pitch || !pool->block_linear_address) ? pool->gpu_address : pool->block_linear_address;
    return base + b->offset;
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
    t->address = texture_pool_base(pool, b);
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

/* The library's function is one `ret`: the object stays readable, so the host keeps its record (U1 decision 2). */
static void h_texture_finalize(CPU* c, u64 entry) {
    (void)c;
    (void)entry;
}
/* The library flushes or invalidates the pool cache and moves no data; the host write tracking already covers the pool. */
static void texture_cache_noop(CPU* c, const char* name) {
    pm_lock(&objs_lock);
    if (!known_record(c->x[0], NVN_OBJ_TEXTURE))
        LOG("nvn-native: %s unknown texture=%llx\n", name, (unsigned long long)c->x[0]);
    else
        LOG_LIMIT(1, "nvn-native: %s is a no-op (the renderer reads the same host memory)\n", name);
    pm_unlock(&objs_lock);
}
static void h_texture_flush_texels(CPU* c, u64 entry) {
    (void)entry;
    texture_cache_noop(c, "nvnTextureFlushTexels");
}
static void h_texture_invalidate_texels(CPU* c, u64 entry) {
    (void)entry;
    texture_cache_noop(c, "nvnTextureInvalidateTexels");
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
/* Reads and checks the arguments of one transfer. to_texture is 1 for WriteTexels (guest buffer to texture) and 0 for
   ReadTexels (texture to guest buffer). row_stride and image_stride are the library's x4 and x5; 0 is the tight stride.
   1 with call filled when the call copies; 0 means return now, and the reason of a refusal is in the log. Caller holds
   objs_lock. */
static int texture_write_check(CPU* c, TextureWriteCall* call, const char* operation, int to_texture,
                               uint32_t row_stride, uint32_t image_stride) {
    const TextureRec* texture = known_record(c->x[0], NVN_OBJ_TEXTURE);
    if (!texture) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s unknown texture=%llx\n", operation,
                  (unsigned long long)c->x[0]);
        return 0;
    }
    const NvnTextureView* view = NULL;
    if (c->x[1]) {
        view = known_record(c->x[1], NVN_OBJ_TEXTURE_VIEW);
        if (!view) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s unknown view=%llx\n", operation,
                      (unsigned long long)c->x[1]);
            return 0;
        }
        if (view->flags & NVN_VIEW_FORMAT) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s view=%llx format changes the level size\n", operation,
                      (unsigned long long)c->x[1]);
            return 0;
        }
    }
    if (!native_guest_readable(c->x[2], TEXTURE_WRITE_REGION_BYTES)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s unreadable region=%llx\n", operation,
                  (unsigned long long)c->x[2]);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s texture=%llx is linear\n", operation,
                  (unsigned long long)c->x[0]);
        return 0;
    }
    if (texture->builder.target == NVN_TEXTURE_TARGET_BUFFER) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s texture=%llx has the BUFFER target\n", operation,
                  (unsigned long long)c->x[0]);
        return 0;
    }
    if (texture->layout.sample_x != 1 || texture->layout.sample_y != 1) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s texture=%llx is multisample\n", operation,
                  (unsigned long long)c->x[0]);
        return 0;
    }
    const uint64_t region = c->x[2];
    uint32_t rx = gr32(region), ry = gr32(region + 4), rz = gr32(region + 8);
    uint32_t width = gr32(region + 0xc), height = gr32(region + 0x10), depth = gr32(region + 0x14);
    /* The effective target 1D array moves y and height to the layer fields and copies one row. */
    uint32_t target = view ? view->target : texture->builder.target;
    int one_d_array = target == NVN_TEXTURE_TARGET_1D_ARRAY;
    uint32_t region_layer = one_d_array ? ry : rz;
    uint32_t layer_count = one_d_array ? height : depth;
    uint32_t region_row = one_d_array ? 0 : ry;
    uint32_t row_count = one_d_array ? 1 : height;
    if (!width || !layer_count || !row_count) return 0; /* the library copies nothing */
    uint32_t level = view && (view->flags & NVN_VIEW_LEVELS) ? view->base_level : 0;
    if (!nvn_texture_level(texture, level, &call->level)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s texture=%llx level=%u levels=%u is outside the texture\n",
                  operation, (unsigned long long)c->x[0], level, (unsigned)texture->layout.levels);
        return 0;
    }
    const MemoryPoolRec* pool = known_record(texture->builder.pool, NVN_OBJ_MEMORY_POOL);
    if (!pool) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s unknown pool=%llx\n", operation,
                  (unsigned long long)texture->builder.pool);
        return 0;
    }
    uint32_t base_layer = view && (view->flags & NVN_VIEW_LAYERS) ? view->base_layer : 0;
    uint64_t layer_first;
    if (!texture_write_add(base_layer, region_layer, &layer_first)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s layer arithmetic overflows\n", operation);
        return 0;
    }
    uint64_t layer_end;
    /* A 3D level counts its slices in the level depth; the other targets count layers in the texture depth. */
    uint64_t max_layers = texture->layout.dim == 3 ? call->level.depth :
                          texture->layout.depth ? texture->layout.depth : 1;
    if (!texture_write_add(layer_first, layer_count, &layer_end) || layer_end > max_layers) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s layer=%llu count=%u depth=%llu is outside the texture\n",
                  operation, (unsigned long long)layer_first, layer_count,
                  (unsigned long long)texture->layout.depth);
        return 0;
    }
    uint64_t bw = texture->layout.block_width, bh = texture->layout.block_height;
    uint64_t bpb = texture->layout.bytes_per_block;
    uint64_t x_start = texture_write_ceil(rx, bw) * bpb;
    uint64_t tight_row = texture_write_ceil(width, bw) * bpb;
    uint64_t x_end = x_start + tight_row;
    uint64_t row_start = texture_write_ceil(region_row, bh);
    uint64_t rows = texture_write_ceil(row_count, bh);
    uint64_t row_end = row_start + rows;
    uint64_t tight_layer;
    if (!texture_write_mul(tight_row, rows, &tight_layer) || x_end > UINT32_MAX || row_end > UINT32_MAX) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s size arithmetic overflows\n", operation);
        return 0;
    }
    /* A nonzero stride replaces the tight one. A stride below the tight bytes would overlap the rows. */
    if (row_stride && row_stride < tight_row) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s row stride=%u is below the tight row bytes=%llu\n",
                  operation, row_stride, (unsigned long long)tight_row);
        return 0;
    }
    if (image_stride && image_stride < tight_layer) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s image stride=%u is below the tight image bytes=%llu\n",
                  operation, image_stride, (unsigned long long)tight_layer);
        return 0;
    }
    uint64_t src_row_bytes = row_stride ? row_stride : tight_row;
    uint64_t src_layer_stride;
    if (image_stride) {
        src_layer_stride = image_stride;
    } else if (!texture_write_mul(src_row_bytes, rows, &src_layer_stride)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s size arithmetic overflows\n", operation);
        return 0;
    }
    uint64_t x_limit = (uint64_t)call->level.gobs_x * NVN_TEXTURE_GOB_WIDTH;
    /* The level holds whole blocks: a block is 2^bh GOBs, that is 8 << bh rows, high. */
    uint64_t row_block = (uint64_t)NVN_TEXTURE_GOB_HEIGHT << call->level.block_height_log2;
    uint64_t row_limit = (call->level.rows + row_block - 1) / row_block * row_block;
    if (x_end > x_limit) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s x_end=%llu is outside the level columns=%llu\n",
                  operation, (unsigned long long)x_end, (unsigned long long)x_limit);
        return 0;
    }
    if (row_end > row_limit) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s row_end=%llu is outside the level rows=%llu\n",
                  operation, (unsigned long long)row_end, (unsigned long long)row_limit);
        return 0;
    }
    /* The guest buffer holds only the region: its rows are src_row_bytes apart and start at the first byte (d8232a0). */
    uint64_t buffer_length, buffer_end;
    if (!texture_write_mul(layer_count, src_layer_stride, &buffer_length) ||
        !texture_write_add(c->x[3], buffer_length, &buffer_end)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s buffer arithmetic overflows\n", operation);
        return 0;
    }
    if (to_texture ? !native_guest_readable(c->x[3], buffer_length)
                   : !native_guest_writable(c->x[3], buffer_length)) {
        LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s %s buffer=%llx bytes=%llu\n", operation,
                  to_texture ? "unreadable source" : "unwritable destination", (unsigned long long)c->x[3],
                  (unsigned long long)buffer_length);
        return 0;
    }
    if (texture->layout.dim == 3) {
        /* A 3D level is one layer: its slices share the level base and the level size covers all of them. */
        uint64_t level_base;
        if (!texture_write_add(pool->storage, texture->builder.offset, &level_base) ||
            !texture_write_add(level_base, call->level.offset, &level_base)) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s address arithmetic overflows\n", operation);
            return 0;
        }
        if (to_texture ? !native_guest_writable(level_base, call->level.size)
                       : !native_guest_readable(level_base, call->level.size)) {
            LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s %s 3D level=%llx bytes=%llu\n", operation,
                      to_texture ? "unwritable" : "unreadable", (unsigned long long)level_base,
                      (unsigned long long)call->level.size);
            return 0;
        }
    } else {
        uint64_t layer_stride = texture->layout.layer_stride;
        for (uint32_t i = 0; i < layer_count; i++) {
            uint64_t layer_offset, level_base;
            if (!texture_write_mul(layer_first + i, layer_stride, &layer_offset) ||
                !texture_write_add(texture->builder.offset, layer_offset, &layer_offset) ||
                !texture_write_add(pool->storage, layer_offset, &level_base) ||
                !texture_write_add(level_base, call->level.offset, &level_base)) {
                LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s address arithmetic overflows\n", operation);
                return 0;
            }
            if (to_texture ? !native_guest_writable(level_base, call->level.size)
                           : !native_guest_readable(level_base, call->level.size)) {
                LOG_LIMIT(TEXTURE_WRITE_LOG_LIMIT, "nvn-native: %s %s layer=%llu level=%llx bytes=%llu\n", operation,
                          to_texture ? "unwritable" : "unreadable", (unsigned long long)(layer_first + i),
                          (unsigned long long)level_base, (unsigned long long)call->level.size);
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
static void texture_write_slice(const TextureWriteCall* call, uint32_t index, int to_texture) {
    uint64_t layer = call->layer_first + index;
    int is_3d = call->texture->layout.dim == 3;
    uint64_t level_base = call->pool->storage + call->texture->builder.offset + call->level.offset +
                          (is_3d ? 0 : layer * call->texture->layout.layer_stride);
    uint8_t* buffer = GP(call->source + index * call->src_layer_stride);
    uint32_t z = is_3d ? (uint32_t)layer : 0;
    if (to_texture)
        nvn_texture_write_rows(GP(level_base), &call->level, call->x_start, call->x_end, call->row_start,
                               call->row_end, buffer, call->src_row_bytes, z);
    else
        nvn_texture_read_rows(GP(level_base), &call->level, call->x_start, call->x_end, call->row_start,
                              call->row_end, buffer, call->src_row_bytes, z);
}
static void texture_transfer(CPU* c, const char* operation, int to_texture, uint32_t row_stride,
                             uint32_t image_stride) {
    pm_lock(&objs_lock);
    TextureWriteCall call;
    if (texture_write_check(c, &call, operation, to_texture, row_stride, image_stride))
        for (uint32_t i = 0; i < call.layer_count; i++) texture_write_slice(&call, i, to_texture);
    pm_unlock(&objs_lock);
}
static void h_texture_write_texels(CPU* c, u64 entry) {
    (void)entry;
    texture_transfer(c, "nvnTextureWriteTexels", 1, 0, 0);
}
static void h_texture_write_texels_strided(CPU* c, u64 entry) {
    (void)entry;
    texture_transfer(c, "nvnTextureWriteTexelsStrided", 1, (u32)c->x[4], (u32)c->x[5]);
}
static void h_texture_read_texels(CPU* c, u64 entry) {
    (void)entry;
    texture_transfer(c, "nvnTextureReadTexels", 0, 0, 0);
}
static void h_texture_read_texels_strided(CPU* c, u64 entry) {
    (void)entry;
    texture_transfer(c, "nvnTextureReadTexelsStrided", 0, (u32)c->x[4], (u32)c->x[5]);
}

/* G6: command buffers (decisions 3 to 5 of the 3c design). The host records ops and never writes the command or control
   memory of the game. A published list is owned by the command buffer until the game gives control memory that overlaps
   the memory the list had: the library's handle is a control memory address, so that is when a handle ends. */
typedef struct { uint64_t handle, begin, end; } OwnedList;
typedef struct {
    uint64_t device, callback, callback_data;
    uint64_t command_pool, command_offset, command_size;
    uint64_t control_begin, control_end;
    uint32_t copy_row_stride, copy_image_stride; /* cb+0x88 and cb+0x8C of the library; 0 is the tight stride */
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
/* U5: the pure getters of a command buffer. The host record holds the last-chunk values that the library reads; an
   unknown command buffer answers 0 after the command_buffer log line. */
#define COMMAND_GETTER_HANDLER(handler, operation, expression)                                     \
    static void h_command_buffer_##handler(CPU* c, u64 entry) {                                    \
        (void)entry;                                                                               \
        pm_lock(&objs_lock);                                                                       \
        const CommandBufferRec* cb = command_buffer(c->x[0], operation);                           \
        c->x[0] = cb ? (expression) : 0;                                                           \
        pm_unlock(&objs_lock);                                                                     \
    }
COMMAND_GETTER_HANDLER(get_memory_callback, "nvnCommandBufferGetMemoryCallback", cb->callback)
COMMAND_GETTER_HANDLER(get_memory_callback_data, "nvnCommandBufferGetMemoryCallbackData", cb->callback_data)
COMMAND_GETTER_HANDLER(is_recording, "nvnCommandBufferIsRecording", cb->recording != 0)
/* The library returns 0x10 before any AddControlMemory and the chunk size after one. */
COMMAND_GETTER_HANDLER(get_control_memory_size, "nvnCommandBufferGetControlMemorySize",
                       nvn_control_memory_size(cb->control_begin, cb->control_end, cb->control_adds))
#undef COMMAND_GETTER_HANDLER
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
/* Set/GetCopyRowStride and Set/GetCopyImageStride are the library stores and loads at cb+0x88 and cb+0x8C. The value 0
   means the tight stride; patch 2 of U3 reads the fields for the texture copies. */
static void h_command_buffer_set_copy_row_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferSetCopyRowStride");
    if (cb) cb->copy_row_stride = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_copy_image_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], "nvnCommandBufferSetCopyImageStride");
    if (cb) cb->copy_image_stride = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_command_buffer_get_copy_row_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const CommandBufferRec* cb = known_record(c->x[0], NVN_OBJ_COMMAND_BUFFER);
    if (!cb) COMMAND_LOG("nvn-native: nvnCommandBufferGetCopyRowStride unknown command buffer=%llx\n",
                         (unsigned long long)c->x[0]);
    c->x[0] = cb ? cb->copy_row_stride : 0;
    pm_unlock(&objs_lock);
}
static void h_command_buffer_get_copy_image_stride(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const CommandBufferRec* cb = known_record(c->x[0], NVN_OBJ_COMMAND_BUFFER);
    if (!cb) COMMAND_LOG("nvn-native: nvnCommandBufferGetCopyImageStride unknown command buffer=%llx\n",
                         (unsigned long long)c->x[0]);
    c->x[0] = cb ? cb->copy_image_stride : 0;
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
/* Appends one op to the open list. An op outside a recording or one that cannot be stored is dropped with a log line.
   1 when the op is stored, 0 when it was dropped. */
static int command_record(CommandBufferRec* cb, u64 addr, const char* operation, NvnOpCode code, const void* payload,
                          uint32_t bytes) {
    if (!cb->recording) {
        cb->dropped_ops++;
        __atomic_fetch_add(&dropped_ops, 1, __ATOMIC_RELAXED);
        COMMAND_LOG("nvn-native: %s outside a recording, command buffer=%llx: dropped\n", operation,
                    (unsigned long long)addr);
        return 0;
    }
    if (!nvn_oplist_append(&cb->open, code, payload, bytes)) fatal("nvn-native: out of memory for a command op\n");
    return 1;
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
#define COMMAND_REFUSE_LOG_LIMIT 16u /* log lines for the refusals of the U4 command functions */
/* A refused command: the name, the reason and the arguments. Nothing is recorded; these library functions return
   void, so there is no result to keep. */
static void command_refused(const CPU* c, const char* operation, const char* reason) {
    LOG_LIMIT(COMMAND_REFUSE_LOG_LIMIT, "nvn-native: %s refused: %s (x0=%llx x1=%llx x2=%llx x3=%llx)\n", operation,
              reason, (unsigned long long)c->x[0], (unsigned long long)c->x[1],
              (unsigned long long)c->x[2], (unsigned long long)c->x[3]);
}
/* The arguments of one draw state setter, stored as words. The builder in nvn_params.c makes the library words at
   replay, as the other draw state does. Caller holds objs_lock. */
static void command_state_record(CommandBufferRec* cb, u64 addr, const char* operation, NvnStateKind kind,
                                 const uint32_t* words, uint32_t count) {
    NvnOpBindState op = {(uint32_t)kind, count};
    memcpy(op.w, words, (size_t)count * sizeof words[0]);
    command_record(cb, addr, operation, NVN_OP_BIND_STATE, &op, sizeof op);
}
#define COMMAND_REFUSE_HANDLER(handler, operation, reason)                                      \
    static void h_command_buffer_##handler(CPU* c, u64 entry) {                                 \
        (void)entry;                                                                            \
        pm_lock(&objs_lock);                                                                    \
        if (command_buffer(c->x[0], operation)) command_refused(c, operation, reason);          \
        pm_unlock(&objs_lock);                                                                  \
    }
COMMAND_REFUSE_HANDLER(draw_arrays_indirect, "nvnCommandBufferDrawArraysIndirect",
                       "the GPU reads the record at draw time and the host replay has no such fetch")
COMMAND_REFUSE_HANDLER(draw_elements_indirect, "nvnCommandBufferDrawElementsIndirect",
                       "the GPU reads the record at draw time and the host replay has no such fetch")
COMMAND_REFUSE_HANDLER(multi_draw_arrays_indirect_count, "nvnCommandBufferMultiDrawArraysIndirectCount",
                       "the GPU reads the records and the count at draw time and the host replay has no such fetch")
COMMAND_REFUSE_HANDLER(multi_draw_elements_indirect_count, "nvnCommandBufferMultiDrawElementsIndirectCount",
                       "the GPU reads the records and the count at draw time and the host replay has no such fetch")
COMMAND_REFUSE_HANDLER(dispatch_compute_indirect, "nvnCommandBufferDispatchComputeIndirect",
                       "the host has no QMD path for a QMD that the GPU writes")
COMMAND_REFUSE_HANDLER(draw_texture, "nvnCommandBufferDrawTexture",
                       "it needs the driver program at device+0x1b00, which the host does not have")
COMMAND_REFUSE_HANDLER(draw_transform_feedback, "nvnCommandBufferDrawTransformFeedback",
                       "it reads byte counts that the GPU writes, and the renderer does not write them")
#define SEPARATE_BIND_REFUSAL "the host has only the combined texture table (128 slots), no separate texture or sampler table"
COMMAND_REFUSE_HANDLER(bind_separate_texture, "nvnCommandBufferBindSeparateTexture", SEPARATE_BIND_REFUSAL)
COMMAND_REFUSE_HANDLER(bind_separate_textures, "nvnCommandBufferBindSeparateTextures", SEPARATE_BIND_REFUSAL)
COMMAND_REFUSE_HANDLER(bind_separate_sampler, "nvnCommandBufferBindSeparateSampler", SEPARATE_BIND_REFUSAL)
COMMAND_REFUSE_HANDLER(bind_separate_samplers, "nvnCommandBufferBindSeparateSamplers", SEPARATE_BIND_REFUSAL)
COMMAND_REFUSE_HANDLER(set_program_subroutines, "nvnCommandBufferSetProgramSubroutines",
                       "the words of the subroutine rewrite (d855880) are not known")
COMMAND_REFUSE_HANDLER(set_shader_scratch_memory, "nvnCommandBufferSetShaderScratchMemory",
                       "two SDK constants of the 0x1E4 block are not known and the host has no shader scratch state")
#undef SEPARATE_BIND_REFUSAL
#undef COMMAND_REFUSE_HANDLER
/* U5: the memory accounting getters. The host keeps no cursor and no used or free count, and it never writes the command
   or control memory of the game. The library also subtracts a reserve at cb+0x74 from its command size, which the host
   record does not hold. */
#define COMMAND_MEMORY_REFUSAL "the host does not write the command or control memory of the game, so it has no used or free count"
#define COMMAND_REFUSE_VALUE_HANDLER(handler, operation, reason)                                   \
    static void h_command_buffer_##handler(CPU* c, u64 entry) {                                    \
        (void)entry;                                                                               \
        pm_lock(&objs_lock);                                                                       \
        CommandBufferRec* cb = command_buffer(c->x[0], operation);                                 \
        if (cb) command_refused(c, operation, reason);                                             \
        c->x[0] = 0;                                                                               \
        pm_unlock(&objs_lock);                                                                     \
    }
COMMAND_REFUSE_VALUE_HANDLER(get_command_memory_size, "nvnCommandBufferGetCommandMemorySize",
                             COMMAND_MEMORY_REFUSAL)
COMMAND_REFUSE_VALUE_HANDLER(get_command_memory_used, "nvnCommandBufferGetCommandMemoryUsed",
                             COMMAND_MEMORY_REFUSAL)
COMMAND_REFUSE_VALUE_HANDLER(get_command_memory_free, "nvnCommandBufferGetCommandMemoryFree",
                             COMMAND_MEMORY_REFUSAL)
COMMAND_REFUSE_VALUE_HANDLER(get_control_memory_used, "nvnCommandBufferGetControlMemoryUsed",
                             COMMAND_MEMORY_REFUSAL)
COMMAND_REFUSE_VALUE_HANDLER(get_control_memory_free, "nvnCommandBufferGetControlMemoryFree",
                             COMMAND_MEMORY_REFUSAL)
#undef COMMAND_MEMORY_REFUSAL
#undef COMMAND_REFUSE_VALUE_HANDLER
/* A command whose effect the host keeps with another rule: nothing is recorded, and the library words are not
   needed. Each handler has its own LOG_LIMIT counter. */
#define COMMAND_NO_OP_HANDLER(handler, operation, reason)                                      \
    static void h_command_buffer_##handler(CPU* c, u64 entry) {                                \
        (void)entry;                                                                            \
        pm_lock(&objs_lock);                                                                    \
        if (command_buffer(c->x[0], operation))                                                 \
            LOG_LIMIT(1, "nvn-native: %s is a logged no-op: %s\n", operation, reason);          \
        pm_unlock(&objs_lock);                                                                  \
    }
COMMAND_NO_OP_HANDLER(set_sample_mask, "nvnCommandBufferSetSampleMask", "the host renders one sample")
COMMAND_NO_OP_HANDLER(save_zcull_data, "nvnCommandBufferSaveZCullData", "the host has no ZCull")
COMMAND_NO_OP_HANDLER(restore_zcull_data, "nvnCommandBufferRestoreZCullData", "the host has no ZCull")
COMMAND_NO_OP_HANDLER(set_stencil_cull_criteria, "nvnCommandBufferSetStencilCullCriteria",
                      "the host has no stencil cull criteria")
COMMAND_NO_OP_HANDLER(set_color_reduction_enable, "nvnCommandBufferSetColorReductionEnable",
                      "the host keeps full color precision")
COMMAND_NO_OP_HANDLER(set_color_reduction_thresholds, "nvnCommandBufferSetColorReductionThresholds",
                      "the host keeps full color precision")
COMMAND_NO_OP_HANDLER(set_subpixel_precision_bias, "nvnCommandBufferSetSubpixelPrecisionBias",
                      "the host uses the device subpixel grid")
COMMAND_NO_OP_HANDLER(set_tiled_cache_action, "nvnCommandBufferSetTiledCacheAction",
                      "the host has no tiled cache, the hint does not change the image")
COMMAND_NO_OP_HANDLER(set_tiled_cache_tile_size, "nvnCommandBufferSetTiledCacheTileSize",
                      "the host has no tiled cache, the hint does not change the image")
COMMAND_NO_OP_HANDLER(set_command_memory_callback_enabled, "nvnCommandBufferSetCommandMemoryCallbackEnabled",
                      "the host never calls the memory callback")
#define DEBUG_MARKER_REASON "a debug marker for an external debugger, the image does not depend on it"
COMMAND_NO_OP_HANDLER(insert_debug_marker, "nvnCommandBufferInsertDebugMarker", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(insert_debug_marker_dynamic, "nvnCommandBufferInsertDebugMarkerDynamic", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(insert_debug_marker_static, "nvnCommandBufferInsertDebugMarkerStatic", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(push_debug_group, "nvnCommandBufferPushDebugGroup", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(push_debug_group_dynamic, "nvnCommandBufferPushDebugGroupDynamic", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(push_debug_group_static, "nvnCommandBufferPushDebugGroupStatic", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(pop_debug_group, "nvnCommandBufferPopDebugGroup", DEBUG_MARKER_REASON)
COMMAND_NO_OP_HANDLER(pop_debug_group_id, "nvnCommandBufferPopDebugGroupId", DEBUG_MARKER_REASON)
#undef DEBUG_MARKER_REASON
#undef COMMAND_NO_OP_HANDLER
/* nvnCommandBufferBindCoverageModulationTable(cb, table): only the disable form is a no-op. The host has no coverage
   modulation, and a non-identity table changes the image. */
static void h_command_buffer_bind_coverage_modulation_table(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindCoverageModulationTable";
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], name)) {
        if (c->x[1])
            command_refused(c, name, "the host has no coverage modulation, and the identity table is not verified");
        else
            LOG_LIMIT(1, "nvn-native: %s is a logged no-op: the table is disabled\n", name);
    }
    pm_unlock(&objs_lock);
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
/* One viewport of the 16, in the shape of the op. */
static void command_viewport(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t index, float x, float y,
                             float width, float height) {
    NvnOpViewport op = {index, x, y, width, height};
    command_record(cb, addr, operation, NVN_OP_SET_VIEWPORT, &op, sizeof op);
}
/* One scissor of the 16, in the shape of the op. */
static void command_scissor(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t index, int32_t x,
                            int32_t y, int32_t width, int32_t height) {
    NvnOpScissor op = {index, x, y, width, height};
    command_record(cb, addr, operation, NVN_OP_SET_SCISSOR, &op, sizeof op);
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
                float v[4];
                for (u32 k = 0; k < 4; k++) {
                    u32 bits = gr32(c->x[3] + (u64)i * 16 + k * 4);
                    memcpy(&v[k], &bits, sizeof bits);
                }
                command_viewport(cb, c->x[0], name, first + i, v[0], v[1], v[2], v[3]);
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
                command_scissor(cb, c->x[0], name, first + i, (int32_t)gr32(c->x[3] + (u64)i * 16),
                                (int32_t)gr32(c->x[3] + (u64)i * 16 + 4),
                                (int32_t)gr32(c->x[3] + (u64)i * 16 + 8),
                                (int32_t)gr32(c->x[3] + (u64)i * 16 + 12));
            }
    }
    pm_unlock(&objs_lock);
}
/* SetViewport takes four int32 values; the library converts them with scvtf. */
static void h_command_buffer_set_viewport(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetViewport";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb)
        command_viewport(cb, c->x[0], name, 0, (float)(int32_t)c->x[1], (float)(int32_t)c->x[2],
                         (float)(int32_t)c->x[3], (float)(int32_t)c->x[4]);
    pm_unlock(&objs_lock);
}
/* SetScissor is SetScissors(first 0, count 1) with the four int32 values on the stack. */
static void h_command_buffer_set_scissor(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetScissor";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb)
        command_scissor(cb, c->x[0], name, 0, (int32_t)c->x[1], (int32_t)c->x[2], (int32_t)c->x[3],
                        (int32_t)c->x[4]);
    pm_unlock(&objs_lock);
}
/* The size of one entry of a plural bind array: a buffer entry holds an address and a size, a texture entry one
   handle. */
#define COMMAND_BIND_BUFFER_BYTES 16u
#define COMMAND_BIND_TEXTURE_BYTES 8u
/* The range checks of a plural bind, done before any entry is read. 1 when the call can be recorded. */
static int command_bind_range_ok(CommandBufferRec* cb, const char* operation, uint32_t stage, uint32_t first,
                                 int32_t count, u64 array, uint32_t slot_max, uint32_t entry_bytes) {
    if (stage > NVN_BIND_STAGE_MAX) {
        command_dropped(cb, operation, "stage outside the bind stages", stage);
        return 0;
    }
    if ((u64)first + (u64)count > slot_max) {
        command_dropped(cb, operation, "range outside the bind slots", ((u64)first << 32) | (u32)count);
        return 0;
    }
    if (!native_guest_readable(array, (u64)count * entry_bytes)) {
        command_dropped(cb, operation, "unreadable array", array);
        return 0;
    }
    return 1;
}
/* nvnCommandBufferBindTexture(cb, stage, slot, handle). The library checks neither the stage nor the slot; the host
   refuses a value that its bind table cannot hold. */
static void h_command_buffer_bind_texture(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTexture";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpBindTexture op = {.stage = (u32)c->x[1], .slot = (u32)c->x[2], .handle = c->x[3], .plural = 0};
    if (!cb) {
    } else if (op.stage > NVN_BIND_STAGE_MAX)
        command_dropped(cb, name, "stage outside the bind stages", op.stage);
    else if (op.slot >= NVN_BIND_SLOT_MAX)
        command_dropped(cb, name, "slot outside the bind slots", op.slot);
    else
        command_record(cb, c->x[0], name, NVN_OP_BIND_TEXTURE, &op, sizeof op);
    pm_unlock(&objs_lock);
}
/* nvnCommandBufferBindTextures(cb, stage, first, count, handles): one combined texture bind per entry. The handles are
   read before the first record. */
static void h_command_buffer_bind_textures(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTextures";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    uint32_t stage = (u32)c->x[1], first = (u32)c->x[2];
    int32_t count = (int32_t)c->x[3];
    u64 array = c->x[4];
    if (cb && count > 0 &&
        command_bind_range_ok(cb, name, stage, first, count, array, NVN_BIND_SLOT_MAX, COMMAND_BIND_TEXTURE_BYTES)) {
        uint64_t handles[NVN_BIND_SLOT_MAX];
        for (uint32_t i = 0; i < (uint32_t)count; i++) handles[i] = gr64(array + (u64)i * COMMAND_BIND_TEXTURE_BYTES);
        for (uint32_t i = 0; i < (uint32_t)count; i++) {
            NvnOpBindTexture op = {.stage = stage, .slot = first + i, .handle = handles[i], .plural = 1};
            command_record(cb, c->x[0], name, NVN_OP_BIND_TEXTURE, &op, sizeof op);
        }
    }
    pm_unlock(&objs_lock);
}
/* One buffer bind: the stage, the slot limit and the bind table are the host's; the library checks none of them. */
static void command_bind_buffer(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                uint32_t stage, uint32_t slot, uint32_t slot_max, uint64_t buffer, uint64_t size,
                                uint32_t plural) {
    NvnOpBindBuffer op = {kind, stage, slot, plural, buffer, size};
    if (stage > NVN_BIND_STAGE_MAX)
        command_dropped(cb, operation, "stage outside the bind stages", stage);
    else if (slot >= slot_max)
        command_dropped(cb, operation, "slot outside the bind slots", slot);
    else
        command_record(cb, addr, operation, NVN_OP_BIND_BUFFER, &op, sizeof op);
}
/* A plural bind whose array entry is {u64 addr; u32 size; u32 pad}. */
static void command_bind_buffers_sized(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                       uint32_t stage, uint32_t first, int32_t count, u64 array, uint32_t slot_max,
                                       uint32_t plural) {
    if (!command_bind_range_ok(cb, operation, stage, first, count, array, slot_max, COMMAND_BIND_BUFFER_BYTES)) return;
    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        u64 at = array + (u64)i * COMMAND_BIND_BUFFER_BYTES;
        NvnOpBindBuffer op = {kind, stage, first + i, plural, gr64(at), (uint32_t)gr32(at + 8)};
        command_record(cb, addr, operation, NVN_OP_BIND_BUFFER, &op, sizeof op);
    }
}
/* A plural bind whose array entry is {u64 addr; u64 size}. The vertex and transform feedback binds have no stage. */
static void command_bind_buffers_full(CommandBufferRec* cb, u64 addr, const char* operation, NvnBindKind kind,
                                      uint32_t first, int32_t count, u64 array, uint32_t slot_max) {
    if (!command_bind_range_ok(cb, operation, 0, first, count, array, slot_max, COMMAND_BIND_BUFFER_BYTES)) return;
    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        u64 at = array + (u64)i * COMMAND_BIND_BUFFER_BYTES;
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
                                NVN_UNIFORM_SLOT_MAX, c->x[3], (uint32_t)c->x[4], 0);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_storage_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindStorageBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_STORAGE, (u32)c->x[1], (u32)c->x[2],
                                NVN_STORAGE_SLOT_MAX, c->x[3], (uint32_t)c->x[4], 0);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_vertex_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindVertexBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_VERTEX, 0, (u32)c->x[1], NVN_VERTEX_SLOT_MAX,
                                c->x[2], c->x[3], 0);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_transform_feedback_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindTransformFeedbackBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_buffer(cb, c->x[0], name, NVN_BIND_TRANSFORM_FEEDBACK, 0, (u32)c->x[1], NVN_XFB_SLOT_MAX,
                                c->x[2], c->x[3], 0);
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
                                   c->x[4], NVN_UNIFORM_SLOT_MAX, 0);
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
                                   c->x[4], NVN_STORAGE_SLOT_MAX, 1);
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
static const StateKindInfo state_kinds[NVN_STATE_OBJECT_COUNT] = {
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
static void h_blend_state_get_advanced_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    c->x[0] = nvn_blend_state_get_advanced_mode(w[0]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_get_advanced_overlap(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    c->x[0] = nvn_blend_state_get_advanced_overlap(w[0]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_get_advanced_premultiplied_src(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    c->x[0] = nvn_blend_state_get_advanced_premultiplied_src(w[0]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_get_advanced_normalized_dst(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    c->x[0] = nvn_blend_state_get_advanced_normalized_dst(w[0]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_set_advanced_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[0] = nvn_blend_state_set_advanced_mode(w[0], (u32)c->x[1]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_set_advanced_overlap(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[0] = nvn_blend_state_set_advanced_overlap(w[0], (u32)c->x[1]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_set_advanced_premultiplied_src(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[0] = nvn_blend_state_set_advanced_premultiplied_src(w[0], (u32)c->x[1]);
    pm_unlock(&objs_lock);
}
static void h_blend_state_set_advanced_normalized_dst(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    uint32_t* w = state_words(c->x[0], NVN_STATE_BLEND);
    w[0] = nvn_blend_state_set_advanced_normalized_dst(w[0], (u32)c->x[1]);
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

static void h_multisample_state_get_sample_locations_enable(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_MULTISAMPLE);
    c->x[0] = nvn_multisample_state_get_sample_locations_enable(w[0]);
    pm_unlock(&objs_lock);
}
static void h_multisample_state_get_sample_locations_grid_enable(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    const uint32_t* w = state_words(c->x[0], NVN_STATE_MULTISAMPLE);
    c->x[0] = nvn_multisample_state_get_sample_locations_grid_enable(w[0]);
    pm_unlock(&objs_lock);
}
static void h_multisample_state_get_sample_locations_grid(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnMultisampleStateGetSampleLocationsGrid";
    pm_lock(&objs_lock);
    if (guest_outputs_writable(c, name, 2)) {
        const uint32_t* w = state_words(c->x[0], NVN_STATE_MULTISAMPLE);
        uint32_t x = 0, y = 0;
        if (nvn_multisample_state_sample_grid(nvn_multisample_state_get_samples(w[0]),
                                              nvn_multisample_state_get_raster_samples(w[0]), &x, &y)) {
            gw32(c->x[1], x);
            gw32(c->x[2], y);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_multisample_state_set_sample_locations(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnMultisampleStateSetSampleLocations";
    const uint32_t count = (u32)c->x[2];
    pm_lock(&objs_lock);
    const uint32_t index = (u32)c->x[1];
    if (!count) {
        pm_unlock(&objs_lock);
        return;
    }
    /* The library writes past its record when index + count leaves 16. The host refuses that write. */
    if (!native_guest_readable(c->x[3], (u64)count * 2u * sizeof(uint32_t)) ||
        !nvn_multisample_locations_fit(index, count)) {
        LOG("nvn-native: %s skipped index=%u count=%u\n", name, index, count);
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t* w = state_words(c->x[0], NVN_STATE_MULTISAMPLE);
    uint8_t* bytes = (uint8_t*)w + 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t xb = gr32(c->x[3] + (u64)i * 2u * sizeof(uint32_t));
        uint32_t yb = gr32(c->x[3] + (u64)i * 2u * sizeof(uint32_t) + sizeof(uint32_t));
        float x, y;
        memcpy(&x, &xb, sizeof x);
        memcpy(&y, &yb, sizeof y);
        bytes[index + i] = nvn_multisample_sample_location(x, y);
    }
    pm_unlock(&objs_lock);
}

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
                op.word_count = nvn_bind_program_words(program->stage_mask, program->words, program->word_count,
                                                       op.words);
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

/* ClearColor, ClearColori and ClearColorui are one library body: the float, int and uint words go out unchanged. */
static void command_clear_color(CPU* c, const char* name) {
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
static void h_command_buffer_clear_color(CPU* c, u64 entry) {
    (void)entry;
    command_clear_color(c, "nvnCommandBufferClearColor");
}
static void h_command_buffer_clear_colori(CPU* c, u64 entry) {
    (void)entry;
    command_clear_color(c, "nvnCommandBufferClearColori");
}
static void h_command_buffer_clear_colorui(CPU* c, u64 entry) {
    (void)entry;
    command_clear_color(c, "nvnCommandBufferClearColorui");
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
/* CopyCommands: a deep copy of each source list into the open list, as the library copies command memory. A CALL op
   keeps its reference model and takes one more, so the destination does not depend on the source after the call. */
static void command_copy_commands(CommandBufferRec* cb, const NvnOpList* src, const char* operation) {
    if (!cb->recording) {
        command_dropped(cb, operation, "outside a recording", src->ops);
        return;
    }
    uint64_t failed = 0;
    if (!nvn_oplist_copy(&cb->open, src, &lists, &failed)) {
        if (failed) command_dropped(cb, operation, "handle is not a live list", failed);
        else command_dropped(cb, operation, "the command list is full", src->ops);
    }
}
static void h_command_buffer_copy_commands(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferCopyCommands";
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
                uint64_t handle = gr64(c->x[2] + (u64)i * 8);
                const NvnOpList* src = nvn_list_find(&lists, handle);
                if (!src) {
                    command_dropped(cb, name, "handle is not a live list", handle);
                    continue;
                }
                command_copy_commands(cb, src, name);
                if (!cb->recording) break;
            }
    }
    pm_unlock(&objs_lock);
}
/* CopyBufferToBuffer(cb, src, dst, size) and ClearBuffer(cb, dst, size, value): the words are built at replay. */
static void h_command_buffer_copy_buffer_to_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferCopyBufferToBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpCopyBuffer op = {c->x[1], c->x[2], c->x[3]};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_COPY_BUFFER, &op, sizeof op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_clear_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferClearBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpClearBuffer op = {c->x[1], c->x[2], (u32)c->x[3], 0};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_CLEAR_BUFFER, &op, sizeof op);
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
/* The log of a refused copy region: the function name and the texture address, as decision 5 asks. */
typedef struct {
    const char* operation;
    uint64_t texture;
} CopyRegionFault;
static void copy_region_fault(void* ctx, const char* what, uint64_t value) {
    (void)value;
    const CopyRegionFault* f = ctx;
    LOG_LIMIT(COMMAND_LOG_LIMIT, "nvn-native: %s region refused: %s texture=%llx\n", f->operation, what,
              (unsigned long long)f->texture);
}
/* CopyBufferToTexture and CopyTextureToBuffer share the record. The region and the copy strides are read at call
   time; the words are built at replay. A zero height writes no copy in the library, so no op is recorded. */
static void command_copy_buffer_texture(CPU* c, const char* operation, int to_texture) {
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], operation);
    NvnOpCopyBufferTexture op = {0};
    op.to_texture = to_texture ? 1u : 0u;
    uint64_t texture_addr = to_texture ? c->x[2] : c->x[1];
    uint64_t view_addr = to_texture ? c->x[3] : c->x[2];
    uint64_t region_addr = to_texture ? c->x[4] : c->x[3];
    op.buffer = to_texture ? c->x[1] : c->x[4];
    op.flags = (uint32_t)c->x[5];
    if (!cb) {
        pm_unlock(&objs_lock);
        return;
    }
    if (!texture_addr) {
        command_dropped(cb, operation, "null texture", 0);
        goto done;
    }
    if (!command_target(texture_addr, view_addr, &op.target, cb, operation)) goto done;
    if (!native_guest_readable(region_addr, sizeof op.region)) {
        command_dropped(cb, operation, "unreadable region", region_addr);
        goto done;
    }
    for (u32 i = 0; i < 6; i++) op.region[i] = gr32(region_addr + (u64)i * 4);
    const TextureRec* texture = known_record(texture_addr, NVN_OBJ_TEXTURE);
    if (!texture) goto done;
    uint32_t target = op.target.has_view && (op.target.view.flags & NVN_VIEW_TARGET) ? op.target.view.target
                                                                                    : texture->builder.target;
    int one_d_array = target == NVN_TEXTURE_TARGET_1D_ARRAY;
    uint32_t width = op.region[3];
    uint32_t height = one_d_array ? 1u : op.region[4];
    uint32_t layers = one_d_array ? op.region[4] : op.region[5];
    if (height == 0) goto done;
    if (!width || !layers) {
        command_dropped(cb, operation, "empty region", texture->address);
        goto done;
    }
    CopyRegionFault fault = {operation, texture->address};
    NvnRtCopyEnv env = {{&fault, NULL, copy_region_fault}, NULL};
    int nothing = 0;
    if (!nvn_rt_copy_dma_check(&env, texture, op.target.has_view ? &op.target.view : NULL, op.buffer, op.region,
                               cb->copy_row_stride, cb->copy_image_stride, to_texture, &nothing)) {
        if (!nothing) {
            cb->dropped_ops++;
            __atomic_fetch_add(&dropped_ops, 1, __ATOMIC_RELAXED);
        }
        goto done;
    }
    op.row_stride = cb->copy_row_stride;
    op.image_stride = cb->copy_image_stride;
    command_record(cb, c->x[0], operation, NVN_OP_COPY_BUFFER_TEXTURE, &op, sizeof op);
done:
    pm_unlock(&objs_lock);
}
static void h_command_buffer_copy_buffer_to_texture(CPU* c, u64 entry) {
    (void)entry;
    command_copy_buffer_texture(c, "nvnCommandBufferCopyBufferToTexture", 1);
}
static void h_command_buffer_copy_texture_to_buffer(CPU* c, u64 entry) {
    (void)entry;
    command_copy_buffer_texture(c, "nvnCommandBufferCopyTextureToBuffer", 0);
}
/* Downsample and the tiled forms share the record. The view forms take the views; the plain forms pass NULL. */
static void command_downsample(CPU* c, const char* operation, int tiled, int has_views) {
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], operation);
    NvnOpDownsample op = {0};
    op.tiled = tiled ? 1u : 0u;
    uint64_t src = c->x[1];
    uint64_t src_view = has_views ? c->x[2] : 0;
    uint64_t dst = has_views ? c->x[3] : c->x[2];
    uint64_t dst_view = has_views ? c->x[4] : 0;
    if (!cb) {
    } else if (!src || !dst) {
        command_dropped(cb, operation, "null texture", src ? dst : src);
    } else if (!command_target(src, src_view, &op.src, cb, operation)) {
    } else if (!command_target(dst, dst_view, &op.dst, cb, operation)) {
    } else {
        command_record(cb, c->x[0], operation, NVN_OP_DOWNSAMPLE, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_downsample(CPU* c, u64 entry) {
    (void)entry;
    command_downsample(c, "nvnCommandBufferDownsample", 0, 0);
}
static void h_command_buffer_downsample_texture_view(CPU* c, u64 entry) {
    (void)entry;
    command_downsample(c, "nvnCommandBufferDownsampleTextureView", 0, 1);
}
static void h_command_buffer_tiled_downsample(CPU* c, u64 entry) {
    (void)entry;
    command_downsample(c, "nvnCommandBufferTiledDownsample", 1, 0);
}
static void h_command_buffer_tiled_downsample_texture_view(CPU* c, u64 entry) {
    (void)entry;
    command_downsample(c, "nvnCommandBufferTiledDownsampleTextureView", 1, 1);
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
/* ReportValue(cb, value, address): any value and address are accepted, as in the library. */
static void h_command_buffer_report_value(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferReportValue";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpReportValue op = {(uint32_t)c->x[1], 0, c->x[2]};
    if (cb) command_record(cb, c->x[0], name, NVN_OP_REPORT_VALUE, &op, sizeof op);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_signal_event(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSignalEvent";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    /* (cb, event, mode, location, flags, value): the library reads location in w3, flags in w4, value in w5. */
    NvnOpSignalEvent op = {c->x[1], 0, (uint32_t)c->x[2], (uint32_t)c->x[5], (uint32_t)c->x[3]};
    if (!cb) {
    } else if (!command_object(op.event, NVN_OBJ_EVENT, &op.generation)) {
        command_dropped(cb, name, "unknown event", op.event);
    } else {
        command_record(cb, c->x[0], name, NVN_OP_SIGNAL_EVENT, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_wait_event(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferWaitEvent";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpWaitEvent op = {c->x[1], 0, (uint32_t)c->x[2], (uint32_t)c->x[3], 0};
    if (!cb) {
    } else if (!command_object(op.event, NVN_OBJ_EVENT, &op.generation)) {
        command_dropped(cb, name, "unknown event", op.event);
    } else {
        command_record(cb, c->x[0], name, NVN_OP_WAIT_EVENT, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_fence_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferFenceSync";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpFenceSync op = {c->x[1], 0, (uint32_t)c->x[2], (uint32_t)c->x[3] & NVN_QUEUE_FENCE_FLUSH_FOR_CPU};
    if (!cb) {
    } else if (!command_object(op.sync, NVN_OBJ_SYNC, &op.generation)) {
        command_dropped(cb, name, "unknown sync", op.sync);
    } else if (op.condition != NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE) {
        cb->dropped_ops++;
        __atomic_fetch_add(&dropped_ops, 1, __ATOMIC_RELAXED);
        COMMAND_LOG("nvn-native: %s condition=%u is not supported\n", name, op.condition);
    } else {
        if (c->x[3] & ~NVN_QUEUE_FENCE_FLUSH_FOR_CPU)
            LOG_LIMIT(8, "nvn-native: %s flags=%llx has bits other than FLUSH_FOR_CPU\n",
                      name, (unsigned long long)c->x[3]);
        command_record(cb, c->x[0], name, NVN_OP_FENCE_SYNC, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_wait_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferWaitSync";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    NvnOpWaitSync op = {c->x[1], 0, 0};
    if (!cb) {
    } else if (!command_object(op.sync, NVN_OBJ_SYNC, &op.generation)) {
        command_dropped(cb, name, "unknown sync", op.sync);
    } else {
        command_record(cb, c->x[0], name, NVN_OP_WAIT_SYNC, &op, sizeof op);
    }
    pm_unlock(&objs_lock);
}
/* The library method 0x47F resolves the depth compression of the bound target in place. Host depth images have no
   compression, so the contents are already resolved and the host adds no barrier. */
static void h_command_buffer_resolve_depth_buffer(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], "nvnCommandBufferResolveDepthBuffer"))
        LOG_LIMIT(1, "nvn-native: ResolveDepthBuffer: host depth images have no compression, no-op\n");
    pm_unlock(&objs_lock);
}
/* The library method 0x3DE makes the target contents undefined. Keeping the contents is one allowed result. */
static void h_command_buffer_discard_color(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], "nvnCommandBufferDiscardColor"))
        LOG_LIMIT(1, "nvn-native: nvnCommandBufferDiscardColor index=%u is a no-op (the contents become undefined)\n",
                  (u32)c->x[1] & 7u);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_discard_depth_stencil(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], "nvnCommandBufferDiscardDepthStencil"))
        LOG_LIMIT(1, "nvn-native: nvnCommandBufferDiscardDepthStencil is a no-op (the contents become undefined)\n");
    pm_unlock(&objs_lock);
}
/* UpdateUniformBuffer(cb, address, size, offset, bytes, data): the library copies (bytes >> 2) words from the guest at
   record time. Data over one op payload goes into several ops in order, each with its own offset. */
static void h_command_buffer_update_uniform_buffer(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferUpdateUniformBuffer";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (!cb) {
        pm_unlock(&objs_lock);
        return;
    }
    const u64 address = c->x[1];
    const u32 size = (u32)c->x[2];
    const u32 offset = (u32)c->x[3];
    const u32 words = (u32)((c->x[4] >> 2) & 0x3FFFFFFFu);
    const u64 data = c->x[5];
    if (words && !native_guest_readable(data, (u64)words * 4u)) {
        command_dropped(cb, name, "unreadable data", data);
        pm_unlock(&objs_lock);
        return;
    }
    const u32 size256 = nvn_update_uniform_size256(size);
    uint8_t payload[NVN_OP_MAX_PAYLOAD];
    u32 done = 0;
    do {
        u32 n = words - done;
        if (n > NVN_UPDATE_UNIFORM_DATA_WORDS) n = NVN_UPDATE_UNIFORM_DATA_WORDS;
        NvnOpUpdateUniform header = {address, size256, offset + 4u * done, n, 0};
        memcpy(payload, &header, sizeof header);
        for (u32 i = 0; i < n; i++) {
            u32 word = gr32(data + (u64)(done + i) * 4u);
            memcpy(payload + sizeof header + (size_t)i * 4u, &word, sizeof word);
        }
        command_record(cb, c->x[0], name, NVN_OP_UPDATE_UNIFORM, payload, (u32)sizeof header + n * 4u);
        done += n;
    } while (done < words);
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
static void h_command_buffer_pause_transform_feedback(CPU* c, u64 entry) {
    (void)entry;
    command_xfb(c, "nvnCommandBufferPauseTransformFeedback", NVN_XFB_PAUSE);
}
static void h_command_buffer_resume_transform_feedback(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferResumeTransformFeedback";
    if (c->x[1]) {
        pm_lock(&objs_lock);
        if (command_buffer(c->x[0], name))
            command_refused(c, name, "it reads the byte counts that the GPU writes");
        pm_unlock(&objs_lock);
        return;
    }
    command_xfb(c, name, NVN_XFB_RESUME);
}
/* The draw state of the U4 tessellation, primitive restart and render enable setters. The host stores the
   arguments and the builder in nvn_params.c makes the library words at replay. */
static void h_command_buffer_set_patch_size(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetPatchSize";
    uint32_t patches = (uint32_t)c->x[1];
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_state_record(cb, c->x[0], name, NVN_STATE_PATCH_SIZE, &patches, 1);
    pm_unlock(&objs_lock);
}
static void command_set_tessellation_levels(CPU* c, const char* name, NvnStateKind kind, uint32_t count) {
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        if (!native_guest_readable(c->x[1], (u64)count * sizeof(uint32_t)))
            command_refused(c, name, "the level array is unreadable");
        else {
            uint32_t bits[NVN_TESS_OUTER_LEVEL_COUNT];
            for (uint32_t i = 0; i < count; i++) bits[i] = gr32(c->x[1] + (u64)i * 4u);
            command_state_record(cb, c->x[0], name, kind, bits, count);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_inner_tessellation_levels(CPU* c, u64 entry) {
    (void)entry;
    command_set_tessellation_levels(c, "nvnCommandBufferSetInnerTessellationLevels", NVN_STATE_INNER_TESSELLATION,
                                    NVN_TESS_INNER_LEVEL_COUNT);
}
static void h_command_buffer_set_outer_tessellation_levels(CPU* c, u64 entry) {
    (void)entry;
    command_set_tessellation_levels(c, "nvnCommandBufferSetOuterTessellationLevels", NVN_STATE_OUTER_TESSELLATION,
                                    NVN_TESS_OUTER_LEVEL_COUNT);
}
static void h_command_buffer_set_primitive_restart(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetPrimitiveRestart";
    uint32_t words[2] = {(uint32_t)c->x[1], (uint32_t)c->x[2]};
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_state_record(cb, c->x[0], name, NVN_STATE_PRIMITIVE_RESTART, words, 2);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_render_enable(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetRenderEnable";
    uint32_t words[NVN_RENDER_ENABLE_STATE_WORDS] = {NVN_RENDER_ENABLE_FORM_VALUE, (uint32_t)c->x[1], 0, 0};
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_state_record(cb, c->x[0], name, NVN_STATE_RENDER_ENABLE, words, NVN_RENDER_ENABLE_STATE_WORDS);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_render_enable_conditional(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetRenderEnableConditional";
    uint32_t condition = (uint32_t)c->x[1];
    uint64_t sync = c->x[2];
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        if (condition >= NVN_RENDER_ENABLE_CONDITION_COUNT) {
            LOG_LIMIT(1, "nvn-native: %s condition=%u is a logged no-op: the library writes nothing\n", name,
                      condition);
        } else {
            uint32_t words[NVN_RENDER_ENABLE_STATE_WORDS] = {NVN_RENDER_ENABLE_FORM_CONDITIONAL, condition,
                                                             (uint32_t)(sync >> 32), (uint32_t)sync};
            command_state_record(cb, c->x[0], name, NVN_STATE_RENDER_ENABLE, words, NVN_RENDER_ENABLE_STATE_WORDS);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_viewport_swizzles(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetViewportSwizzles";
    int32_t count = (int32_t)c->x[2];
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) {
        if (count < 0) {
            command_refused(c, name, "the entry count is negative");
        } else if (!count) {
            LOG_LIMIT(1, "nvn-native: %s with no entries is a logged no-op\n", name);
        } else if (!native_guest_readable(c->x[3], (u64)count * NVN_VIEWPORT_SWIZZLE_ENTRY_BYTES)) {
            command_refused(c, name, "the entry array is unreadable");
        } else {
            int identity = 1;
            for (int32_t i = 0; i < count && identity; i++) {
                uint32_t entry[4];
                for (uint32_t k = 0; k < 4; k++)
                    entry[k] = gr32(c->x[3] + (u64)i * NVN_VIEWPORT_SWIZZLE_ENTRY_BYTES + (u64)k * 4u);
                uint32_t word = nvn_viewport_swizzle_word(entry);
                identity = nvn_viewport_swizzles_identity(&word, 1);
            }
            if (identity) {
                LOG_LIMIT(1, "nvn-native: %s entries are all the identity, a logged no-op\n", name);
            } else {
                command_refused(c, name, "an entry is not the identity");
            }
        }
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_conservative_raster_enable(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetConservativeRasterEnable";
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], name)) {
        if ((c->x[1] & 0xFFu) != 0)
            command_refused(c, name, "the host has no conservative rasterization");
        else
            LOG_LIMIT(1, "nvn-native: %s is a logged no-op: the host has no conservative rasterization, which is "
                         "already disabled\n", name);
    }
    pm_unlock(&objs_lock);
}
static void h_command_buffer_set_conservative_raster_dilate(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferSetConservativeRasterDilate";
    uint32_t bits = SNX_VS(c->v[0], 0);
    float dilate;
    memcpy(&dilate, &bits, sizeof dilate);
    pm_lock(&objs_lock);
    if (command_buffer(c->x[0], name)) {
        if (nvn_conservative_raster_dilate_level(dilate) != 0)
            command_refused(c, name, "the host has no conservative rasterization");
        else
            LOG_LIMIT(1, "nvn-native: %s is a logged no-op: the host has no conservative rasterization, and the "
                         "level is 0\n", name);
    }
    pm_unlock(&objs_lock);
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
/* One BindImage or BindImages call. A NULL host_handles reads the guest array at guest_handles. plural keeps the
   library's mode 5 packet form of BindImages. */
static void command_bind_image(CommandBufferRec* cb, u64 addr, const char* operation, uint32_t stage, uint32_t first,
                               int32_t count, u64 guest_handles, const uint64_t* host_handles, uint32_t plural) {
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
    NvnOpBindImage op = {.stage = stage, .first = first, .count = n, .plural = plural};
    for (uint32_t i = 0; i < n; i++)
        op.handle[i] = host_handles ? host_handles[i] : gr64(guest_handles + (u64)i * 8);
    command_record(cb, addr, operation, NVN_OP_BIND_IMAGE, &op, sizeof op);
}
static void h_command_buffer_bind_image(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindImage";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb) command_bind_image(cb, c->x[0], name, (uint32_t)c->x[1], (uint32_t)c->x[2], 1, 0, &c->x[3], 0);
    pm_unlock(&objs_lock);
}
static void h_command_buffer_bind_images(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnCommandBufferBindImages";
    pm_lock(&objs_lock);
    CommandBufferRec* cb = command_buffer(c->x[0], name);
    if (cb)
        command_bind_image(cb, c->x[0], name, (uint32_t)c->x[1], (uint32_t)c->x[2], (int32_t)c->x[3], c->x[4], NULL,
                           1);
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
static void replay_host_channel_method(void* ctx, uint32_t sub, uint32_t m, uint32_t v) {
    gpu_channel_method(((NvnNativeReplayHost*)ctx)->channel, sub, m, v, REPLAY_METHOD_LAST);
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
static int replay_host_bind_texture_store(NvnNativeReplayHost* host, uint32_t stage, uint32_t slot, uint64_t handle,
                                          uint32_t plural) {
    if (stage > NVN_BIND_STAGE_MAX || slot >= NVN_BIND_SLOT_MAX) return 0;
    host->texture_handle[stage][slot] = handle;
    host->texture_bound[stage][slot] = 1;
    host->texture_plural[stage][slot] = (uint8_t)plural;
    host->texture_dirty[stage][slot] = NVN_BIND_DIRTY;
    return 1;
}
static int replay_host_bind_texture(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle) {
    return replay_host_bind_texture_store(ctx, stage, slot, handle, 0);
}
static int replay_host_bind_texture_plural(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle) {
    return replay_host_bind_texture_store(ctx, stage, slot, handle, 1);
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
    entry->plural = (uint8_t)(op->plural != 0);
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
    "color", "depth stencil", "blend", "polygon", "multisample", "channel mask",
    "patch size", "inner tessellation", "outer tessellation", "primitive restart", "render enable"};
/* One refusal line: the group, stage, slot and first builder input word. The data stays for a later bind. */
static void replay_bind_refuse(NvnNativeReplayHost* host, const char* group, int32_t stage, int32_t slot,
                               uint32_t word) {
    host->bind_refusals++;
    LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: bind %s refused stage=%d slot=%d word=%08x\n", group, stage, slot, word);
}
/* One method of a host built stream, routed as gpu.c's method_call does: a puller method (below 0x40) of any
   subchannel goes to the puller, and the other methods of subchannel 0 go to the 3D engine. */
static void replay_stream_method(GpuChannel* ch, uint32_t subchannel, uint32_t method, uint32_t value, int last) {
    if (subchannel || method < PULLER_NON_PULLER_METHODS)
        gpu_channel_method(ch, subchannel, method, value, last);
    else
        maxwell3d_method(ch, method, value, last);
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
            replay_stream_method(host->channel, stream.subchannel, method, value, last);
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
    case NVN_STATE_PATCH_SIZE: count = nvn_patch_size_words(w[0], out); break;
    case NVN_STATE_INNER_TESSELLATION: count = nvn_inner_tessellation_levels_words(w, out); break;
    case NVN_STATE_OUTER_TESSELLATION: count = nvn_outer_tessellation_levels_words(w, out); break;
    case NVN_STATE_PRIMITIVE_RESTART: count = nvn_primitive_restart_words(w[0], w[1], out); break;
    case NVN_STATE_RENDER_ENABLE:
        count = w[0] == NVN_RENDER_ENABLE_FORM_VALUE
                    ? nvn_render_enable_words(w[1], out)
                    : nvn_render_enable_conditional_words(w[1], ((uint64_t)w[2] << 32) | w[3], out);
        break;
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
/* A size 0 storage bind is always sent: the builder writes its parameter word and a zero data block. A slot that came
   from BindStorageBuffers keeps the plural mode 3 form. */
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
            uint32_t out[NVN_BIND_STORAGE_BUFFERS_WORDS_MAX];
            uint32_t words;
            if (entry->plural) {
                const uint64_t range[2] = {entry->addr, entry->size};
                words = nvn_bind_storage_buffers_words(stage, slot, 1, range, out);
            } else {
                words = nvn_bind_storage_buffer_words(stage, slot, entry->addr, (uint32_t)entry->size, out);
            }
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
            uint32_t out[NVN_BIND_TEXTURES_WORDS_MAX];
            uint32_t words = host->texture_plural[stage][slot]
                                 ? nvn_bind_textures_words(stage, slot, 1, &handle, NVN_TEXTURE_DESCRIPTOR_FLAG, out)
                                 : nvn_bind_texture_words(stage, slot, handle, NVN_TEXTURE_DESCRIPTOR_FLAG, out);
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
    replay_bind_send_state(host, NVN_STATE_PATCH_SIZE);
    replay_bind_send_state(host, NVN_STATE_INNER_TESSELLATION);
    replay_bind_send_state(host, NVN_STATE_OUTER_TESSELLATION);
    replay_bind_send_state(host, NVN_STATE_PRIMITIVE_RESTART);
    replay_bind_send_state(host, NVN_STATE_RENDER_ENABLE);
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

/* Allocates the arena at the first dispatch. It takes objs_lock for the shared state of the arena. */
static int dispatch_scratch_get(uint64_t* gpu) {
    pm_lock(&objs_lock);
    if (!dispatch_scratch_ready && !dispatch_scratch_failed) {
        uint64_t cpu = kmem_alloc(NVN_DISPATCH_SCRATCH_BYTES, MS_STATIC, PERM_RW);
        uint64_t address = nvn_gpu_va_alloc(NVN_DISPATCH_SCRATCH_BYTES, NVN_POOL_PAGE_SIZE);
        if (!cpu || !address || !nvn_gpu_map_range(address, cpu, NVN_DISPATCH_SCRATCH_BYTES)) {
            if (address) nvn_gpu_va_free(address, NVN_DISPATCH_SCRATCH_BYTES);
            dispatch_scratch_failed = 1;
            LOG_LIMIT(REPLAY_LOG_LIMIT,
                      "nvn-native: dispatch arena of %u bytes cannot be allocated and mapped\n",
                      (unsigned)NVN_DISPATCH_SCRATCH_BYTES);
        } else {
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
    int emitted = nvn_rt_emit(&env, color, op->count, depth.texture ? &depth : NULL, host->clip_limit);
    if (emitted && depth.texture)
        LOG_LIMIT(1, "nvn-native: depth target bound, the dependent block (0x1F8, 0x572) is not written\n");
    return emitted;
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

static int replay_host_report_value(NvnNativeReplayHost* host, const NvnOpReportValue* op) {
    uint32_t words[NVN_REPORT_VALUE_WORDS_MAX];
    uint32_t count = nvn_report_value_words(op->value, op->address, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "report value", -1, -1, op->value);
        return 1;
    }
    return 1;
}

/* The value word of a live event, or 0 after a log line. Caller holds objs_lock. */
static u64 replay_event_word(u64 event, uint32_t generation, const char* operation) {
    NvnObj* o = nvn_objtab_valid(&objs, event, generation) ? nvn_objtab_find(&objs, event) : NULL;
    if (!o || o->kind != NVN_OBJ_EVENT) {
        LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: %s event=%llx generation=%u is gone\n", operation,
                  (unsigned long long)event, generation);
        return 0;
    }
    return event_value_address(o->data, operation, event);
}

/* SignalEvent: the host writes the CPU word. The location only names a stage in the library; the replay is serial. */
static int replay_host_signal_event(NvnNativeReplayHost* host, const NvnOpSignalEvent* op) {
    (void)host;
    pm_lock(&objs_lock);
    const u64 word = replay_event_word(op->event, op->generation, "nvnCommandBufferSignalEvent");
    if (word) {
        uint32_t* at = (uint32_t*)GP(word);
        const uint32_t old = __atomic_load_n(at, __ATOMIC_ACQUIRE);
        __atomic_store_n(at, nvn_event_signal_value(op->mode, old, op->value), __ATOMIC_RELEASE);
    }
    pm_unlock(&objs_lock);
    return 1;
}

static int replay_host_wait_event(NvnNativeReplayHost* host, const NvnOpWaitEvent* op) {
    (void)host;
    pm_lock(&objs_lock);
    const u64 word = replay_event_word(op->event, op->generation, "nvnCommandBufferWaitEvent");
    if (word) {
        const uint32_t value = __atomic_load_n((const uint32_t*)GP(word), __ATOMIC_ACQUIRE);
        if (!nvn_event_wait_met(op->mode, value, op->value)) {
            frame_unmet_event_waits++;
            LOG_LIMIT(REPLAY_LOG_LIMIT,
                      "nvn-native: nvnCommandBufferWaitEvent event=%llx mode=%u word=%08x value=%08x is not met, the "
                      "replay goes on\n",
                      (unsigned long long)op->event, op->mode, value, op->value);
            /* The host has one GPU thread, so a wait for a later CPU or queue signal can never end, and the library
               has no timeout. The frame line counts the misses. */
        }
    }
    pm_unlock(&objs_lock);
    return 1;
}

static int replay_host_bind_image(NvnNativeReplayHost* host, const NvnOpBindImage* op) {
    uint32_t words[NVN_BIND_IMAGE_WORDS_MAX];
    uint32_t count = nvn_bind_image_words(op->stage, op->first, op->count, op->handle,
                                          NVN_TEXTURE_DESCRIPTOR_FLAG, (int)op->plural, words);
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

/* CopyBufferToTexture and CopyTextureToBuffer: one texture record and the buffer range (nvn_rt.c, DMA and 2D
   routes). The record is copied under the lock, so that the words are made without it. */
static int replay_host_copy_buffer_texture(NvnNativeReplayHost* host, const NvnOpCopyBufferTexture* op) {
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
        replay_bind_refuse(host, "copy buffer texture", -1, -1, (uint32_t)op->target.texture);
        return 1;
    }
    NvnRtCopyEnv env = {{host, replay_host_method, replay_host_rt_fault}, copy_format_word_cb};
    const NvnTextureView* view = op->target.has_view ? &op->target.view : NULL;
    if (op->to_texture && (op->flags & NVN_COPY_FLAG_ENGINE_2D)) {
        uint32_t words[NVN_RT_COPY_2D_WORDS_MAX];
        uint32_t count = nvn_rt_copy_buffer_texture_2d_words(&env, &texture, view, op->buffer, op->region,
                                                             op->flags, op->row_stride, op->image_stride, words);
        if (!count || !replay_bind_send(host, words, count))
            replay_bind_refuse(host, "copy buffer texture", -1, -1, (uint32_t)op->target.texture);
        return 1;
    }
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    int nothing = 0;
    if (!nvn_rt_copy_dma_check(&env, &texture, view, op->buffer, op->region, op->row_stride, op->image_stride,
                               op->to_texture, &nothing)) {
        if (!nothing) replay_bind_refuse(host, "copy buffer texture", -1, -1, (uint32_t)op->target.texture);
        return 1;
    }
    if (nothing) return 1;
    uint32_t count = nvn_rt_copy_dma_words(&env, &texture, view, op->buffer, op->region, op->flags, op->row_stride,
                                           op->image_stride, op->to_texture, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    if (!count || launches == 0) {
        replay_bind_refuse(host, "copy buffer texture", -1, -1, (uint32_t)op->target.texture);
        return 1;
    }
    for (uint32_t i = 0; i < launches; i++) {
        if (i > 0)
            count = nvn_rt_copy_dma_words(&env, &texture, view, op->buffer, op->region, op->flags, op->row_stride,
                                          op->image_stride, op->to_texture, i, &launches, words,
                                          NVN_RT_DMA_LAUNCH_WORDS);
        if (!count || !replay_bind_send(host, words, count)) {
            replay_bind_refuse(host, "copy buffer texture", -1, -1, (uint32_t)op->target.texture);
            return 1;
        }
    }
    return 1;
}

/* Downsample and the tiled form: one 2D block from the source level to the destination level. */
static int replay_host_downsample(NvnNativeReplayHost* host, const NvnOpDownsample* op) {
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
        replay_bind_refuse(host, "downsample", -1, -1, (uint32_t)op->src.texture);
        return 1;
    }
    uint32_t words[NVN_RT_DOWNSAMPLE_WORDS_MAX];
    int ratio_not_two = 0;
    NvnRtCopyEnv env = {{host, replay_host_method, replay_host_rt_fault}, copy_format_word_cb};
    uint32_t count = nvn_rt_downsample_words(&env, &src, op->src.has_view ? &op->src.view : NULL, &dst,
                                             op->dst.has_view ? &op->dst.view : NULL, (int)op->tiled,
                                             &ratio_not_two, words);
    if (ratio_not_two)
        LOG_LIMIT(REPLAY_LOG_LIMIT, "nvn-native: TiledDownsample: ratio is not 2, linear blit result\n");
    if (!count || !replay_bind_send(host, words, count))
        replay_bind_refuse(host, "downsample", -1, -1, (uint32_t)op->src.texture);
    return 1;
}

/* The words of one DMA send: a larger copy op is split here, not in the op. */
#define NVN_REPLAY_SEND_WORDS_MAX 256u
/* The DMA words of CopyBufferToBuffer and ClearBuffer, and the 3D words of UpdateUniformBuffer (analyst-u3-A-1.md). */
static int replay_host_copy_buffer(NvnNativeReplayHost* host, const NvnOpCopyBuffer* op) {
    uint32_t words[NVN_REPLAY_SEND_WORDS_MAX];
    uint64_t bytes_per_send = (uint64_t)NVN_COPY_BUFFER_CHUNK_BYTES *
                              (NVN_REPLAY_SEND_WORDS_MAX / NVN_COPY_BUFFER_WORDS_PER_CHUNK);
    uint64_t done = 0;
    do {
        uint64_t part = op->size - done;
        if (part > bytes_per_send) part = bytes_per_send;
        uint32_t count = nvn_copy_buffer_words(op->src + done, op->dst + done, part, words,
                                                NVN_REPLAY_SEND_WORDS_MAX);
        if (count && !replay_bind_send(host, words, count)) {
            replay_bind_refuse(host, "copy buffer", -1, -1, (uint32_t)op->src);
            return 1;
        }
        done += part;
    } while (done < op->size);
    return 1;
}
static int replay_host_clear_buffer(NvnNativeReplayHost* host, const NvnOpClearBuffer* op) {
    uint32_t words[NVN_CLEAR_BUFFER_WORDS_MAX];
    uint32_t count = nvn_clear_buffer_words(op->dst, op->size, op->value, words);
    if (count && !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "clear buffer", -1, -1, (uint32_t)op->dst);
        return 1;
    }
    return 1;
}
static int replay_host_update_uniform(NvnNativeReplayHost* host, const NvnOpUpdateUniform* op) {
    uint32_t words[NVN_UPDATE_UNIFORM_WORDS_MAX];
    uint32_t count = nvn_update_uniform_words(op->address, op->size256, op->offset, op->data, op->words, words);
    if (!count || !replay_bind_send(host, words, count)) {
        replay_bind_refuse(host, "update uniform", -1, -1, op->size256);
        return 1;
    }
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
    if (code == NVN_OP_REPORT_VALUE) return replay_host_report_value(host, (const NvnOpReportValue*)payload);
    if (code == NVN_OP_SIGNAL_EVENT) return replay_host_signal_event(host, (const NvnOpSignalEvent*)payload);
    if (code == NVN_OP_WAIT_EVENT) return replay_host_wait_event(host, (const NvnOpWaitEvent*)payload);
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
    if (code == NVN_OP_COPY_BUFFER_TEXTURE)
        return replay_host_copy_buffer_texture(host, (const NvnOpCopyBufferTexture*)payload);
    if (code == NVN_OP_DOWNSAMPLE)
        return replay_host_downsample(host, (const NvnOpDownsample*)payload);
    if (code == NVN_OP_COPY_BUFFER)
        return replay_host_copy_buffer(host, (const NvnOpCopyBuffer*)payload);
    if (code == NVN_OP_CLEAR_BUFFER)
        return replay_host_clear_buffer(host, (const NvnOpClearBuffer*)payload);
    if (code == NVN_OP_UPDATE_UNIFORM)
        return replay_host_update_uniform(host, (const NvnOpUpdateUniform*)payload);
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
                          .bind_texture = replay_host_bind_texture,
                          .bind_texture_plural = replay_host_bind_texture_plural,
                          .bind_buffer = replay_host_bind_buffer,
                          .bind_state = replay_host_bind_state, .draw = replay_host_draw,
                          .dispatch = replay_host_dispatch, .bind_program = replay_host_bind_program,
                          .bind_vertex_attrib = replay_host_bind_vertex_attrib,
                          .bind_vertex_stream = replay_host_bind_vertex_stream,
                          .render_targets = replay_host_render_targets, .gpu_op = replay_host_gpu_op,
                          .fault = replay_host_fault, .window_origin_flip = window_origin_flip,
                          .channel_method = replay_host_channel_method};
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
WINDOW_BUILDER_HANDLER(window_builder_set_num_active_textures,
                       nvn_window_builder_set_active_textures(b, (u32)c->x[1]))
#undef WINDOW_BUILDER_HANDLER

#define WINDOW_BUILDER_GETTER(name, expression)                                 \
    static void h_##name(CPU* c, u64 entry) {                                   \
        (void)entry;                                                           \
        pm_lock(&objs_lock);                                                   \
        NvnWindowBuilder* b = rec_get(c->x[0], NVN_OBJ_WINDOW_BUILDER, sizeof *b); \
        c->x[0] = (expression);                                                \
        pm_unlock(&objs_lock);                                                 \
    }
WINDOW_BUILDER_GETTER(window_builder_get_device, b->device)
WINDOW_BUILDER_GETTER(window_builder_get_native_window, b->native_window)
WINDOW_BUILDER_GETTER(window_builder_get_num_active_textures, b->active_texture_count)
WINDOW_BUILDER_GETTER(window_builder_get_num_textures, b->texture_count)
WINDOW_BUILDER_GETTER(window_builder_get_present_interval, nvn_window_builder_present_interval(b))
#undef WINDOW_BUILDER_GETTER

static void h_window_builder_get_texture(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnWindowBuilder* b = rec_get(c->x[0], NVN_OBJ_WINDOW_BUILDER, sizeof *b);
    const int32_t index = (int32_t)c->x[1];
    uint64_t at = 0;
    uint64_t texture = 0;
    if (!nvn_window_texture_slot(b->textures, b->texture_count, index, &at)) {
        c->x[0] = 0;
    } else if (!native_guest_readable(at, sizeof(uint64_t))) {
        LOG("nvn-native: nvnWindowBuilderGetTexture unreadable index=%d address=%llx\n",
            (int)index, (unsigned long long)at);
        c->x[0] = 0;
    } else {
        texture = gr64(at);
        c->x[0] = texture;
    }
    pm_unlock(&objs_lock);
}

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
    next.active_textures = b->active_texture_count;
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
/* The library frees the image syncs and the native window; the host owns none of them and removes only its record. */
static void h_window_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (known_record(c->x[0], NVN_OBJ_WINDOW))
        nvn_objtab_remove(&objs, c->x[0]);
    else
        LOG("nvn-native: nvnWindowFinalize unknown window=%llx\n", (unsigned long long)c->x[0]);
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
static void h_window_set_num_active_textures(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
    if (!w) LOG("nvn-native: nvnWindowSetNumActiveTextures unknown window=%llx\n", (unsigned long long)c->x[0]);
    else w->active_textures = nvn_window_active_textures((u32)c->x[1]);
    pm_unlock(&objs_lock);
}
#define WINDOW_GETTER(name, expression)                                         \
    static void h_##name(CPU* c, u64 entry) {                                   \
        (void)entry;                                                           \
        pm_lock(&objs_lock);                                                   \
        const WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);            \
        if (!w) LOG("nvn-native: " #name " unknown window=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = w ? (expression) : 0;                                        \
        pm_unlock(&objs_lock);                                                 \
    }
WINDOW_GETTER(window_get_native_window, w->native_window)
WINDOW_GETTER(window_get_num_active_textures, w->active_textures)
WINDOW_GETTER(window_get_num_textures, w->texture_count)
WINDOW_GETTER(window_get_present_interval, w->interval)
#undef WINDOW_GETTER

static void h_window_get_crop(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnWindowGetCrop";
    pm_lock(&objs_lock);
    if (guest_outputs_writable(c, name, 4)) {
        const WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
        if (!w)
            LOG("nvn-native: %s unknown window=%llx\n", name, (unsigned long long)c->x[0]);
        else {
            gw32(c->x[1], w->crop.x);
            gw32(c->x[2], w->crop.y);
            gw32(c->x[3], w->crop.width);
            gw32(c->x[4], w->crop.height);
        }
    }
    pm_unlock(&objs_lock);
}
static void h_sync_initialize(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    SyncRec* s = rec_get(c->x[0], NVN_OBJ_SYNC, sizeof *s);
    s->device = c->x[1];
    s->state = NVN_SYNC_UNSIGNALED;
    s->syncpoint = 0;
    s->value = 0;
    s->serial = 0;
    c->x[0] = 1; pm_unlock(&objs_lock);
}
static void h_sync_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (known_record(c->x[0], NVN_OBJ_SYNC)) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
/* The host has no GL backend. The library stores a GL sync in a SyncRec of kind 4, so both calls fail the same way:
   the library's result for "no backend" and "unexpected backend result". */
#define NVN_GL_SYNC_OK         0u
#define NVN_GL_SYNC_NO_BACKEND 4u
static void h_sync_initialize_from_fenced_gl_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnSyncInitializeFromFencedGLSync";
    LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s sync=%llx device=%llx fails: the host has no GL backend\n", name,
              (unsigned long long)c->x[0], (unsigned long long)c->x[1]);
    if (c->x[3] && native_guest_writable(c->x[3], sizeof(uint32_t)))
        gw32(c->x[3], NVN_GL_SYNC_NO_BACKEND);
    c->x[0] = NVN_GL_SYNC_OK;
}
static void h_sync_create_gl_sync(CPU* c, u64 entry) {
    (void)entry;
    LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: nvnSyncCreateGLSync sync=%llx fails: the host has no GL backend\n",
              (unsigned long long)c->x[0]);
    c->x[0] = NVN_GL_SYNC_NO_BACKEND;
}

enum { NVN_ACQUIRE_OK = 0, NVN_ACQUIRE_FAILED = 1 };
/* Ends a failed acquire: the operation, the reason, the addresses and the result. objs_lock is held. */
static void window_acquire_failed(CPU* c, const char* operation, u64 queue_addr, u64 window_addr, u64 sync_addr,
                                  u64 index_addr, const char* reason) {
    LOG("nvn-native: %s failed: %s (queue=%llx window=%llx sync=%llx index=%llx)\n", operation, reason,
        (unsigned long long)queue_addr, (unsigned long long)window_addr, (unsigned long long)sync_addr,
        (unsigned long long)index_addr);
    if (native_guest_writable(index_addr, sizeof(uint32_t))) gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
    c->x[0] = NVN_ACQUIRE_FAILED;
}
/* The pick and state part of nvnWindowAcquireTexture and nvnQueueAcquireTexture. operation goes into the log lines,
   queue_addr is 0 for a window acquire and sync_addr is 0 for a queue acquire. The library makes the queue wait for the
   image at the GPU; the host picks only an image whose frame job has finished, so it records no wait op. */
static void window_acquire_texture(CPU* c, u64 queue_addr, u64 window_addr, u64 sync_addr, u64 index_addr,
                                   const char* operation) {
    const int has_queue = queue_addr != 0;
    const int has_sync = !has_queue; /* a window acquire needs its sync, a queue acquire has none */
    pm_lock(&objs_lock);
    const int queue_ok = !has_queue || known_record(queue_addr, NVN_OBJ_QUEUE) != NULL;
    WindowRec* window = known_record(window_addr, NVN_OBJ_WINDOW);
    SyncRec* sync = has_sync ? known_record(sync_addr, NVN_OBJ_SYNC) : NULL;
    const char* reason = nvn_window_acquire_refusal(0, queue_ok, window != NULL, !has_sync || sync != NULL,
                                                    native_guest_writable(index_addr, sizeof(uint32_t)));
    if (reason) {
        window_acquire_failed(c, operation, queue_addr, window_addr, sync_addr, index_addr, reason);
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t interval = window->interval;
    pm_unlock(&objs_lock);
    present_pace(interval);
    pm_lock(&objs_lock);
    window = known_record(window_addr, NVN_OBJ_WINDOW);
    sync = has_sync ? known_record(sync_addr, NVN_OBJ_SYNC) : NULL;
    reason = nvn_window_acquire_refusal(1, 1, window != NULL, !has_sync || sync != NULL,
                                        native_guest_writable(index_addr, sizeof(uint32_t)));
    if (reason) {
        window_acquire_failed(c, operation, queue_addr, window_addr, sync_addr, index_addr, reason);
        pm_unlock(&objs_lock);
        return;
    }
    int32_t index = nvn_window_pick_image(window->states, window->texture_count, window->last_index);
    /* A PRESENTED image comes back when its frame job finishes. */
    while (index < 0 && nvn_window_has_presented(window->states, window->texture_count)) {
        kthread_wait(&frame_done_cv, &objs_lock, -1);
        window = known_record(window_addr, NVN_OBJ_WINDOW);
        sync = has_sync ? known_record(sync_addr, NVN_OBJ_SYNC) : NULL;
        reason = nvn_window_acquire_refusal(1, 1, window != NULL, !has_sync || sync != NULL,
                                            native_guest_writable(index_addr, sizeof(uint32_t)));
        if (reason) {
            window_acquire_failed(c, operation, queue_addr, window_addr, sync_addr, index_addr, reason);
            pm_unlock(&objs_lock);
            return;
        }
        index = nvn_window_pick_image(window->states, window->texture_count, window->last_index);
    }
    if (index < 0) {
        LOG("nvn-native: %s failed: no free window image (window=%llx states=%u,%u,%u,%u)\n", operation,
            (unsigned long long)window_addr, window->states[0], window->states[1], window->states[2],
            window->states[3]);
        gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
        c->x[0] = NVN_ACQUIRE_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    nvn_window_acquire_take(window->states, &window->last_index, index);
    /* A free image means its previous frame job has finished. */
    if (sync) sync->state = NVN_SYNC_SIGNALED;
    gw32(index_addr, (uint32_t)index);
    c->x[0] = NVN_ACQUIRE_OK;
    frame_acquires++;
    pm_unlock(&objs_lock);
}
static void h_window_acquire_texture(CPU* c, u64 entry) {
    (void)entry;
    window_acquire_texture(c, 0, c->x[0], c->x[1], c->x[2], "nvnWindowAcquireTexture");
}
static void h_queue_acquire_texture(CPU* c, u64 entry) {
    (void)entry;
    window_acquire_texture(c, c->x[0], c->x[1], 0, c->x[2], "nvnQueueAcquireTexture");
}
/* 1 when the syncpoint has reached value, across the 32-bit wrap (the look of syncpt_wait with a 0 timeout). */
static int syncpoint_reached(u32 id, u32 value) { return (int32_t)(syncpt_read(id) - value) >= 0; }
/* nvnSyncWait: PENDING waits for the frame's post, then the syncpoint of the queue is the fence. The syncpoint wait
   runs without objs_lock, so the GPU thread can post and finish jobs meanwhile. */
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
    NvnSyncWaitResult result = NVN_SYNC_WAIT_FAILED;
    int waited = 0;
    for (;;) {
        const int reached = sync->state == NVN_SYNC_FENCED ? syncpoint_reached(sync->syncpoint, sync->value) : 0;
        const NvnSyncAction action = nvn_sync_wait_action(sync->state, reached);
        if (action == NVN_SYNC_ACTION_SIGNALED) {
            result = waited ? NVN_SYNC_WAIT_SATISFIED : NVN_SYNC_WAIT_SIGNALED;
            break;
        }
        if (action == NVN_SYNC_ACTION_REFUSE) {
            LOG_LIMIT(32, "nvn-native: nvnSyncWait sync=%llx state=%u timeout=%llx is not signaled, fails (caller lr=%llx)\n",
                      (unsigned long long)sync_addr, (unsigned)sync->state, (unsigned long long)c->x[1],
                      (unsigned long long)c->x[30]);
            result = NVN_SYNC_WAIT_FAILED;
            break;
        }
        const i64 left = nvn_wait_remaining_ns(limit, plat_ns());
        if (left == 0) {
            result = NVN_SYNC_WAIT_TIMEOUT;
            break;
        }
        if (action == NVN_SYNC_ACTION_WAIT_FRAME) {
            kthread_wait(&frame_done_cv, &objs_lock, left);
        } else {
            const uint32_t id = sync->syncpoint, value = sync->value;
            pm_unlock(&objs_lock);
            syncpt_wait(id, value, left);
            pm_lock(&objs_lock);
        }
        waited = 1;
        sync = known_record(sync_addr, NVN_OBJ_SYNC);
        if (!sync) {
            LOG_LIMIT(32, "nvn-native: nvnSyncWait sync=%llx is gone while it waited, fails\n",
                      (unsigned long long)sync_addr);
            result = NVN_SYNC_WAIT_FAILED;
            break;
        }
    }
    c->x[0] = result;
    pm_unlock(&objs_lock);
}

/* The frame that Present, a flush, a finish or a fenced flush closes (decisions 8 and 10). The GPU thread replays it
   on the private channel, shows the window image of a present, frees it and increments the queue's syncpoint. */
typedef enum { NVN_FRAME_PRESENT = 0, NVN_FRAME_FLUSH } NvnFrameKind;
typedef struct {
    NvnPendingFrame frame;
    NvnFrameKind kind;
    uint64_t queue, window, texture;
    uint32_t window_generation, texture_generation, index;
    int window_origin_flip;
    GpuChannel* channel; /* the private channel of the queue, used on the GPU thread only */
    uint32_t* init_words; /* the queue's macro code, copied so that it outlives the record */
    uint32_t init_word_count;
} FrameJob;
/* What a post takes from the caller; a flush leaves the window fields zero. */
typedef struct {
    NvnFrameKind kind;
    uint64_t window, texture;
    uint32_t window_generation, texture_generation, index;
    int window_origin_flip;
} FrameTarget;
#define FRAME_LOG_LIMIT 4u           /* log lines for the faults of the frame job */
#define NVN_FLUSH_LOG_LIMIT 4u       /* the first flush jobs of a run, each logged */
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
/* The selftest replays one command list on its private channel. The host state is large, so it lives on the heap for
   this one call. GPU thread only. */
int nvn_native_selftest_replay(GpuChannel* ch, uint64_t handle, int window_origin_flip, NvnReplayStats* stats) {
    NvnNativeReplayHost* host = calloc(1, sizeof *host);
    if (!host) {
        LOG("nvn-native: selftest replay: out of memory for the host state\n");
        return -1;
    }
    host->channel = ch;
    host->clip_limit = NVN_REPLAY_CLIP_LIMIT;
    NvnReplayEnv env;
    nvn_native_replay_env(host, window_origin_flip, &env);
    pm_lock(&objs_lock);
    const NvnOpList* list = nvn_list_find(&lists, handle);
    pm_unlock(&objs_lock);
    if (!list) {
        LOG("nvn-native: selftest replay: handle=%llx is not a live list\n", (unsigned long long)handle);
        free(host);
        return -1;
    }
    nvn_replay_list(&env, list, stats);
    free(host);
    return 0;
}
static void frame_end(const FrameJob* job) {
    r3d_const_flush(job->channel);
    r3d_pending_flush();
    /* The job's syncpoint increment follows the releases of its reports, as the console's fence does. */
    gpu_stream_incr(job->channel->id);
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
    if (job->kind == NVN_FRAME_PRESENT) {
        NvnObj* wo = nvn_objtab_valid(&objs, job->window, job->window_generation)
                         ? nvn_objtab_find(&objs, job->window) : NULL;
        WindowRec* window = wo && wo->kind == NVN_OBJ_WINDOW ? wo->data : NULL;
        if (window && job->index < window->texture_count && window->states[job->index] == NVN_IMAGE_PRESENTED)
            window->states[job->index] = NVN_IMAGE_FREE;
    }
    nvn_frame_release(&job->frame, &lists);
    frames_in_flight--;
    pc_broadcast(&frame_done_cv); /* the image and the syncs are up to date */
    pm_unlock(&objs_lock);
}
static void frame_line(const FrameJob* job, const NvnReplayStats* stats, int shown, uint32_t fences,
                       uint32_t list_count, uint32_t frame_number, uint64_t dispatches, uint32_t dispatch_refusals) {
    uint32_t ops = 0;
    for (uint32_t i = 0; i < NVN_OP_COUNT; i++) ops += stats->replayed[i];
    uint32_t missing = __atomic_load_n(&missing_calls, __ATOMIC_RELAXED);
    uint32_t dropped = __atomic_load_n(&dropped_ops, __ATOMIC_RELAXED);
    static uint32_t last_missing_calls, last_dropped_ops; /* only the GPU thread runs the frame jobs */
    uint32_t missing_delta = missing - last_missing_calls, dropped_delta = dropped - last_dropped_ops;
    /* A flush has no window, so a missing image is normal for it. */
    const int flush = job->kind == NVN_FRAME_FLUSH;
    int abnormal = stats->faults != 0 || dropped_delta != 0 || missing_delta != 0 || (!shown && !flush);
    if (flush) {
        if (!abnormal && frame_number > NVN_FLUSH_LOG_LIMIT) return;
        char causes[256];
        replay_causes_take(causes, sizeof causes);
        if (causes[0]) LOG("nvn-native: flush %u refusals: %s\n", frame_number, causes);
        last_missing_calls = missing;
        last_dropped_ops = dropped;
        LOG_LIMIT(NVN_FRAME_ABNORMAL_LIMIT,
                  "nvn-native: flush %u queue=%llx lists=%u tokens=%u ops=%u faults=%u methods=%u draws=%u "
                  "missing=%u dropped=%u\n",
                  frame_number, (unsigned long long)job->queue, list_count, fences, ops, stats->faults, stats->methods,
                  stats->replayed[NVN_OP_DRAW], missing_delta, dropped_delta);
        return;
    }
    char causes[256];
    replay_causes_take(causes, sizeof causes);
    pm_lock(&objs_lock);
    uint32_t submitted = frame_submitted_lists, skipped = frame_skipped_lists, acquires = frame_acquires;
    uint32_t unmet_event_waits = frame_unmet_event_waits;
    uint32_t live = nvn_list_live(&lists);
    pm_unlock(&objs_lock);
    uint32_t gops = stats->replayed[NVN_OP_CLEAR_DEPTH_STENCIL] + stats->replayed[NVN_OP_CLEAR_TEXTURE] +
                    stats->replayed[NVN_OP_COPY_TEXTURE] + stats->replayed[NVN_OP_REPORT_COUNTER] +
                    stats->replayed[NVN_OP_BIND_IMAGE];
    if ((abnormal && nvn_frame_log_due(frame_number, 1)) || (!abnormal && nvn_frame_log_due(frame_number, 0))) {
        if (causes[0]) LOG("nvn-native: frame %u refusals: %s\n", frame_number, causes);
        last_missing_calls = missing; /* the deltas of the next line are from this one */
        last_dropped_ops = dropped;
        if (abnormal)
            LOG_LIMIT(NVN_FRAME_ABNORMAL_LIMIT,
                      "nvn-native: frame %u window=%llx index=%u lists=%u (live %u) ops=%u faults=%u methods=%u "
                      "draws=%u dispatches=%llu dispatch_refusals=%u vbinds=%u gops=%u dropped=%u missing=%u "
                      "skipped_lists=%u acquires=%u fences=%u unmet_event_waits=%u shown=%d\n",
                      frame_number, (unsigned long long)job->window, job->index, submitted, live, ops, stats->faults,
                      stats->methods, stats->replayed[NVN_OP_DRAW], (unsigned long long)dispatches, dispatch_refusals,
                      stats->replayed[NVN_OP_BIND_VERTEX_ATTRIB] + stats->replayed[NVN_OP_BIND_VERTEX_STREAM], gops,
                      dropped_delta, missing_delta, skipped, acquires, fences, unmet_event_waits, shown);
        else
            LOG("nvn-native: frame %u window=%llx index=%u lists=%u (live %u) ops=%u faults=%u methods=%u draws=%u "
                "dispatches=%llu dispatch_refusals=%u vbinds=%u gops=%u dropped=%u missing=%u skipped_lists=%u "
                "acquires=%u fences=%u unmet_event_waits=%u shown=%d\n",
                frame_number, (unsigned long long)job->window, job->index, submitted, live, ops, stats->faults,
                stats->methods, stats->replayed[NVN_OP_DRAW], (unsigned long long)dispatches, dispatch_refusals,
                stats->replayed[NVN_OP_BIND_VERTEX_ATTRIB] + stats->replayed[NVN_OP_BIND_VERTEX_STREAM], gops,
                dropped_delta, missing_delta, skipped, acquires, fences, unmet_event_waits, shown);
    }
}
/* Replays the init words that the host built at QueueInitialize, before the frame's own lists. Subchannel 0 is the
   3D engine; the other subchannels are their own classes. */
static void frame_replay_init(const FrameJob* job) {
    if (!job->init_word_count) return;
    NvnWordStream stream = {0};
    uint32_t methods = 0;
    for (uint32_t i = 0; i < job->init_word_count; i++) {
        uint32_t method, value;
        int last;
        const int result = nvn_word_stream_next(&stream, job->init_words[i], &method, &value, &last);
        if (result < 0) {
            LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: queue init stream refused at word=%u of %u\n", i,
                      job->init_word_count);
            return;
        }
        if (result == 0) continue; /* a header word: its data words follow */
        replay_stream_method(job->channel, stream.subchannel, method, value, last);
        methods++;
    }
    if (!nvn_word_stream_complete(&stream)) {
        LOG_LIMIT(FRAME_LOG_LIMIT, "nvn-native: queue init stream ends inside a run after %u words\n",
                  job->init_word_count);
        return;
    }
    LOG("nvn-native: queue init stream replayed methods=%u\n", methods);
}

/* Runs on the GPU thread: it frees the job that a post made. A flush has no window: it replays the lists and ends. */
static void frame_run(void* arg) {
    FrameJob* job = arg;
    static uint32_t frame_jobs, flush_jobs; /* only the GPU thread runs the frame jobs */
    const int flush = job->kind == NVN_FRAME_FLUSH;
    frame_begin();
    FrameParts parts = {0};
    if (!flush) {
        frame_read_parts(job, &parts);
        frame_first_log(job, &parts);
    }
    frame_replay_init(job);
    if (!flush) frame_set_target(job, &parts);
    NvnReplayStats stats = {0};
    uint64_t dispatches = 0;
    uint32_t dispatch_refusals = 0;
    frame_replay_lists(job, &stats, &dispatches, &dispatch_refusals);
    frame_end(job);
    int shown = flush ? 0 : frame_show(job, &parts);
    uint32_t list_count = job->frame.handle_count;
    uint32_t fences = job->frame.token_count;
    uint32_t frame_number = flush ? ++flush_jobs : ++frame_jobs;
    frame_finish(job);
    frame_line(job, &stats, shown, fences, list_count, frame_number, dispatches, dispatch_refusals);
    free(job->init_words);
    free(job);
}

/* The serial that the next fence of a sync takes. Commit it only when the token that carries it is stored. */
static uint32_t fence_serial_next(const SyncRec* sync) {
    const uint32_t serial = sync->serial + 1;
    return serial ? serial : 1;
}
/* The device window origin of the queue, for a job without a window. Caller holds objs_lock. */
static int queue_origin_flip(const QueueRec* q) {
    const DeviceRec* device = known_record(q->builder.device, NVN_OBJ_DEVICE);
    return device && device->window_origin == 1;
}
/* Posts the pending frame of a queue as a job: one syncpoint increment, the value to the newest fence of each token,
   the init words once. Caller holds objs_lock and calls gpu_call with the result before the unlock, so the fence and
   its job enter the GPU queue in that order. Lock order: objs_lock, then g_qlk; never take objs_lock with g_qlk held. */
static FrameJob* queue_post_frame(u64 queue_addr, QueueRec* q, const FrameTarget* target) {
    FrameJob* job = malloc(sizeof *job);
    if (!job) fatal("nvn-native: out of memory for the frame job of a queue\n");
    *job = (FrameJob){0};
    job->frame = nvn_frame_take(&q->pending);
    job->kind = target->kind;
    job->queue = queue_addr;
    job->window = target->window;
    job->window_generation = target->window_generation;
    job->texture = target->texture;
    job->texture_generation = target->texture_generation;
    job->index = target->index;
    job->window_origin_flip = target->kind == NVN_FRAME_PRESENT ? target->window_origin_flip : queue_origin_flip(q);
    job->channel = q->channel;
    if (!q->init_replayed) {
        if (q->init_word_count) {
            job->init_words = malloc((size_t)q->init_word_count * sizeof *job->init_words);
            if (!job->init_words) fatal("nvn-native: out of memory for the macro code of a frame job\n");
            memcpy(job->init_words, q->init_words, (size_t)q->init_word_count * sizeof *job->init_words);
            job->init_word_count = q->init_word_count;
        }
        q->init_replayed = 1; /* this job carries the replay, even when the builder left nothing */
    }
    const uint32_t value = syncpt_promise(q->channel_id, 1);
    q->last_posted = value;
    q->has_posted = 1;
    for (uint32_t i = 0; i < job->frame.token_count; i++) {
        const NvnFrameToken* token = &job->frame.tokens[i];
        NvnObj* so = nvn_objtab_valid(&objs, token->sync, token->generation)
                         ? nvn_objtab_find(&objs, token->sync) : NULL;
        if (!so || so->kind != NVN_OBJ_SYNC) continue;
        SyncRec* sync = so->data;
        if (sync->serial != token->serial) continue; /* a newer fence of this sync is in another frame */
        sync->syncpoint = q->channel_id;
        sync->value = value;
        sync->state = NVN_SYNC_FENCED;
    }
    frames_in_flight++;
    pc_broadcast(&frame_done_cv); /* a PENDING waiter now has a fence to wait on */
    return job;
}

/* Follows the fence, wait and CALL ops of one submitted list. A fence becomes a token of the pending frame with the
   serial of that sync, so a newer fence replaces it. Its signal is at the end of the frame job, later than in the
   library: a waiter waits longer, never shorter. A wait only checks what the serial order has reached. *flush is set
   to 1 when a fence token has FLUSH_FOR_CPU. Called with objs_lock held, after the lists were added to the frame. */
static void queue_scan_list(u64 queue_addr, QueueRec* q, const char* name, const NvnOpList* list, uint64_t list_handle,
                            uint32_t depth, int* flush) {
    size_t pos = 0;
    NvnOpHeader header;
    const void* payload;
    while (nvn_oplist_next(list, &pos, &header, &payload)) {
        if (header.code == NVN_OP_FENCE_SYNC) {
            const NvnOpFenceSync* op = payload;
            NvnObj* so = nvn_objtab_valid(&objs, op->sync, op->generation) ? nvn_objtab_find(&objs, op->sync) : NULL;
            if (!so || so->kind != NVN_OBJ_SYNC) {
                LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx list=%llx sync=%llx generation=%u is gone\n",
                          name, (unsigned long long)queue_addr, (unsigned long long)list_handle,
                          (unsigned long long)op->sync, op->generation);
            } else {
                SyncRec* sync = so->data;
                const uint32_t serial = fence_serial_next(sync);
                if (!nvn_frame_add_token(&q->pending, op->sync, op->generation, serial)) {
                    LOG_LIMIT(QUEUE_LOG_LIMIT,
                              "nvn-native: %s queue=%llx list=%llx sync=%llx does not fit in the pending frame\n", name,
                              (unsigned long long)queue_addr, (unsigned long long)list_handle,
                              (unsigned long long)op->sync);
                } else {
                    sync->serial = serial;
                    sync->syncpoint = q->channel_id;
                    sync->state = NVN_SYNC_PENDING;
                    if (op->flags & NVN_QUEUE_FENCE_FLUSH_FOR_CPU) *flush = 1;
                }
            }
        } else if (header.code == NVN_OP_WAIT_SYNC) {
            const NvnOpWaitSync* op = payload;
            NvnObj* so = nvn_objtab_valid(&objs, op->sync, op->generation) ? nvn_objtab_find(&objs, op->sync) : NULL;
            const SyncRec* sync = so && so->kind == NVN_OBJ_SYNC ? so->data : NULL;
            if (!sync) {
                LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx list=%llx sync=%llx generation=%u is gone\n",
                          name, (unsigned long long)queue_addr, (unsigned long long)list_handle,
                          (unsigned long long)op->sync, op->generation);
            } else if (sync->state == NVN_SYNC_UNSIGNALED ||
                       (sync->state == NVN_SYNC_PENDING && sync->syncpoint != q->channel_id)) {
                /* a PENDING fence of this queue is earlier in the same pending frame, so it is in order */
                LOG_LIMIT(QUEUE_LOG_LIMIT,
                          "nvn-native: %s queue=%llx list=%llx sync=%llx is not reached before the wait\n", name,
                          (unsigned long long)queue_addr, (unsigned long long)list_handle,
                          (unsigned long long)op->sync);
            }
        } else if (header.code == NVN_OP_CALL) {
            const NvnOpCall* op = payload;
            if (depth >= NVN_CALL_DEPTH_MAX) {
                LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx list=%llx call=%llx is deeper than %u\n", name,
                          (unsigned long long)queue_addr, (unsigned long long)list_handle,
                          (unsigned long long)op->handle, NVN_CALL_DEPTH_MAX);
                continue;
            }
            const NvnOpList* target = nvn_list_find(&lists, op->handle);
            if (!target) {
                LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx list=%llx call=%llx is not a live list\n", name,
                          (unsigned long long)queue_addr, (unsigned long long)list_handle,
                          (unsigned long long)op->handle);
                continue;
            }
            queue_scan_list(queue_addr, q, name, target, op->handle, depth + 1, flush);
        }
    }
}

/* nvnQueueSubmitCommands(queue, count, handles): copies the handle array at once and adds the lists to the pending
   frame (decision 8). The game alternates two arrays and reuses them, so the copy is not optional. A fence op with
   FLUSH_FOR_CPU posts the frame after the scan. */
static void h_queue_submit_commands(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueSubmitCommands";
    FrameJob* job = NULL;
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
    if (result == NVN_FRAME_MEMORY) {
        free(handles);
        fatal("nvn-native: out of memory for the pending frame of a queue\n");
    }
    if (result == NVN_FRAME_LIMIT) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx count=%u does not fit in the pending frame\n",
                  name, (unsigned long long)c->x[0], handle_count);
    } else {
        int flush = 0;
        if (skipped)
            LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s queue=%llx skipped %u handles that are not live lists\n",
                      name, (unsigned long long)c->x[0], skipped);
        for (uint32_t i = 0; i < handle_count; i++) {
            const NvnOpList* list = nvn_list_find(&lists, handles[i]);
            if (list) queue_scan_list(c->x[0], q, name, list, handles[i], 0, &flush);
        }
        frame_submitted_lists += handle_count - skipped;
        frame_skipped_lists += skipped;
        if (flush && !nvn_frame_is_empty(&q->pending)) {
            const FrameTarget target = {.kind = NVN_FRAME_FLUSH};
            job = queue_post_frame(c->x[0], q, &target);
        }
    }
    free(handles);
    if (job) gpu_call(frame_run, job, NULL, 0);
    pm_unlock(&objs_lock);
}

/* nvnQueueFenceSync(queue, sync, condition, flags): a token in the pending frame, or the syncpoint value of the
   queue when it has no pending list (decisions 8 and 10). Bit 0 also posts the pending frame. */
static void h_queue_fence_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueFenceSync";
    const uint32_t flags = (uint32_t)c->x[3];
    FrameJob* job = NULL;
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
    if (flags & ~NVN_QUEUE_FENCE_FLUSH_FOR_CPU)
        LOG_LIMIT(8, "nvn-native: %s flags=%llx has bits other than FLUSH_FOR_CPU\n", name,
                  (unsigned long long)flags);
    if (!nvn_frame_is_empty(&q->pending)) {
        const uint32_t serial = fence_serial_next(sync);
        if (!nvn_frame_add_token(&q->pending, c->x[1], so->generation, serial)) {
            LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s sync=%llx does not fit in the pending frame\n",
                      name, (unsigned long long)c->x[1]);
            pm_unlock(&objs_lock);
            return;
        }
        sync->serial = serial;
        sync->syncpoint = q->channel_id;
        sync->state = NVN_SYNC_PENDING;
        if (flags & NVN_QUEUE_FENCE_FLUSH_FOR_CPU) {
            const FrameTarget target = {.kind = NVN_FRAME_FLUSH};
            job = queue_post_frame(c->x[0], q, &target);
        }
    } else {
        sync->serial = fence_serial_next(sync); /* a newer fence replaces a token that another frame still holds */
        sync->syncpoint = q->channel_id;
        sync->value = nvn_sync_fence_value(q->has_posted, q->last_posted, syncpt_read(q->channel_id));
        sync->state = NVN_SYNC_FENCED;
        pc_broadcast(&frame_done_cv); /* a PENDING waiter of an older token now sees the fence */
    }
    if (job) gpu_call(frame_run, job, NULL, 0);
    pm_unlock(&objs_lock);
}

/* nvnQueueWaitSync(queue, sync): the host has one GPU worker (gpu.c, gpu_call), and the frame jobs and calls run on it
   in order, so a sync that a queue fenced is reached before later work of any queue. A PENDING fence of another queue
   is not posted yet, and the host cannot park a job: the call returns 0 with a log line. A PENDING fence of the same
   queue is earlier in the same frame, so it returns 1. The library appends GPU waits. */
static void h_queue_wait_sync(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueWaitSync";
    const u64 sync_addr = c->x[1];
    pm_lock(&objs_lock);
    QueueRec* q = queue_rec(c->x[0], name);
    const SyncRec* sync = q ? known_record(sync_addr, NVN_OBJ_SYNC) : NULL;
    const int cross = sync && q && sync->state == NVN_SYNC_PENDING && sync->syncpoint != q->channel_id;
    if (q && !sync)
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s unknown sync=%llx\n", name, (unsigned long long)sync_addr);
    if (cross)
        LOG_LIMIT(QUEUE_LOG_LIMIT,
                  "nvn-native: %s queue=%llx sync=%llx: a cross-queue wait on a pending fence is not supported\n",
                  name, (unsigned long long)c->x[0], (unsigned long long)sync_addr);
    else if (sync && sync->state == NVN_SYNC_PENDING)
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s sync=%llx is pending: its frame is not posted yet\n", name,
                  (unsigned long long)sync_addr);
    if (!q || !sync || sync->state == NVN_SYNC_UNSIGNALED) {
        if (sync && sync->state == NVN_SYNC_UNSIGNALED)
            LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s sync=%llx is not signaled\n", name,
                      (unsigned long long)sync_addr);
        c->x[0] = 0;
    } else {
        c->x[0] = cross ? 0 : 1;
    }
    pm_unlock(&objs_lock);
}
/* nvnQueueFlush: the pending frame goes to the GPU as a job without a window. An empty frame posts nothing. */
static void h_queue_flush(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueFlush";
    FrameJob* job = NULL;
    pm_lock(&objs_lock);
    QueueRec* q = queue_rec(c->x[0], name);
    if (q && !nvn_frame_is_empty(&q->pending)) {
        const FrameTarget target = {.kind = NVN_FRAME_FLUSH};
        job = queue_post_frame(c->x[0], q, &target);
    }
    if (job) gpu_call(frame_run, job, NULL, 0);
    pm_unlock(&objs_lock);
}
/* nvnQueueFinish: posts the pending frame and waits for the last posted syncpoint value, so the GPU work and its
   releases are done. The wait runs without objs_lock. */
static void h_queue_finish(CPU* c, u64 entry) {
    (void)entry;
    static const char* const name = "nvnQueueFinish";
    FrameJob* job = NULL;
    uint32_t id = 0, value = 0;
    int wait = 0;
    pm_lock(&objs_lock);
    QueueRec* q = queue_rec(c->x[0], name);
    if (q && !nvn_frame_is_empty(&q->pending)) {
        const FrameTarget target = {.kind = NVN_FRAME_FLUSH};
        job = queue_post_frame(c->x[0], q, &target);
    }
    if (q && q->has_posted) {
        id = q->channel_id;
        value = q->last_posted;
        wait = 1;
    }
    if (job) gpu_call(frame_run, job, NULL, 0);
    pm_unlock(&objs_lock);
    if (wait) syncpt_wait(id, value, -1);
}
/* QueueFinalize would destroy the queue and its channel; the host keeps the record to the process end. */
static void h_queue_finalize(CPU* c, u64 entry) {
    (void)entry;
    LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: nvnQueueFinalize queue=%llx is not supported, the record stays\n",
              (unsigned long long)c->x[0]);
}
/* U5: the queue usage getters. The host has no cursor and no running counter over the queue memory of the game, so
   every usage getter refuses with 0 instead of an invented number. */
static void h_queue_get_error(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    (void)queue_rec(c->x[0], "nvnQueueGetError");
    c->x[0] = 0; /* the host has no channel error notification, and x1 is not written */
    pm_unlock(&objs_lock);
}
#define QUEUE_USAGE_REFUSAL "the host does not use the game's queue memory, so it has no usage count"
#define QUEUE_USAGE_REFUSE_HANDLER(handler, operation)                                             \
    static void h_queue_##handler(CPU* c, u64 entry) {                                             \
        (void)entry;                                                                               \
        pm_lock(&objs_lock);                                                                       \
        if (queue_rec(c->x[0], operation))                                                         \
            LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s refused: %s\n", operation, QUEUE_USAGE_REFUSAL); \
        c->x[0] = 0;                                                                               \
        pm_unlock(&objs_lock);                                                                     \
    }
QUEUE_USAGE_REFUSE_HANDLER(get_total_command_memory_used, "nvnQueueGetTotalCommandMemoryUsed")
QUEUE_USAGE_REFUSE_HANDLER(get_total_control_memory_used, "nvnQueueGetTotalControlMemoryUsed")
QUEUE_USAGE_REFUSE_HANDLER(get_total_compute_memory_used, "nvnQueueGetTotalComputeMemoryUsed")
#undef QUEUE_USAGE_REFUSAL
#undef QUEUE_USAGE_REFUSE_HANDLER

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
    const char* reason = nvn_present_refusal(q != NULL, window != NULL, index,
                                             window ? window->texture_count : 0,
                                             window ? window->states : NULL);
    if (reason) {
        LOG_LIMIT(QUEUE_LOG_LIMIT, "nvn-native: %s failed: %s (queue=%llx window=%llx index=%d)\n",
                  name, reason, (unsigned long long)queue_addr, (unsigned long long)window_addr, index);
        pm_unlock(&objs_lock);
        return;
    }
    window->states[index] = NVN_IMAGE_PRESENTED;
    FrameTarget target = {.kind = NVN_FRAME_PRESENT};
    target.window = window_addr;
    target.window_generation = wo->generation;
    target.texture = window->textures[index];
    NvnObj* to = nvn_objtab_find(&objs, target.texture);
    target.texture_generation = to ? to->generation : 0;
    target.index = (uint32_t)index;
    const DeviceRec* device = known_record(window->device, NVN_OBJ_DEVICE);
    target.window_origin_flip = device && device->window_origin == 1;
    FrameJob* job = queue_post_frame(queue_addr, q, &target);
    gpu_call(frame_run, job, NULL, 0);
    pm_unlock(&objs_lock);
}

/* The host handlers by NVN function name. Later steps add their entries here (G2 to G5 of nvn-design.md, 2-6). */
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
    {"nvnDeviceGetCurrentTimestampInNanoseconds", h_device_get_current_timestamp_ns},
    {"nvnDeviceGenerateDebugDomainId", h_device_generate_debug_domain_id},
    {"nvnDeviceIsExternalDebuggerAttached", h_device_is_external_debugger_attached},
    {"nvnDeviceInstallDebugCallback", h_library_ret},
    {"nvnDeviceWalkDebugDatabase", h_library_ret},
    {"nvnDeviceSetIntermediateShaderCache", h_library_ret},
    {"nvnDeviceSetAutomaticCheckpointsByDebugGroup", h_debug_checkpoint},
    {"nvnDeviceSetAutomaticCheckpointsEnable", h_debug_checkpoint},
    {"nvnDeviceSetAutomaticCheckpointsSamplingInterval", h_debug_checkpoint},
    {"nvnDeviceSetCheckpointCallStacksEnable", h_debug_checkpoint},
    {"nvnDeviceSetCheckpointMaxCallStacksDepth", h_debug_checkpoint},
    {"nvnDeviceRegisterFastClearColor", h_device_register_fast_clear_color},
    {"nvnDeviceRegisterFastClearColori", h_device_register_fast_clear_colori},
    {"nvnDeviceRegisterFastClearColorui", h_device_register_fast_clear_colorui},
    {"nvnDeviceRegisterFastClearDepth", h_device_register_fast_clear_depth},
    {"nvnDeviceGetTextureHandle", h_device_get_texture_handle},
    {"nvnDeviceGetImageHandle", h_device_get_image_handle},
    {"nvnDeviceGetSeparateSamplerHandle", h_device_get_separate_sampler_handle},
    {"nvnDeviceGetSeparateTextureHandle", h_device_get_separate_texture_handle},
    {"nvnDeviceGetTexelFetchHandle", h_device_get_texel_fetch_handle},
    {"nvnDeviceFinalize", h_device_finalize},
    {"nvnDeviceApplyDeferredFinalizes", h_device_apply_deferred_finalizes},
    {"nvnDeviceFinalizeCommandHandle", h_device_finalize_command_handle},
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
    {"nvnQueueWaitSync", h_queue_wait_sync},
    {"nvnQueueFlush", h_queue_flush},
    {"nvnQueueFinish", h_queue_finish},
    {"nvnQueueFinalize", h_queue_finalize},
    {"nvnQueuePresentTexture", h_queue_present_texture},
    {"nvnQueueAcquireTexture", h_queue_acquire_texture},
    {"nvnQueueGetError", h_queue_get_error},
    {"nvnQueueGetTotalCommandMemoryUsed", h_queue_get_total_command_memory_used},
    {"nvnQueueGetTotalControlMemoryUsed", h_queue_get_total_control_memory_used},
    {"nvnQueueGetTotalComputeMemoryUsed", h_queue_get_total_compute_memory_used},
    {"nvnQueueResetMemoryUsageCounts", h_library_ret},
    {"nvnMemoryPoolBuilderSetDefaults", h_memory_pool_builder_set_defaults},
    {"nvnMemoryPoolBuilderSetDevice", h_memory_pool_builder_set_device},
    {"nvnMemoryPoolBuilderSetFlags", h_memory_pool_builder_set_flags},
    {"nvnMemoryPoolBuilderSetStorage", h_memory_pool_builder_set_storage},
    {"nvnMemoryPoolBuilderGetDevice", h_memory_pool_builder_get_device},
    {"nvnMemoryPoolBuilderGetMemory", h_memory_pool_builder_get_memory},
    {"nvnMemoryPoolBuilderGetSize", h_memory_pool_builder_get_size},
    {"nvnMemoryPoolBuilderGetFlags", h_memory_pool_builder_get_flags},
    {"nvnMemoryPoolInitialize", h_memory_pool_initialize},
    {"nvnMemoryPoolFinalize", h_memory_pool_finalize},
    {"nvnMemoryPoolFlushMappedRange", h_memory_pool_flush_mapped_range},
    {"nvnMemoryPoolInvalidateMappedRange", h_memory_pool_invalidate_mapped_range},
    {"nvnMemoryPoolMap", h_memory_pool_map},
    {"nvnMemoryPoolMapVirtual", h_memory_pool_map_virtual},
    {"nvnMemoryPoolGetFlags", h_memory_pool_get_flags},
    {"nvnMemoryPoolGetSize", h_memory_pool_get_size},
    {"nvnMemoryPoolGetBufferAddress", h_memory_pool_get_buffer_address},
    {"nvnTexturePoolInitialize", h_texture_pool_initialize},
    {"nvnTexturePoolFinalize", h_library_ret},
    {"nvnTexturePoolGetSize", h_texture_pool_get_size},
    {"nvnSamplerPoolInitialize", h_sampler_pool_initialize},
    {"nvnSamplerPoolFinalize", h_library_ret},
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
    {"nvnSamplerBuilderSetBorderColori", h_sampler_builder_set_border_color},
    {"nvnSamplerBuilderSetBorderColorui", h_sampler_builder_set_border_color},
    {"nvnSamplerBuilderGetDevice", h_sampler_builder_get_device},
    {"nvnSamplerBuilderGetMinMagFilter", h_sampler_builder_get_min_mag_filter},
    {"nvnSamplerBuilderGetWrapMode", h_sampler_builder_get_wrap_mode},
    {"nvnSamplerBuilderGetLodClamp", h_sampler_builder_get_lod_clamp},
    {"nvnSamplerBuilderGetLodBias", h_sampler_builder_get_lod_bias},
    {"nvnSamplerBuilderGetCompare", h_sampler_builder_get_compare},
    {"nvnSamplerBuilderGetBorderColor", h_sampler_builder_get_border_color},
    {"nvnSamplerBuilderGetBorderColori", h_sampler_builder_get_border_color},
    {"nvnSamplerBuilderGetBorderColorui", h_sampler_builder_get_border_color},
    {"nvnSamplerBuilderGetMaxAnisotropy", h_sampler_builder_get_max_anisotropy},
    {"nvnSamplerBuilderGetReductionFilter", h_sampler_builder_get_reduction_filter},
    {"nvnSamplerBuilderGetLodSnap", h_sampler_builder_get_lod_snap},
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
    {"nvnTextureBuilderGetDepth", h_texture_builder_get_depth},
    {"nvnTextureBuilderGetDepthStencilMode", h_texture_builder_get_depth_stencil_mode},
    {"nvnTextureBuilderGetDevice", h_texture_builder_get_device},
    {"nvnTextureBuilderGetFlags", h_texture_builder_get_flags},
    {"nvnTextureBuilderGetFormat", h_texture_builder_get_format},
    {"nvnTextureBuilderGetLevels", h_texture_builder_get_levels},
    {"nvnTextureBuilderGetSamples", h_texture_builder_get_samples},
    {"nvnTextureBuilderGetSwizzle", h_texture_builder_get_swizzle},
    {"nvnTextureBuilderGetTarget", h_texture_builder_get_target},
    {"nvnTextureBuilderGetStride", h_texture_builder_get_stride},
    {"nvnTextureBuilderGetMemoryOffset", h_texture_builder_get_memory_offset},
    {"nvnTextureBuilderGetMemoryPool", h_texture_builder_get_memory_pool},
    {"nvnTextureBuilderGetGLTextureName", h_texture_builder_get_gl_texture_name},
    {"nvnTextureBuilderGetPackagedTextureData", h_texture_builder_get_packaged_texture_data},
    {"nvnTextureBuilderGetPackagedTextureLayout", h_texture_builder_get_packaged_texture_layout},
    {"nvnTextureBuilderGetRawStorageClass", h_texture_builder_get_raw_storage_class},
    {"nvnTextureBuilderGetStorageClass", h_texture_builder_get_storage_class},
    {"nvnTextureBuilderGetZCullStorageSize", h_texture_builder_get_zcull_storage_size},
    {"nvnTextureBuilderGetSparseTileLayout", h_texture_builder_get_sparse_tile_layout},
    {"nvnTextureBuilderSetGLTextureName", h_texture_builder_set_gl_texture_name},
    {"nvnTextureBuilderSetPackagedTextureData", h_texture_builder_set_packaged_texture_data},
    {"nvnTextureBuilderSetSamples", h_texture_builder_set_samples},
    {"nvnTextureBuilderSetSize1D", h_texture_builder_set_size_1d},
    {"nvnSamplerInitialize", h_sampler_initialize},
    {"nvnSamplerFinalize", h_sampler_finalize},
    {"nvnSamplerPoolRegisterSampler", h_sampler_pool_register_sampler},
    {"nvnSamplerPoolRegisterSamplerBuilder", h_sampler_pool_register_sampler_builder},
    {"nvnSamplerPoolGetMemoryPool", h_sampler_pool_get_memory_pool},
    {"nvnSamplerPoolGetMemoryOffset", h_sampler_pool_get_memory_offset},
    {"nvnSamplerCompare", h_sampler_compare},
    {"nvnSamplerGetBorderColor", h_sampler_get_border_color},
    {"nvnSamplerGetBorderColori", h_sampler_get_border_color},
    {"nvnSamplerGetBorderColorui", h_sampler_get_border_color},
    {"nvnSamplerGetCompare", h_sampler_get_compare},
    {"nvnSamplerGetDebugID", h_sampler_get_debug_id},
    {"nvnSamplerGetLodBias", h_sampler_get_lod_bias},
    {"nvnSamplerGetLodClamp", h_sampler_get_lod_clamp},
    {"nvnSamplerGetMaxAnisotropy", h_sampler_get_max_anisotropy},
    {"nvnSamplerGetMinMagFilter", h_sampler_get_min_mag_filter},
    {"nvnSamplerGetReductionFilter", h_sampler_get_reduction_filter},
    {"nvnSamplerGetWrapMode", h_sampler_get_wrap_mode},
    {"nvnTextureInitialize", h_texture_initialize},
    {"nvnTextureFinalize", h_texture_finalize},
    {"nvnTextureFlushTexels", h_texture_flush_texels},
    {"nvnTextureInvalidateTexels", h_texture_invalidate_texels},
    {"nvnTextureReadTexels", h_texture_read_texels},
    {"nvnTextureReadTexelsStrided", h_texture_read_texels_strided},
    {"nvnWindowBuilderSetDefaults", h_window_builder_set_defaults},
    {"nvnWindowBuilderSetDevice", h_window_builder_set_device},
    {"nvnWindowBuilderSetNativeWindow", h_window_builder_set_native_window},
    {"nvnWindowBuilderSetTextures", h_window_builder_set_textures},
    {"nvnWindowBuilderSetPresentInterval", h_window_builder_set_present_interval},
    {"nvnWindowBuilderSetNumActiveTextures", h_window_builder_set_num_active_textures},
    {"nvnWindowBuilderGetDevice", h_window_builder_get_device},
    {"nvnWindowBuilderGetNativeWindow", h_window_builder_get_native_window},
    {"nvnWindowBuilderGetNumActiveTextures", h_window_builder_get_num_active_textures},
    {"nvnWindowBuilderGetNumTextures", h_window_builder_get_num_textures},
    {"nvnWindowBuilderGetPresentInterval", h_window_builder_get_present_interval},
    {"nvnWindowBuilderGetTexture", h_window_builder_get_texture},
    {"nvnWindowInitialize", h_window_initialize},
    {"nvnWindowFinalize", h_window_finalize},
    {"nvnWindowSetPresentInterval", h_window_set_present_interval},
    {"nvnWindowSetCrop", h_window_set_crop},
    {"nvnWindowSetNumActiveTextures", h_window_set_num_active_textures},
    {"nvnWindowGetCrop", h_window_get_crop},
    {"nvnWindowGetNativeWindow", h_window_get_native_window},
    {"nvnWindowGetNumActiveTextures", h_window_get_num_active_textures},
    {"nvnWindowGetNumTextures", h_window_get_num_textures},
    {"nvnWindowGetPresentInterval", h_window_get_present_interval},
    {"nvnSyncInitialize", h_sync_initialize},
    {"nvnSyncFinalize", h_sync_finalize},
    {"nvnSyncInitializeFromFencedGLSync", h_sync_initialize_from_fenced_gl_sync},
    {"nvnSyncCreateGLSync", h_sync_create_gl_sync},
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
    {"nvnTextureGetDebugID", h_texture_get_debug_id},
    {"nvnTextureGetDepthStencilMode", h_texture_get_depth_stencil_mode},
    {"nvnTextureGetFlags", h_texture_get_flags},
    {"nvnTextureGetFormat", h_texture_get_format},
    {"nvnTextureGetLevels", h_texture_get_levels},
    {"nvnTextureGetMemoryOffset", h_texture_get_memory_offset},
    {"nvnTextureGetMemoryPool", h_texture_get_memory_pool},
    {"nvnTextureGetRawStorageClass", h_texture_get_raw_storage_class},
    {"nvnTextureGetStorageClass", h_texture_get_storage_class},
    {"nvnTextureGetZCullStorageSize", h_texture_get_zcull_storage_size},
    {"nvnTextureGetSparseTileLayout", h_texture_get_sparse_tile_layout},
    {"nvnTextureGetViewOffset", h_texture_get_view_offset},
    {"nvnTextureGetSamples", h_texture_get_samples},
    {"nvnTextureGetSwizzle", h_texture_get_swizzle},
    {"nvnTextureGetStorageSize", h_texture_get_storage_size},
    {"nvnTextureGetTextureAddress", h_texture_get_texture_address},
    {"nvnTextureCompare", h_texture_compare},
    {"nvnTextureViewCompare", h_texture_view_compare},
    {"nvnTexturePoolGetMemoryPool", h_texture_pool_get_memory_pool},
    {"nvnTexturePoolGetMemoryOffset", h_texture_pool_get_memory_offset},
    {"nvnTexturePoolRegisterTexture", h_texture_pool_register_texture},
    {"nvnTexturePoolRegisterImage", h_texture_pool_register_image},
    {"nvnTextureWriteTexels", h_texture_write_texels},
    {"nvnTextureWriteTexelsStrided", h_texture_write_texels_strided},
    {"nvnBufferBuilderSetDevice", h_buffer_builder_set_device},
    {"nvnBufferBuilderSetDefaults", h_buffer_builder_set_defaults},
    {"nvnBufferBuilderSetStorage", h_buffer_builder_set_storage},
    {"nvnBufferBuilderGetSize", h_buffer_builder_get_size},
    {"nvnBufferBuilderGetDevice", h_buffer_builder_get_device},
    {"nvnBufferBuilderGetMemoryPool", h_buffer_builder_get_memory_pool},
    {"nvnBufferBuilderGetMemoryOffset", h_buffer_builder_get_memory_offset},
    {"nvnBufferInitialize", h_buffer_initialize},
    {"nvnBufferFinalize", h_buffer_finalize},
    {"nvnBufferFlushMappedRange", h_buffer_flush_mapped_range},
    {"nvnBufferInvalidateMappedRange", h_buffer_invalidate_mapped_range},
    {"nvnBufferMap", h_buffer_map},
    {"nvnBufferGetAddress", h_buffer_get_address},
    {"nvnBufferGetSize", h_buffer_get_size},
    {"nvnBufferGetDebugID", h_buffer_get_debug_id},
    {"nvnBufferGetMemoryPool", h_buffer_get_memory_pool},
    {"nvnBufferGetMemoryOffset", h_buffer_get_memory_offset},
    {"nvnBufferSetDebugLabel", h_buffer_set_debug_label},
    {"nvnDeviceSetDebugLabel", h_library_ret},
    {"nvnCommandBufferSetDebugLabel", h_library_ret},
    {"nvnMemoryPoolSetDebugLabel", h_library_ret},
    {"nvnProgramSetDebugLabel", h_library_ret},
    {"nvnQueueSetDebugLabel", h_library_ret},
    {"nvnSamplerSetDebugLabel", h_library_ret},
    {"nvnSyncSetDebugLabel", h_library_ret},
    {"nvnTextureSetDebugLabel", h_library_ret},
    {"nvnWindowSetDebugLabel", h_library_ret},
    {"nvnTexturePoolSetDebugLabel", h_library_ret},
    {"nvnSamplerPoolSetDebugLabel", h_library_ret},
    {"nvnProgramInitialize", h_program_initialize},
    {"nvnProgramSetShadersExt", h_program_set_shaders_ext},
    {"nvnProgramSetShaders", h_program_set_shaders},
    {"nvnProgramSetSampleShading", h_program_set_sample_shading},
    {"nvnProgramSetSubroutineLinkage", h_program_set_subroutine_linkage},
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
    {"nvnCommandBufferCopyCommands", h_command_buffer_copy_commands},
    {"nvnCommandBufferSetCopyRowStride", h_command_buffer_set_copy_row_stride},
    {"nvnCommandBufferSetCopyImageStride", h_command_buffer_set_copy_image_stride},
    {"nvnCommandBufferGetCopyRowStride", h_command_buffer_get_copy_row_stride},
    {"nvnCommandBufferGetCopyImageStride", h_command_buffer_get_copy_image_stride},
    {"nvnCommandBufferGetMemoryCallback", h_command_buffer_get_memory_callback},
    {"nvnCommandBufferGetMemoryCallbackData", h_command_buffer_get_memory_callback_data},
    {"nvnCommandBufferIsRecording", h_command_buffer_is_recording},
    {"nvnCommandBufferGetControlMemorySize", h_command_buffer_get_control_memory_size},
    {"nvnCommandBufferSetCommandMemoryCallbackEnabled", h_command_buffer_set_command_memory_callback_enabled},
    {"nvnCommandBufferGetCommandMemorySize", h_command_buffer_get_command_memory_size},
    {"nvnCommandBufferGetCommandMemoryUsed", h_command_buffer_get_command_memory_used},
    {"nvnCommandBufferGetCommandMemoryFree", h_command_buffer_get_command_memory_free},
    {"nvnCommandBufferGetControlMemoryUsed", h_command_buffer_get_control_memory_used},
    {"nvnCommandBufferGetControlMemoryFree", h_command_buffer_get_control_memory_free},
    {"nvnCommandBufferInsertCheckpointDynamic", h_debug_checkpoint},
    {"nvnCommandBufferInsertCheckpointStatic", h_debug_checkpoint},
    {"nvnCommandBufferSetTexturePool", h_command_buffer_set_texture_pool},
    {"nvnCommandBufferSetSamplerPool", h_command_buffer_set_sampler_pool},
    {"nvnCommandBufferBindTexture", h_command_buffer_bind_texture},
    {"nvnCommandBufferBindTextures", h_command_buffer_bind_textures},
    {"nvnCommandBufferBindSeparateTexture", h_command_buffer_bind_separate_texture},
    {"nvnCommandBufferBindSeparateTextures", h_command_buffer_bind_separate_textures},
    {"nvnCommandBufferBindSeparateSampler", h_command_buffer_bind_separate_sampler},
    {"nvnCommandBufferBindSeparateSamplers", h_command_buffer_bind_separate_samplers},
    {"nvnCommandBufferBindCoverageModulationTable", h_command_buffer_bind_coverage_modulation_table},
    {"nvnCommandBufferSetProgramSubroutines", h_command_buffer_set_program_subroutines},
    {"nvnCommandBufferSetShaderScratchMemory", h_command_buffer_set_shader_scratch_memory},
    {"nvnCommandBufferSetTiledCacheAction", h_command_buffer_set_tiled_cache_action},
    {"nvnCommandBufferSetTiledCacheTileSize", h_command_buffer_set_tiled_cache_tile_size},
    {"nvnCommandBufferInsertDebugMarker", h_command_buffer_insert_debug_marker},
    {"nvnCommandBufferInsertDebugMarkerDynamic", h_command_buffer_insert_debug_marker_dynamic},
    {"nvnCommandBufferInsertDebugMarkerStatic", h_command_buffer_insert_debug_marker_static},
    {"nvnCommandBufferPushDebugGroup", h_command_buffer_push_debug_group},
    {"nvnCommandBufferPushDebugGroupDynamic", h_command_buffer_push_debug_group_dynamic},
    {"nvnCommandBufferPushDebugGroupStatic", h_command_buffer_push_debug_group_static},
    {"nvnCommandBufferPopDebugGroup", h_command_buffer_pop_debug_group},
    {"nvnCommandBufferPopDebugGroupId", h_command_buffer_pop_debug_group_id},
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
    {"nvnBlendStateGetAdvancedMode", h_blend_state_get_advanced_mode},
    {"nvnBlendStateGetAdvancedOverlap", h_blend_state_get_advanced_overlap},
    {"nvnBlendStateGetAdvancedPremultipliedSrc", h_blend_state_get_advanced_premultiplied_src},
    {"nvnBlendStateGetAdvancedNormalizedDst", h_blend_state_get_advanced_normalized_dst},
    {"nvnBlendStateSetAdvancedMode", h_blend_state_set_advanced_mode},
    {"nvnBlendStateSetAdvancedOverlap", h_blend_state_set_advanced_overlap},
    {"nvnBlendStateSetAdvancedPremultipliedSrc", h_blend_state_set_advanced_premultiplied_src},
    {"nvnBlendStateSetAdvancedNormalizedDst", h_blend_state_set_advanced_normalized_dst},
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
    {"nvnMultisampleStateGetSampleLocationsEnable", h_multisample_state_get_sample_locations_enable},
    {"nvnMultisampleStateGetSampleLocationsGrid", h_multisample_state_get_sample_locations_grid},
    {"nvnMultisampleStateGetSampleLocationsGridEnable", h_multisample_state_get_sample_locations_grid_enable},
    {"nvnMultisampleStateSetSampleLocations", h_multisample_state_set_sample_locations},
    {"nvnEventBuilderSetDefaults", h_event_builder_set_defaults},
    {"nvnEventBuilderSetStorage", h_event_builder_set_storage},
    {"nvnEventBuilderGetMemoryPool", h_event_builder_get_memory_pool},
    {"nvnEventBuilderGetMemoryOffset", h_event_builder_get_memory_offset},
    {"nvnEventBuilderGetStorage", h_event_builder_get_storage},
    {"nvnEventInitialize", h_event_initialize},
    {"nvnEventFinalize", h_event_finalize},
    {"nvnEventGetValue", h_event_get_value},
    {"nvnEventGetMemoryPool", h_event_get_memory_pool},
    {"nvnEventGetMemoryOffset", h_event_get_memory_offset},
    {"nvnEventSignal", h_event_signal},
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
    {"nvnCommandBufferSetScissor", h_command_buffer_set_scissor},
    {"nvnCommandBufferSetViewport", h_command_buffer_set_viewport},
    {"nvnCommandBufferSetViewportSwizzles", h_command_buffer_set_viewport_swizzles},
    {"nvnCommandBufferSetSampleMask", h_command_buffer_set_sample_mask},
    {"nvnCommandBufferSetConservativeRasterEnable", h_command_buffer_set_conservative_raster_enable},
    {"nvnCommandBufferSetConservativeRasterDilate", h_command_buffer_set_conservative_raster_dilate},
    {"nvnCommandBufferSetSubpixelPrecisionBias", h_command_buffer_set_subpixel_precision_bias},
    {"nvnCommandBufferSetRenderEnable", h_command_buffer_set_render_enable},
    {"nvnCommandBufferSetRenderEnableConditional", h_command_buffer_set_render_enable_conditional},
    {"nvnCommandBufferSaveZCullData", h_command_buffer_save_zcull_data},
    {"nvnCommandBufferRestoreZCullData", h_command_buffer_restore_zcull_data},
    {"nvnCommandBufferSetStencilCullCriteria", h_command_buffer_set_stencil_cull_criteria},
    {"nvnCommandBufferSetColorReductionEnable", h_command_buffer_set_color_reduction_enable},
    {"nvnCommandBufferSetColorReductionThresholds", h_command_buffer_set_color_reduction_thresholds},
    {"nvnCommandBufferClearColor", h_command_buffer_clear_color},
    {"nvnCommandBufferClearColori", h_command_buffer_clear_colori},
    {"nvnCommandBufferClearColorui", h_command_buffer_clear_colorui},
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
    {"nvnCommandBufferCopyBufferToTexture", h_command_buffer_copy_buffer_to_texture},
    {"nvnCommandBufferCopyTextureToBuffer", h_command_buffer_copy_texture_to_buffer},
    {"nvnCommandBufferDownsample", h_command_buffer_downsample},
    {"nvnCommandBufferDownsampleTextureView", h_command_buffer_downsample_texture_view},
    {"nvnCommandBufferTiledDownsample", h_command_buffer_tiled_downsample},
    {"nvnCommandBufferTiledDownsampleTextureView", h_command_buffer_tiled_downsample_texture_view},
    {"nvnCommandBufferCopyBufferToBuffer", h_command_buffer_copy_buffer_to_buffer},
    {"nvnCommandBufferClearBuffer", h_command_buffer_clear_buffer},
    {"nvnCommandBufferResolveDepthBuffer", h_command_buffer_resolve_depth_buffer},
    {"nvnCommandBufferDiscardColor", h_command_buffer_discard_color},
    {"nvnCommandBufferDiscardDepthStencil", h_command_buffer_discard_depth_stencil},
    {"nvnCommandBufferUpdateUniformBuffer", h_command_buffer_update_uniform_buffer},
    {"nvnCommandBufferReportCounter", h_command_buffer_report_counter},
    {"nvnCommandBufferReportValue", h_command_buffer_report_value},
    {"nvnCommandBufferSignalEvent", h_command_buffer_signal_event},
    {"nvnCommandBufferWaitEvent", h_command_buffer_wait_event},
    {"nvnCommandBufferFenceSync", h_command_buffer_fence_sync},
    {"nvnCommandBufferWaitSync", h_command_buffer_wait_sync},
    {"nvnCommandBufferResetCounter", h_command_buffer_reset_counter},
    {"nvnCommandBufferBeginTransformFeedback", h_command_buffer_begin_transform_feedback},
    {"nvnCommandBufferEndTransformFeedback", h_command_buffer_end_transform_feedback},
    {"nvnCommandBufferPauseTransformFeedback", h_command_buffer_pause_transform_feedback},
    {"nvnCommandBufferResumeTransformFeedback", h_command_buffer_resume_transform_feedback},
    {"nvnCommandBufferDrawArraysIndirect", h_command_buffer_draw_arrays_indirect},
    {"nvnCommandBufferDrawElementsIndirect", h_command_buffer_draw_elements_indirect},
    {"nvnCommandBufferMultiDrawArraysIndirectCount", h_command_buffer_multi_draw_arrays_indirect_count},
    {"nvnCommandBufferMultiDrawElementsIndirectCount", h_command_buffer_multi_draw_elements_indirect_count},
    {"nvnCommandBufferDispatchComputeIndirect", h_command_buffer_dispatch_compute_indirect},
    {"nvnCommandBufferDrawTexture", h_command_buffer_draw_texture},
    {"nvnCommandBufferDrawTransformFeedback", h_command_buffer_draw_transform_feedback},
    {"nvnCommandBufferSetPatchSize", h_command_buffer_set_patch_size},
    {"nvnCommandBufferSetInnerTessellationLevels", h_command_buffer_set_inner_tessellation_levels},
    {"nvnCommandBufferSetOuterTessellationLevels", h_command_buffer_set_outer_tessellation_levels},
    {"nvnCommandBufferSetPrimitiveRestart", h_command_buffer_set_primitive_restart},
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
static u64 getproc_addr; /* the address of nvnDeviceGetProcAddress, hooked */
static int active;
static void getproc_native(CPU* c, u64 entry);

/* The NVN library of the sdk module of update 1.1.1: the one build whose name and address tables the host knows. The
   names have no "nvn" prefix, are sorted, and have one guest address each. */
#define NVN_PROC_NAME_BYTES 128u
typedef struct {
    u8 build_id[32]; /* the sdk module's build ID, the first 20 bytes, then zero */
    u32 count;       /* 523 names and 523 addresses */
    u32 off_offsets; /* 0xABE798: 523 u32 offsets from off_strings, guest 0xDE85798 */
    u32 off_strings; /* 0xABAFD5: the NUL-terminated names, guest 0xDE81FD5 */
    u32 off_addrs;   /* 0xC63E58: 523 u64 guest addresses, guest 0xE02AE58 */
} NvnLibrary;
static const NvnLibrary known_library = {
    .build_id = {0xb3, 0x9a, 0xdd, 0x63, 0x42, 0x1b, 0xe5, 0x25, 0xd3, 0x52,
                 0xe5, 0x76, 0xda, 0xb4, 0x38, 0xd3, 0x81, 0x2e, 0xfd, 0xf0},
    .count = 523u,
    .off_offsets = 0xABE798u,
    .off_strings = 0xABAFD5u,
    .off_addrs = 0xC63E58u,
};
/* The host's copy of the known library's tables. It lives until exit and is filled once, under slots_lock. */
typedef struct {
    char* storage;      /* count names of NVN_PROC_NAME_BYTES, without the "nvn" prefix */
    const char** names; /* storage + index * NVN_PROC_NAME_BYTES */
    u64* addrs;
} ProcTable;
static ProcTable proc_table;
static int proc_table_ready;

/* Reads the known library's tables once. Called under slots_lock. Each failure is fatal: a build without the tables
   cannot answer a lookup, and a faked address would run the wrong function. */
static void proc_table_build(void) {
    const SnxModInfo* sdk = NULL;
    for (u32 i = 0; g_mod && i < g_mod->nmods; i++)
        if (!strcmp(g_mod->mods[i].name, "sdk")) sdk = &g_mod->mods[i];
    if (!sdk) fatal("nvn-native: the game has no sdk module, so the NVN name table is unknown\n");
    if (memcmp(sdk->build_id, known_library.build_id, sizeof sdk->build_id))
        fatal("nvn-native: the sdk build is not the one the NVN name table was read from\n");
    u64 text_lo = sdk->base + sdk->text_off;
    u64 text_hi = text_lo + sdk->text_size;
    u64 ro_lo = sdk->base + sdk->ro_off;
    u64 ro_hi = ro_lo + sdk->ro_size;
    u64 data_lo = sdk->base + sdk->data_off;
    u64 data_hi = data_lo + sdk->data_size;
    u64 strings = sdk->base + known_library.off_strings;
    u64 offsets = sdk->base + known_library.off_offsets;
    u64 address_table = sdk->base + known_library.off_addrs;
    if (!nvn_span_inside(offsets, 4ull * known_library.count, ro_lo, ro_hi))
        fatal("nvn-native: the NVN name offset table is not inside the sdk rodata\n");
    if (!nvn_span_inside(address_table, 8ull * known_library.count, data_lo, data_hi))
        fatal("nvn-native: the NVN address table is not inside the sdk data\n");
    char* storage = calloc(known_library.count, NVN_PROC_NAME_BYTES);
    const char** names = calloc(known_library.count, sizeof *names);
    u64* addrs = calloc(known_library.count, sizeof *addrs);
    if (!storage || !names || !addrs) fatal("nvn-native: cannot allocate the NVN name table\n");
    for (u32 n = 0; n < known_library.count; n++) {
        u32 offset = gr32(offsets + 4ull * n);
        u64 name_at = strings + offset;
        names[n] = storage + (u64)n * NVN_PROC_NAME_BYTES;
        if (!nvn_span_inside(name_at, 1, ro_lo, ro_hi))
            fatal("nvn-native: name %u of the NVN library is outside the sdk rodata\n", n);
        NvnNameRead result = nvn_guest_name_read(name_at, (char*)names[n]);
        if (result != NVN_NAME_OK)
            fatal("nvn-native: name %u of the NVN library is not usable (result %d)\n", n, (int)result);
        if (!nvn_span_inside(name_at, strlen(names[n]) + 1, ro_lo, ro_hi))
            fatal("nvn-native: name %u of the NVN library is not inside the sdk rodata\n", n);
        addrs[n] = gr64(address_table + 8ull * n);
    }
    int bad = nvn_proc_table_check(names, addrs, known_library.count, text_lo, text_hi);
    if (bad >= 0)
        fatal("nvn-native: NVN name %d (%s) or its address is bad\n", bad, names[bad] ? names[bad] : "empty");
    proc_table.storage = storage;
    proc_table.names = names;
    proc_table.addrs = addrs;
    __atomic_store_n(&proc_table_ready, 1, __ATOMIC_RELEASE);
}

/* The guest address of the NVN function name in the known library, or 0 when the library does not have it. */
static u64 host_lookup(const char* name) {
    if (!__atomic_load_n(&proc_table_ready, __ATOMIC_ACQUIRE)) {
        pm_lock(&slots_lock);
        if (!proc_table_ready) proc_table_build();
        pm_unlock(&slots_lock);
    }
    int index = nvn_proc_name_index(proc_table.names, known_library.count, name);
    return index < 0 ? 0 : proc_table.addrs[index];
}

/* The known library has no function under the name (the library would try its NVX extension list next). Any answer
   but 0 would be invented, so the host stops unless nvn_native_missing=0. */
static void lookup_missing(const char* name) {
    if (g_settings.nvn_native_missing)
        fatal("nvn-native: the game asked for \"%s\", which the NVN library of this build does not have\n", name);
    LOG_LIMIT(16, "nvn-native: the game asked for \"%s\", which the NVN library of this build does not have\n", name);
}

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
    if (!__atomic_fetch_add(&s->calls, 1, __ATOMIC_RELAXED)) { /* the first call: what the game asked for */
        if (g_settings.nvn_native_missing)
            fatal("nvn-native: not implemented: %s (x0=%llx x1=%llx x2=%llx x3=%llx)\n", s->name,
                  (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2],
                  (unsigned long long)c->x[3]);
        LOG("nvn-native: not implemented: %s (x0=%llx x1=%llx x2=%llx x3=%llx), returns 0\n", s->name,
            (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2],
            (unsigned long long)c->x[3]);
    }
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

/* One lookup hook: answers the name at pointer from the host table in x0 and returns to the caller. The hooks sit in
   g_disp themselves (no native_call) and run no guest code, so they set c->pc to x30 as a guest RET does. The address,
   or 0 when the library does not have the name. */
static u64 answer_lookup(CPU* c, const char* hook, u64 pointer, char name[128]) {
    NvnNameRead result = pointer ? nvn_guest_name_read(pointer, name) : NVN_NAME_UNREADABLE;
    if (result == NVN_NAME_UNREADABLE)
        fatal("nvn-native: %s name (%llx) is null or unreadable\n", hook, (unsigned long long)pointer);
    /* An empty or overlong name is not in the table. The library would also find nothing. */
    u64 addr = result == NVN_NAME_OK ? host_lookup(name) : 0;
    if (!addr) lookup_missing(name);
    c->x[0] = addr;
    c->pc = c->x[30];
    return addr;
}

/* nvnDeviceGetProcAddress(device, name). The device is not read: the host answers from the name table, so the library
   lookup never runs (nvn-design.md, 2-3, item 6). The bootstrap hook put this function in the resolver's slot. */
static void getproc_native(CPU* c, u64 entry) {
    (void)entry;
    char name[128];
    u64 addr = answer_lookup(c, "nvnDeviceGetProcAddress", c->x[1], name);
    if (addr && strcmp(name, "nvnDeviceGetProcAddress")) install(name, addr);
}

/* nvnBootstrapLoader(name) */
static void bootstrap_native(CPU* c, u64 entry) {
    (void)entry;
    char name[128];
    u64 addr = answer_lookup(c, "nvnBootstrapLoader", c->x[0], name);
    if (!addr) return;
    if (strcmp(name, "nvnDeviceGetProcAddress")) {
        install(name, addr);
        return;
    }
    pm_lock(&slots_lock);
    GuestFn* slot = &g_disp[(addr - g_code_lo) / 4];
    if (!*slot) {
        pm_unlock(&slots_lock);
        fatal("nvn-native: the nvnDeviceGetProcAddress entry at %llx is empty\n", (unsigned long long)addr);
    }
    if (!getproc_addr) { /* hook once; nvn_trace wraps this slot after the first lookup returns */
        getproc_addr = addr;
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
