/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_params.h"
#include "nx/gpu/texture/blocklinear.h"
#include <stddef.h>
#include <math.h>
#include <string.h>

void nvn_window_builder_defaults(NvnWindowBuilder* b) {
    memset(b, 0, sizeof *b);
    b->interval = 1;
}

void nvn_window_builder_set_device(NvnWindowBuilder* b, uint64_t device) { b->device = device; }
void nvn_window_builder_set_native_window(NvnWindowBuilder* b, uint64_t native_window) { b->native_window = native_window; }
void nvn_window_builder_set_textures(NvnWindowBuilder* b, uint32_t count, uint64_t textures) {
    b->texture_count = count;
    b->textures = textures;
}
void nvn_window_builder_set_interval(NvnWindowBuilder* b, uint32_t interval) { b->interval = interval; }
int nvn_window_initialize_check(uint32_t texture_count) {
    return texture_count != 0 && texture_count <= NVN_WINDOW_MAX_TEXTURES;
}
int32_t nvn_window_pick_image(const uint8_t* states, uint32_t count, int32_t last) {
    if (!states || !count || count > NVN_WINDOW_MAX_TEXTURES) return NVN_WINDOW_NO_IMAGE;
    uint32_t start = last >= 0 && (uint32_t)last < count ? (uint32_t)last + 1u : 0u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = (start + i) % count;
        if (states[index] == NVN_IMAGE_FREE) return (int32_t)index;
    }
    return NVN_WINDOW_NO_IMAGE;
}
NvnSyncWaitResult nvn_sync_wait_result(NvnSyncState state) {
    /* FENCED is signaled by its frame job, which step 2e adds. Until then a wait on it cannot be
       satisfied. */
    return state == NVN_SYNC_SIGNALED ? NVN_SYNC_WAIT_SIGNALED : NVN_SYNC_WAIT_FAILED;
}
NvnWindowCrop nvn_window_crop(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                              uint32_t texture_width, uint32_t texture_height) {
    if (!width || !height) return (NvnWindowCrop){0, 0, texture_width, texture_height};
    return (NvnWindowCrop){x, y, width, height};
}

/* nvnDeviceGetInteger of the run of 2026-11-02 (title screen). The library takes a value from a device field or from a
   constant, and the run does not say which. */
static const struct {
    uint32_t what;
    int32_t value;
} device_integers[] = {
    {4, 256},   {15, 32},   {16, 32},   {17, 256},  {18, 256},  {19, 4},    {20, 8},    {40, 1},
    {41, 1},    {42, 1},    {50, 1},    {59, 1},    {66, 1},    {67, 1},    {68, 1},    {69, 1},
    {71, 1},    {72, 1},    {73, 1},    {74, 1},    {75, 1},    {76, 1},    {82, 65536}, {85, 262144},
    {87, 1},    {90, 0},    {92, 16384},
};

int nvn_device_integer(uint32_t what, int32_t* value) {
    if (what > NVN_DEVICE_INTEGER_LIMIT) {
        *value = 0;
        return 1;
    }
    for (size_t i = 0; i < sizeof device_integers / sizeof device_integers[0]; i++)
        if (device_integers[i].what == what) {
            *value = device_integers[i].value;
            return 1;
        }
    return 0;
}

/* The library's ratio, at device +0x58 and +0x5c: 625 / 384 (the 614.4 MHz GPU timer to 1 GHz) */
#define TIMESTAMP_NUM 0x271u
#define TIMESTAMP_DEN 0x180u

uint64_t nvn_timestamp_ns(uint64_t ticks) {
    uint64_t whole = ticks / TIMESTAMP_DEN, rest = ticks % TIMESTAMP_DEN;
    return whole * TIMESTAMP_NUM + rest * TIMESTAMP_NUM / TIMESTAMP_DEN;
}

/* The library masks no index and sign extends index bit 31: texture bits above 0xFFFFF spill into the sampler field and
   a high index reaches the tag. Both are kept, so a handle matches the library. An image handle has no tag. */
uint64_t nvn_texture_handle(uint32_t texture_index, uint32_t sampler_index) {
    int32_t packed = (int32_t)(texture_index | (sampler_index << NVN_HANDLE_SAMPLER_SHIFT));
    return (uint64_t)(int64_t)packed | NVN_TEXTURE_HANDLE_TAG;
}

uint64_t nvn_image_handle(uint32_t image_index) { return (uint64_t)(int64_t)(int32_t)image_index; }

int nvn_bind_buffer_active(NvnBindKind kind, uint64_t addr, uint64_t size) {
    switch (kind) {
    case NVN_BIND_UNIFORM: return size != 0;
    case NVN_BIND_STORAGE: return 1;
    case NVN_BIND_VERTEX: return size != 0;
    case NVN_BIND_TRANSFORM_FEEDBACK: return addr != 0;
    default: return 0;
    }
}

/* The state objects of a command buffer (analyst-g3-A-1.md and analyst-g3-B-1.md). The formulas keep the library's
   unmasked values and its spills. */
uint32_t nvn_state_insert(uint32_t word, uint32_t mask, uint32_t shift, uint32_t value) {
    return (word & ~mask) | (value << shift);
}

uint32_t nvn_state_set_flag(uint32_t word, uint32_t bit, uint32_t value) {
    uint32_t on = (value & 0xFFu) != 0;
    return (word & ~(1u << bit)) | (on << bit);
}

uint32_t nvn_blend_state_set_blend_func(uint32_t word, uint32_t src_color, uint32_t dst_color,
                                        uint32_t src_alpha, uint32_t dst_alpha) {
    return (word & 0x80808080u) | (src_color & 0x808080FFu) | ((dst_color << 8) & 0x8080FF00u) |
           ((src_alpha << 16) & 0x80FFFFFFu) | (dst_alpha << 24);
}

uint32_t nvn_blend_state_set_blend_equation(uint32_t word, uint32_t rgb, uint32_t alpha) {
    return (word & 0x88FFFFFFu) | ((rgb << 24) & 0x8FFFFFFFu) | (alpha << 28);
}

uint32_t nvn_channel_mask_state_set_channel_mask(uint32_t word, uint32_t target, uint32_t r, uint32_t g,
                                                 uint32_t b, uint32_t a) {
    uint32_t value = ((r & 0xFFu) != 0) | (((g & 0xFFu) != 0) << 1) | (((b & 0xFFu) != 0) << 2) |
                     (((a & 0xFFu) != 0) << 3);
    uint32_t shift = (target << 2) & 31u;
    return (word & ~(0xFu << shift)) | (value << shift);
}

uint32_t nvn_color_state_set_blend_enable(uint32_t word, uint32_t target, uint32_t value) {
    uint32_t bit = target & 31u;
    uint32_t on = (value & 0xFFu) != 0;
    uint32_t byte1 = ((~(1u << bit)) & (word >> 8) & 0xFFu) | (on << bit);
    return (word & 0xFFFF00FFu) | (byte1 << 8);
}

uint32_t nvn_depth_stencil_state_set_stencil_func(uint32_t word, uint32_t face, uint32_t func) {
    if (face & 1u) word = (word & 0xFFFFFFF0u) | func;
    if (face & 2u) word = (word & 0xFFF0FFFFu) | (func << 16);
    return word;
}

uint32_t nvn_depth_stencil_state_set_stencil_op(uint32_t word, uint32_t face, uint32_t fail, uint32_t zfail,
                                                uint32_t zpass) {
    if (face & 1u)
        word = (word & 0xFFFF000Fu) | ((fail << 4) & 0xFFFF00FFu) | (zfail << 12) | (zpass << 8);
    if (face & 2u)
        word = (word & 0xFFFFFu) | ((fail & 0xFu) << 20) | ((zfail & 0xFu) << 28) | (zpass << 24);
    return word;
}

uint32_t nvn_vertex_attrib_state_set_format(uint32_t word, uint32_t format, uint32_t relative_offset) {
    return (word & 0x8000003Fu) | (format << 21) | (relative_offset << 7) | 0x40u;
}

uint32_t nvn_vertex_attrib_state_set_stream_index(uint32_t word, uint32_t stream_index) {
    return (word & 0xFFFFFFE0u) | stream_index;
}

void nvn_color_state_defaults(uint32_t* words) { words[0] = 0x08030000u; }

void nvn_depth_stencil_state_defaults(uint32_t* words) {
    words[0] = 0x00000022u;
    words[1] = 0x11180118u;
}

void nvn_blend_state_defaults(uint32_t* words) {
    words[0] = 0x110C0000u;
    words[1] = 0x01020102u;
}

void nvn_polygon_state_defaults(uint32_t* words) { words[0] = 0x14u; }

void nvn_multisample_state_defaults(uint32_t* words) {
    words[0] = 5;
    for (uint32_t i = 1; i <= 4; i++) words[i] = 0x88888888u;
    words[5] = 0;
}

void nvn_channel_mask_state_defaults(uint32_t* words) { words[0] = 0xFFFFFFFFu; }

uint32_t nvn_blend_state_get_blend_target(uint32_t word) { return word & 7u; }

void nvn_blend_state_get_blend_func(uint32_t word, uint32_t out[4]) {
    for (uint32_t i = 0; i < 4; i++) out[i] = (word >> (8u * i)) & 0x7Fu;
}

void nvn_channel_mask_state_get_channel_mask(uint32_t word, uint32_t target, uint8_t out[4]) {
    uint32_t shift = (target << 2) & 31u;
    for (uint32_t i = 0; i < 4; i++) out[i] = (uint8_t)((word >> (shift + i)) & 1u);
}

uint32_t nvn_color_state_get_alpha_test(uint32_t word) { return (word >> 24) & 0xFu; }

uint32_t nvn_color_state_get_blend_enable(uint32_t word, uint32_t target) {
    return (((word >> 8) & 0xFFu) >> (target & 31u)) & 1u;
}

uint32_t nvn_depth_stencil_state_get_depth_func(uint32_t word) { return (word >> 4) & 0xFu; }
uint32_t nvn_depth_stencil_state_get_depth_test_enable(uint32_t word) { return word & 1u; }
uint32_t nvn_depth_stencil_state_get_depth_write_enable(uint32_t word) { return (word >> 1) & 1u; }
uint32_t nvn_multisample_state_get_multisample_enable(uint32_t word) { return word & 1u; }
uint32_t nvn_multisample_state_get_samples(uint32_t word) { return (word >> 8) & 0x1Fu; }
uint32_t nvn_multisample_state_get_alpha_to_coverage_enable(uint32_t word) { return (word >> 1) & 1u; }
uint32_t nvn_multisample_state_get_alpha_to_coverage_dither(uint32_t word) { return (word >> 2) & 1u; }
uint32_t nvn_multisample_state_get_coverage_modulation_mode(uint32_t word) { return (word >> 13) & 3u; }
uint32_t nvn_multisample_state_get_raster_samples(uint32_t word) { return (word >> 15) & 0x1Fu; }
uint32_t nvn_multisample_state_get_coverage_to_color_enable(uint32_t word) { return (word >> 20) & 1u; }
uint32_t nvn_multisample_state_get_coverage_to_color_output(uint32_t word) { return (word >> 21) & 7u; }
uint32_t nvn_polygon_state_get_cull_face(uint32_t word) { return word & 3u; }
uint32_t nvn_polygon_state_get_front_face(uint32_t word) { return (word >> 2) & 1u; }
uint32_t nvn_color_state_get_logic_op(uint32_t word) { return (word >> 16) & 0xFFu; }
uint32_t nvn_depth_stencil_state_get_stencil_test_enable(uint32_t word) { return (word >> 2) & 1u; }

uint32_t nvn_depth_stencil_state_get_stencil_func(uint32_t word, uint32_t face) {
    return face == 2u ? (word >> 16) & 0xFu : word & 0xFu;
}

uint32_t nvn_polygon_state_get_polygon_mode(uint32_t word) { return (word >> 3) & 3u; }
uint32_t nvn_polygon_state_get_polygon_offset_enables(uint32_t word) { return (word >> 15) & 0xFu; }

void nvn_depth_stencil_state_get_stencil_op(uint32_t word, uint32_t face, uint32_t out[3]) {
    if (face == 2u) {
        out[0] = (word >> 20) & 0xFu;
        out[1] = (word >> 28) & 0xFu;
        out[2] = (word >> 24) & 0xFu;
    } else {
        out[0] = (word >> 4) & 0xFu;
        out[1] = (word >> 12) & 0xFu;
        out[2] = (word >> 8) & 0xFu;
    }
}

void nvn_blend_state_get_blend_equation(uint32_t word, uint32_t* rgb, uint32_t* alpha) {
    if (rgb) *rgb = (word >> 24) & 7u;
    if (alpha) *alpha = (word >> 28) & 7u;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

int nvn_raster_methods(NvnRasterKind which, const uint32_t w[3], NvnRasterMethod out[NVN_RASTER_METHODS_MAX],
                       uint32_t* count) {
    uint32_t n = 0;
    switch (which) {
    case NVN_RASTER_DEPTH_BOUNDS: {
        uint32_t enabled = (w[0] & 0xFFu) != 0;
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_DEPTH_BOUNDS, enabled};
        if (enabled) {
            out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_DEPTH_BOUNDS_MIN, w[1]};
            out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_DEPTH_BOUNDS_MAX, w[2]};
        }
        break;
    }
    case NVN_RASTER_DISCARD:
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_DISCARD, (w[0] & 0xFFu) == 0 ? 1u : 0u};
        break;
    case NVN_RASTER_POLYGON_OFFSET_CLAMP: {
        float units;
        memcpy(&units, &w[1], sizeof units);
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_POLYGON_OFFSET, w[0]};
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_POLYGON_OFFSET_UNITS, float_bits(units + units)};
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_POLYGON_OFFSET_CLAMP, w[2]};
        break;
    }
    case NVN_RASTER_DEPTH_CLAMP:
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_DEPTH_CLAMP,
                                     (w[0] & 0xFFu) != 0 ? NVN_RASTER_DEPTH_CLAMP_NEAR : NVN_RASTER_DEPTH_CLAMP_FAR};
        break;
    case NVN_RASTER_LINE_WIDTH:
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_LINE_WIDTH, w[0]};
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_LINE_WIDTH_ALIAS, w[0]};
        break;
    default:
        return 0;
    }
    *count = n;
    return 1;
}

void nvn_depth_range_methods(uint32_t index, uint32_t depth_mode, float near_value, float far_value,
                             NvnDepthRangeMethod out[NVN_DEPTH_RANGE_METHODS_MAX]) {
    float fraction = depth_mode == 1u ? far_value - near_value : (far_value - near_value) * 0.5f;
    float translate = depth_mode == 1u ? near_value : (near_value + far_value) * 0.5f;
    float lowest = near_value < far_value ? near_value : far_value;
    float highest = near_value > far_value ? near_value : far_value;
    uint32_t method_stride = NVN_DEPTH_RANGE_METHOD_STRIDE * index;
    uint32_t clip_stride = NVN_DEPTH_RANGE_CLIP_STRIDE * index;
    out[0] = (NvnDepthRangeMethod){NVN_DEPTH_RANGE_METHOD_FRACTION + method_stride, float_bits(fraction)};
    out[1] = (NvnDepthRangeMethod){NVN_DEPTH_RANGE_METHOD_TRANSLATE + method_stride, float_bits(translate)};
    out[2] = (NvnDepthRangeMethod){NVN_DEPTH_RANGE_METHOD_MIN + clip_stride, float_bits(lowest)};
    out[3] = (NvnDepthRangeMethod){NVN_DEPTH_RANGE_METHOD_MAX + clip_stride, float_bits(highest)};
}

const NvnQueueDeviceWords nvn_queue_device_words = {.w8c = 0x80, .w94 = 2, .w98 = 1, .we0 = 1, .we4 = 0x1000, .w1af4 = 0};

void nvn_queue_builder_defaults(NvnQueueBuilder* b) {
    uint64_t device = b->device; /* SetDefaults does not touch the device */
    memset(b, 0, sizeof *b);
    b->device = device;
    b->priority = NVN_QUEUE_PRIORITY_DEFAULT;
}

#define QUEUE_FLAG_NO_A 0x1u /* skips the first term (NVN: NO_FRAGMENT_INTERLOCK, name not verified) */
#define QUEUE_FLAG_NO_B 0x2u /* skips the aligned term (NVN: NO_ZCULL, name not verified) */
#define DEFAULT_COMMAND_SIZE 0x10000
#define DEFAULT_COMPUTE_SIZE 0x40000
#define DEFAULT_CONTROL_SIZE 0x4000
#define DEV_FLAG_BIT8 0x100u /* of device word +0x1af4: it picks the sizes of the per-slot areas */

/* The port of the library function at d8383f0 (update 1.1.1), step by step, in the same integer widths. */
uint64_t nvn_queue_memory_size(const NvnQueueBuilder* b, const NvnQueueDeviceWords* dev) {
    int64_t command = b->has_command ? b->command : DEFAULT_COMMAND_SIZE;
    int64_t control = b->has_control ? b->control : DEFAULT_CONTROL_SIZE;
    int64_t compute = b->has_compute ? b->compute : DEFAULT_COMPUTE_SIZE;
    int bit8 = (dev->w1af4 & DEV_FLAG_BIT8) != 0;

    uint32_t area = 0;
    if (!(b->flags & QUEUE_FLAG_NO_A)) area = (0xFFu | (dev->w98 << 10)) + ((dev->w8c * dev->w94) << 2);
    area += bit8 ? 0x9400u : 0x7B00u;
    if (!(b->flags & QUEUE_FLAG_NO_B) && dev->we0) {
        uint32_t align = dev->we4, mask = -align, last = align - 1;
        uint32_t first = (dev->we0 + last) & mask;
        area = ((area + last) & mask);
        area += ((first + last) & mask);
    }
    uint64_t slot = bit8 ? 0x700u : 0x200u;
    uint64_t slots = (uint64_t)compute / (slot + 0x600u);
    area = (area + 0xFFu) & 0xFFFFFF00u;
    area += (uint32_t)slots * 0x600u;
    area += (uint32_t)slots * (uint32_t)slot;
    area = (area + 0xFFFu) & 0xFFFFF000u;
    return (uint64_t)(command + control) + area;
}

void nvn_memory_pool_builder_defaults(NvnMemoryPoolBuilder* b) {
    uint64_t device = b->device;
    memset(b, 0, sizeof *b);
    b->device = device;
    b->flags = NVN_POOL_DEFAULT_FLAGS;
}

NvnPoolResult nvn_memory_pool_params(const NvnMemoryPoolBuilder* b, uint64_t page_size, uint32_t* flags) {
    uint32_t f = b->flags;
    uint32_t cpu = (f & NVN_POOL_CPU_UNCACHED) ? NVN_POOL_CPU_UNCACHED :
                   (f & NVN_POOL_CPU_CACHED) ? NVN_POOL_CPU_CACHED :
                   (f & NVN_POOL_CPU_NO_ACCESS) ? NVN_POOL_CPU_NO_ACCESS : NVN_POOL_CPU_UNCACHED;
    uint32_t gpu = (f & NVN_POOL_GPU_CACHED) ? NVN_POOL_GPU_CACHED :
                   (f & NVN_POOL_GPU_UNCACHED) ? NVN_POOL_GPU_UNCACHED :
                   (f & NVN_POOL_GPU_NO_ACCESS) ? NVN_POOL_GPU_NO_ACCESS : NVN_POOL_GPU_CACHED;
    if (f & NVN_POOL_VIRTUAL) cpu = NVN_POOL_CPU_NO_ACCESS;
    if (f & NVN_POOL_PHYSICAL) gpu = NVN_POOL_GPU_NO_ACCESS;
    *flags = (f & ~(NVN_POOL_CPU_NO_ACCESS | NVN_POOL_CPU_UNCACHED | NVN_POOL_CPU_CACHED |
                    NVN_POOL_GPU_NO_ACCESS | NVN_POOL_GPU_UNCACHED | NVN_POOL_GPU_CACHED)) | cpu | gpu;
    if (f & NVN_POOL_VIRTUAL) return NVN_POOL_UNSUPPORTED_VIRTUAL;
    if (f & NVN_POOL_IMPORT) return NVN_POOL_UNSUPPORTED_IMPORT;
    if (!b->storage || !b->size || b->storage % NVN_POOL_STORAGE_ALIGNMENT ||
        b->size % NVN_POOL_STORAGE_ALIGNMENT) return NVN_POOL_INVALID_STORAGE;
    if (page_size < NVN_POOL_STORAGE_ALIGNMENT || page_size % NVN_POOL_STORAGE_ALIGNMENT ||
        ((f & NVN_POOL_PHYSICAL) && b->size % page_size)) return NVN_POOL_INVALID_PAGE_SIZE;
    return NVN_POOL_OK;
}

int nvn_pool_range_fits(uint64_t pool_size, uint64_t offset, uint64_t size) {
    return offset <= pool_size && size <= pool_size - offset;
}

NvnBufferResult nvn_buffer_check(uint32_t pool_flags, uint64_t pool_size, uint64_t offset, uint64_t size) {
    if (pool_flags & NVN_POOL_PHYSICAL) return NVN_BUFFER_PHYSICAL_POOL;
    return nvn_pool_range_fits(pool_size, offset, size) ? NVN_BUFFER_OK : NVN_BUFFER_OUT_OF_POOL;
}

int nvn_descriptor_offset(uint64_t pool_offset, uint32_t count, uint64_t memory_size, uint32_t entry_bytes,
                          int32_t index, uint64_t* offset) {
    if (index < 0 || (uint32_t)index >= count || pool_offset > memory_size) return 0;
    if ((uint64_t)count * entry_bytes > memory_size - pool_offset) return 0;
    *offset = pool_offset + (uint64_t)(uint32_t)index * entry_bytes;
    return 1;
}

int nvn_pool_gpu_allocate(uint64_t cursor, uint64_t size, uint64_t page_size,
                          uint64_t* address, uint64_t* next) {
    if (!size || page_size < NVN_POOL_STORAGE_ALIGNMENT || page_size % NVN_POOL_STORAGE_ALIGNMENT ||
        cursor < NVN_POOL_GPU_BASE || cursor >= NVN_POOL_GPU_END) return 0;
    uint64_t padding = (page_size - cursor % page_size) % page_size;
    if (padding >= NVN_POOL_GPU_END - cursor) return 0;
    uint64_t base = cursor + padding;
    if (size > NVN_POOL_GPU_END - base) return 0;
    uint64_t end = base + size;
    padding = (page_size - end % page_size) % page_size;
    if (padding > NVN_POOL_GPU_END - end) return 0;
    *address = base;
    *next = end + padding;
    return 1;
}

uint64_t nvn_memory_pool_map(uint32_t flags, uint64_t storage) {
    return (flags & NVN_POOL_CPU_NO_ACCESS) ? 0 : storage;
}

uint32_t nvn_memory_pool_flags(uint32_t flags) { return flags & NVN_POOL_PUBLIC_FLAGS; }

const NvnSamplerDeviceWords nvn_sampler_device_words = {
    NVN_SAMPLER_DEVICE_LOD_BIAS, NVN_SAMPLER_DEVICE_FILTER_FLAGS
};
const NvnTextureDeviceWords nvn_texture_device_words = {
    NVN_TEXTURE_DEVICE_PAGE_SIZE, NVN_TEXTURE_DEVICE_COLOR_COMPRESSION,
    NVN_TEXTURE_DEVICE_DEPTH_COMPRESSION, NVN_TEXTURE_DEVICE_BUFFER_KIND,
    NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE,
    NVN_TEXTURE_DEVICE_DIMENSION_LIMIT, NVN_TEXTURE_DEVICE_SECTOR_LAYOUT,
    NVN_TEXTURE_DEVICE_HEADER_ENABLE, NVN_TEXTURE_DEVICE_QUALITY,
    NVN_TEXTURE_DEVICE_QUALITY_OVERRIDE, NVN_TEXTURE_DEVICE_DESCRIPTOR_ROW
};

void nvn_sampler_defaults(NvnSampler* s) {
    uint64_t device = s->device;
    memset(s, 0, sizeof *s);
    s->device = device;
    s->min_filter = s->mag_filter = NVN_SAMPLER_FILTER_DEFAULT;
    for (unsigned i = 0; i < 3; i++) s->wrap[i] = NVN_SAMPLER_WRAP_DEFAULT;
    s->max_lod = NVN_SAMPLER_MAX_LOD_DEFAULT;
    s->compare = NVN_SAMPLER_COMPARE_DEFAULT;
    s->anisotropy = NVN_SAMPLER_ANISOTROPY_DEFAULT;
}

/* The library compares every word of the sampler and treats a NaN as unequal (fcmp). */
int nvn_sampler_equal(const NvnSampler* a, const NvnSampler* b) {
    if (a->device != b->device || a->min_filter != b->min_filter || a->mag_filter != b->mag_filter) return 0;
    for (unsigned i = 0; i < 3; i++)
        if (a->wrap[i] != b->wrap[i]) return 0;
    if (a->min_lod != b->min_lod || a->max_lod != b->max_lod || a->lod_bias != b->lod_bias) return 0;
    if (a->compare_enable != b->compare_enable || a->compare != b->compare) return 0;
    for (unsigned i = 0; i < 4; i++)
        if (a->border[i] != b->border[i]) return 0;
    if (a->anisotropy != b->anisotropy || a->reduction != b->reduction || a->lod_snap != b->lod_snap) return 0;
    return 1;
}

#define SAMPLER_SNAP_SEGMENT_SIZE 8u
#define SAMPLER_SNAP_LAST_INDEX 31u

/* T[i] = (8-d)/(2*d), d=(8+i%8)*2^(i/8). One division rounds the exact rational to f32.
   All 32 results match the module at de87870, including the positive zero at index 0. */
float nvn_sampler_snap_bias(uint32_t index) {
    uint32_t d = (SAMPLER_SNAP_SEGMENT_SIZE + (index & 7u)) << ((index >> 3) & 31u);
    return (float)(8.0 - (double)d) / (float)(2.0 * (double)d);
}

#define SAMPLER_ROUND_MAGIC 12582912.0f
#define SAMPLER_LOD_LIMIT 15.0f
#define SAMPLER_LOD_SCALE 256.0f
#define SAMPLER_DEVICE_BIAS_SCALE 0.0625f
#define SAMPLER_FIXED_WORD0 0x26000u
#define SAMPLER_FIXED_WORD1 0x200u
#define SAMPLER_SRGB_THRESHOLD 0x1.9a5c38p-9f
#define SAMPLER_SRGB_LINEAR 0x1.9d70a4p+3f
#define SAMPLER_SRGB_EXPONENT 0x1.aaa8ecp-2f
#define SAMPLER_SRGB_SCALE 0x1.0e147ap+0f
#define SAMPLER_SRGB_OFFSET (-0x1.c28f5cp-5f)

static float bits_float(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}
static uint32_t sampler_border_byte(uint32_t bits) {
    float x = bits_float(bits);
    if (x <= 0) return 0;
    if (x > 1) return 255;
    float y = x < SAMPLER_SRGB_THRESHOLD ? x * SAMPLER_SRGB_LINEAR :
              powf(x, SAMPLER_SRGB_EXPONENT) * SAMPLER_SRGB_SCALE + SAMPLER_SRGB_OFFSET;
    return float_bits(y * 255.0f + SAMPLER_ROUND_MAGIC) & 255u;
}
static uint32_t sampler_anisotropy(float value) {
    static const uint8_t codes[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7};
    if (!(value > 1)) return 0;
    if (value >= 16) return 7;
    uint32_t rounded = float_bits(value + SAMPLER_ROUND_MAGIC) - float_bits(SAMPLER_ROUND_MAGIC);
    return codes[rounded - 1];
}
int nvn_sampler_descriptor(const NvnSampler* s, const NvnSamplerDeviceWords* dev,
                           uint32_t out[NVN_SAMPLER_DESCRIPTOR_WORDS]) {
    static const uint8_t wraps[] = {4, 0, 7, 5, 6, 3, 1, 2};
    for (unsigned i = 0; i < 3; i++)
        if (s->wrap[i] >= sizeof wraps / sizeof wraps[0]) return 0;
    memset(out, 0, NVN_SAMPLER_DESCRIPTOR_BYTES);
    for (unsigned i = 0; i < 3; i++) out[0] |= (uint32_t)wraps[s->wrap[i]] << (3 * i);
    out[0] |= SAMPLER_FIXED_WORD0 | (sampler_anisotropy(s->anisotropy) << 20);
    if (s->compare_enable) out[0] |= 0x200u | (((s->compare + 7u) & 7u) << 10);
    uint32_t min = s->min_filter;
    out[1] = 0x11u + (s->mag_filter & 1u) + ((min & 1u) << 4) +
             (((((min + ((int32_t)min < 0)) << 5) & 0xc0u) + 0x40u) & 0xc0u);
    uint32_t snap = 0;
    if (s->lod_snap > 0)
        while (snap < SAMPLER_SNAP_LAST_INDEX && s->lod_snap > -nvn_sampler_snap_bias(snap)) snap++;
    float bias = s->lod_bias + (float)dev->w1a80 * SAMPLER_DEVICE_BIAS_SCALE;
    bias = fminf(SAMPLER_LOD_LIMIT, fmaxf(-SAMPLER_LOD_LIMIT, bias + nvn_sampler_snap_bias(snap)));
    out[1] |= (((uint32_t)(int32_t)(bias * SAMPLER_LOD_SCALE) & 0x1fffu) << 12) |
              (snap << 26) | (dev->w1a84 & 0x100u) | SAMPLER_FIXED_WORD1;
    if (s->reduction == 1) out[1] |= 0x400u;
    if (s->reduction == 2) out[1] |= 0x800u;
    float lo = fminf(SAMPLER_LOD_LIMIT, fmaxf(0, s->min_lod));
    float hi = fminf(SAMPLER_LOD_LIMIT, fmaxf(lo, s->max_lod));
    out[2] = ((uint32_t)(lo * SAMPLER_LOD_SCALE) & 0xfffu) |
             (((uint32_t)(hi * SAMPLER_LOD_SCALE) & 0xfffu) << 12) |
             (sampler_border_byte(s->border[0]) << 24);
    out[3] = (sampler_border_byte(s->border[1]) << 12) | (sampler_border_byte(s->border[2]) << 20);
    memcpy(out + 4, s->border, sizeof s->border);
    return 1;
}

void nvn_texture_builder_defaults(NvnTextureBuilder* b) {
    uint64_t device = b->device;
    memset(b, 0, sizeof *b);
    b->device = device;
    b->target = NVN_TEXTURE_TARGET_2D;
    b->levels = 1;
    for (unsigned i = 0; i < 4; i++) b->swizzle[i] = i + 2;
}

typedef struct { uint8_t bytes, width, height, depth_stencil; } TextureFormat;
static int texture_format(uint32_t f, TextureFormat* out) {
    if (!f || f > NVN_TEXTURE_FORMAT_MAX || (f >= 0x19 && f <= 0x21) || (f >= 0x51 && f <= 0x6c)) return 0;
    *out = (TextureFormat){4, 1, 1, 0};
    if (f <= 4 || f == 0x31) out->bytes = 1;
    if ((f >= 5 && f <= 9) || (f >= 0xd && f <= 0x10) || f == 0x32 ||
        (f >= 0x39 && f <= 0x3c) || (f >= 0x95 && f <= 0x98)) out->bytes = 2;
    if ((f >= 0x16 && f <= 0x18) || (f >= 0x29 && f <= 0x2d) || f == 0x36 ||
        (f >= 0x71 && f <= 0x75) || f == 0x41 || f == 0x42 || f == 0x45 || f == 0x46 ||
        f == 0x49 || f == 0x4a || f == 0x9d || f == 0x9f) out->bytes = 8;
    if (f >= 0x22 && f <= 0x24) out->bytes = 12;
    if ((f >= 0x2e && f <= 0x30) || (f >= 0x76 && f <= 0x94) || f == 0x43 || f == 0x44 ||
        f == 0x47 || f == 0x48 || (f >= 0x4b && f <= 0x50) || f == 0x9e || f == 0xa0) out->bytes = 16;
    if ((f >= 0x41 && f <= 0x50) || (f >= 0x79 && f <= 0x94) || f >= 0x9d)
        out->width = out->height = 4;
    if ((f >= 0x7a && f <= 0x86) || (f >= 0x88 && f <= 0x94)) {
        static const uint8_t widths[] = {5, 5, 6, 6, 8, 8, 8, 10, 10, 10, 10, 12, 12};
        static const uint8_t heights[] = {4, 5, 5, 6, 5, 6, 8, 5, 6, 8, 10, 10, 12};
        unsigned i = f - (f >= 0x88 ? 0x88 : 0x7a);
        out->width = widths[i];
        out->height = heights[i];
    }
    if (f >= 0x32 && f <= 0x36) out->depth_stencil |= 1;
    if (f == 0x31 || f == 0x35 || f == 0x36) out->depth_stencil |= 2;
    return 1;
}

static int texture_page_round(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, TextureFormat f) {
    return (b->format >= 0x31 && b->format <= 0x36) ||
           ((b->flags & NVN_TEXTURE_FLAG_COMPRESS) && dev->w1b1c && f.width == 1 && f.bytes >= 4 &&
            dev->w19f8 != NVN_TEXTURE_COMPRESSION_DISABLED);
}
static uint64_t texture_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, TextureFormat f) {
    if (b->target == NVN_TEXTURE_TARGET_BUFFER) {
        uint32_t kind = dev->w1b18 & 255u;
        return kind != 0 && kind != NVN_TEXTURE_KIND_PITCH ? dev->wac : f.bytes == 12 ? 4 : f.bytes;
    }
    return texture_page_round(b, dev, f) ? dev->wac : NVN_TEXTURE_GOB_BYTES;
}
uint64_t nvn_texture_storage_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev) {
    if (b->flags & NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET) return NVN_TEXTURE_LINEAR_RT_ALIGNMENT;
    if (b->flags & NVN_TEXTURE_FLAG_LINEAR) return NVN_TEXTURE_LINEAR_ALIGNMENT;
    TextureFormat f;
    if (!texture_format(b->format, &f)) return 0;
    return texture_alignment(b, dev, f);
}
static uint32_t ceil_u32(uint32_t n, uint32_t d) { return d ? (n + d - 1) / d : 0; }
static uint64_t align_u64(uint64_t n, uint64_t a) { return (n + a - 1) & -a; }
static uint32_t tile_shrink(uint32_t exponent, uint32_t base, uint32_t n) {
    while (exponent && (base << ((exponent - 1) & 31u)) >= n) exponent--;
    return exponent;
}
static uint32_t mip_dimension(uint32_t n, uint32_t i) {
    n >>= i & 31u;
    return n ? n : 1;
}
static int texture_is_array(uint32_t target) {
    return target == NVN_TEXTURE_TARGET_1D_ARRAY || target == NVN_TEXTURE_TARGET_2D_ARRAY ||
           target == NVN_TEXTURE_TARGET_CUBE || target == NVN_TEXTURE_TARGET_CUBE_ARRAY;
}
typedef struct { uint64_t size; uint32_t gobs_x, rows; uint8_t block_height_log2; } TextureLevelParts;
/* One level of a block linear texture with the tile exponents x, y and z: the GOBs across (rounded to the tile),
   the block rows, the trimmed block height exponent and the bytes of the level. */
static TextureLevelParts texture_level_parts(const NvnTextureLayout* l, uint32_t level,
                                             uint32_t x, uint32_t y, uint32_t z) {
    uint32_t p = 1u << (x & 31u);
    uint32_t w = l->width * l->sample_x, h = l->height * l->sample_y;
    uint32_t depth = l->dim == 3 ? l->depth : 1;
    uint32_t a = ceil_u32(mip_dimension(w, level), l->block_width);
    uint32_t rows = ceil_u32(mip_dimension(h, level), l->block_height);
    uint32_t d = l->dim == 3 ? mip_dimension(depth, level) : 1;
    uint32_t ey = tile_shrink(y, NVN_TEXTURE_GOB_HEIGHT, rows), ez = tile_shrink(z, 1, d);
    uint32_t q = rows >= (NVN_TEXTURE_GOB_HEIGHT << (y & 31u)) &&
                 a >= (NVN_TEXTURE_GOB_WIDTH / l->bytes_per_block) * p && (d >> (z & 31u)) ? p : 1;
    uint32_t nx = (ceil_u32(a * l->bytes_per_block, NVN_TEXTURE_GOB_WIDTH) + q - 1) & -q;
    TextureLevelParts parts = {
        .gobs_x = nx,
        .rows = rows,
        .block_height_log2 = (uint8_t)ey,
    };
    parts.size = (uint64_t)nx * ceil_u32(rows, NVN_TEXTURE_GOB_HEIGHT << (ey & 31u)) *
                 ceil_u32(d, 1u << (ez & 31u)) * ((uint64_t)NVN_TEXTURE_GOB_BYTES << ((ey + ez) & 63u));
    return parts;
}
static uint64_t texture_footprint(const NvnTextureLayout* l, uint32_t levels, int array,
                                 uint32_t x, uint32_t y, uint32_t z) {
    uint32_t p = 1u << (x & 31u);
    uint64_t sum = 0;
    uint32_t h = l->height * l->sample_y;
    uint32_t depth = l->dim == 3 ? l->depth : 1;
    for (uint32_t i = 0; i < levels; i++) sum += texture_level_parts(l, i, x, y, z).size;
    if (array) {
        uint32_t shift = p >= 2 ? y + z :
            tile_shrink(y, NVN_TEXTURE_GOB_HEIGHT, ceil_u32(h, l->block_height)) + tile_shrink(z, 1, depth);
        uint32_t alignment = (NVN_TEXTURE_GOB_BYTES * (p >= 2 ? p : 1)) << (shift & 31u);
        sum = align_u64(sum, alignment);
    }
    return sum;
}
static int texture_sparse_tile(const NvnTextureBuilder* b, NvnTextureLayout* l, uint32_t levels) {
    if ((b->flags & NVN_TEXTURE_SPARSE_TILE_MASK) != NVN_TEXTURE_FLAG_SPARSE || b->target == NVN_TEXTURE_TARGET_1D ||
        b->target == NVN_TEXTURE_TARGET_1D_ARRAY || l->bytes_per_block == 12 || !levels) return 0;
    uint32_t bytes = l->bytes_per_block, x, y, z;
    if (l->dim == 3) {
        x = bytes == 4 ? 1 : bytes == 8 || bytes == 16 ? 2 : 0;
        y = bytes == 8 || bytes == 16 ? 1 : 2;
        z = bytes == 4 || bytes == 8 || bytes == 16 ? 4 : 5;
    } else {
        x = bytes == 2 || bytes == 4 ? 3 : bytes == 8 || bytes == 16 ? 4 : 2;
        y = bytes == 2 || bytes == 4 ? 4 : bytes == 8 || bytes == 16 ? 3 : 5;
        z = 0;
    }
    uint32_t w = ((NVN_TEXTURE_GOB_WIDTH / bytes) << x) / l->sample_x * l->block_width;
    uint32_t h = (NVN_TEXTURE_GOB_HEIGHT << y) / l->sample_y * l->block_height;
    if ((int32_t)l->width < (int32_t)w || (int32_t)l->height < (int32_t)h ||
        (int32_t)l->depth < (int32_t)(1u << z)) return 0;
    l->tile_x = x; l->tile_y = y; l->tile_z = z;
    return 1;
}
static void texture_choose_tile(const NvnTextureBuilder* b, NvnTextureLayout* l, uint32_t levels) {
    if (b->tile & 15u) {
        l->tile_x = (b->tile >> 12) & 15u;
        l->tile_y = (b->tile >> 4) & 15u;
        l->tile_z = (b->tile >> 8) & 15u;
        return;
    }
    if (b->flags & NVN_TEXTURE_FLAG_MINIMAL_TILE) { l->tile_y = 1; return; }
    if (texture_sparse_tile(b, l, levels)) return;
    if (l->dim == 3) {
        uint32_t v = l->depth + l->depth / 2;
        l->tile_z = v > NVN_TEXTURE_AUTO_TILE_DEPTH_4 ? 4 : v >= NVN_TEXTURE_AUTO_TILE_DEPTH_3 ? 3 :
                    v > NVN_TEXTURE_AUTO_TILE_DEPTH_2 ? 2 : v > NVN_TEXTURE_AUTO_TILE_DEPTH_1 ? 1 : 0;
    } else {
        uint32_t hb = ceil_u32(l->height * l->sample_y, l->block_height), v = hb + hb / 2;
        l->tile_y = v >= NVN_TEXTURE_AUTO_TILE_ROWS_4 ? 4 : v >= NVN_TEXTURE_AUTO_TILE_ROWS_3 ? 3 :
                    v > NVN_TEXTURE_AUTO_TILE_ROWS_2 ? 2 : v > NVN_TEXTURE_AUTO_TILE_ROWS_1 ? 1 : 0;
    }
    if ((b->flags & NVN_TEXTURE_FLAG_ADAPTIVE_TILE) &&
        texture_footprint(l, levels, texture_is_array(b->target), 0, l->tile_y, l->tile_z) !=
        texture_footprint(l, levels, texture_is_array(b->target), 0, 0, 0))
        l->tile_y = l->tile_z = 0;
}
int nvn_texture_layout(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, NvnTextureLayout* l) {
    memset(l, 0, sizeof *l);
    l->alignment = nvn_texture_storage_alignment(b, dev);
    TextureFormat f;
    if (!texture_format(b->format, &f)) return 0;
    l->width = b->width; l->height = b->height; l->depth = b->depth;
    if (b->target == NVN_TEXTURE_TARGET_1D_ARRAY) { l->depth = l->height; l->height = 1; }
    if (b->target == NVN_TEXTURE_TARGET_CUBE) l->depth = NVN_TEXTURE_CUBE_FACES;
    l->bytes_per_block = f.bytes; l->block_width = f.width; l->block_height = f.height;
    l->depth_stencil = f.depth_stencil;
    for (uint32_t n = f.bytes; n > 1; n >>= 1) l->log2_bytes++;
    l->sample_x = b->samples == 2 || b->samples == 4 ? 2 : b->samples == 8 || b->samples == 16 ? 4 : 1;
    l->sample_y = b->samples == 4 || b->samples == 8 ? 2 : b->samples == 16 ? 4 : 1;
    l->samples = b->samples == 1 || b->samples == 2 ? 2 :
                 b->samples == 4 || b->samples == 8 || b->samples == 16 ? b->samples : 0;
    uint32_t levels = b->levels;
    if (b->target == NVN_TEXTURE_TARGET_BUFFER) {
        l->dim = l->levels = 1;
        l->layer_stride = l->size = (uint64_t)l->width * f.bytes;
    } else if (b->flags & NVN_TEXTURE_LINEAR_FLAGS) {
        l->dim = 2; l->levels = 1; l->linear_stride = b->stride;
        l->layer_stride = l->size = (uint64_t)l->height * b->stride;
    } else {
        if (b->target >= NVN_TEXTURE_TARGET_2D_MS && b->target <= NVN_TEXTURE_TARGET_RECTANGLE) levels = 1;
        l->levels = (uint8_t)levels;
        l->dim = b->target == NVN_TEXTURE_TARGET_3D ? 3 :
                 b->target == NVN_TEXTURE_TARGET_1D || b->target == NVN_TEXTURE_TARGET_1D_ARRAY ? 1 : 2;
        texture_choose_tile(b, l, levels);
        l->layer_stride = texture_footprint(l, levels, texture_is_array(b->target), l->tile_x, l->tile_y, l->tile_z);
        l->size = l->layer_stride * (b->target == NVN_TEXTURE_TARGET_3D ? 1 : l->depth);
        if (texture_page_round(b, dev, f)) l->size = align_u64(l->size, dev->wac);
    }
    if (b->gl_name) l->size = 0;
    return 1;
}

uint16_t nvn_texture_packaged_tile(uint32_t layout) { return (uint16_t)(((layout & 0xfffu) << 4) | 1u); }

/* The offset of byte (x, y) of a level, z = 0 (gpu.c's bl_offset with bd = 0). */
static uint64_t texture_level_offset(const NvnTextureLevel* lv, uint32_t x, uint32_t y) {
    uint64_t bh = lv->block_height_log2, gy = y >> GOB_SIZE_Y_SHIFT;
    return ((uint64_t)(x >> GOB_SIZE_X_SHIFT) << (GOB_SIZE_SHIFT + bh)) + gob_x_offset(x) +
           ((gy >> bh) * lv->gobs_x << (GOB_SIZE_SHIFT + bh)) +
           ((gy & ((1u << bh) - 1)) << GOB_SIZE_SHIFT) + gob_y_offset(y);
}
int nvn_texture_level(const NvnTexture* t, uint32_t level, NvnTextureLevel* out) {
    if (!t || !out) return 0;
    const NvnTextureLayout* l = &t->layout;
    if ((t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) || t->builder.target == NVN_TEXTURE_TARGET_BUFFER ||
        l->dim == 3 || l->sample_x != 1 || l->sample_y != 1 || level >= l->levels) return 0;
    uint64_t offset = 0;
    for (uint32_t i = 0; i < level; i++)
        offset += texture_level_parts(l, i, l->tile_x, l->tile_y, l->tile_z).size;
    TextureLevelParts parts = texture_level_parts(l, level, l->tile_x, l->tile_y, l->tile_z);
    *out = (NvnTextureLevel){.offset = offset, .size = parts.size, .gobs_x = parts.gobs_x,
                             .rows = parts.rows, .block_height_log2 = parts.block_height_log2};
    return 1;
}
int nvn_texture_write_rows(uint8_t* level_base, const NvnTextureLevel* lv, uint32_t x_start, uint32_t x_end,
                           uint32_t row_start, uint32_t row_end, const uint8_t* src, uint64_t src_stride) {
    if (!level_base || !lv || !src) return 0;
    /* The level holds whole blocks: a block is 2^bh GOBs, that is 8 << bh rows, high. */
    uint64_t block_rows = (uint64_t)GOB_SIZE_Y << lv->block_height_log2;
    uint64_t row_limit = (lv->rows + block_rows - 1) / block_rows * block_rows;
    if (x_end > (uint64_t)lv->gobs_x * GOB_SIZE_X || row_end > row_limit) return 0;
    if (x_start >= x_end || row_start >= row_end) return 1;
    /* Validate the last byte before the first write: the offsets grow with x and with y. */
    if (texture_level_offset(lv, x_end - 1, row_end - 1) >= lv->size) return 0;
    for (uint32_t r = row_start; r < row_end; r++) {
        const uint8_t* from = src + (uint64_t)(r - row_start) * src_stride;
        for (uint32_t x = x_start; x < x_end; x++)
            level_base[texture_level_offset(lv, x, r)] = from[x - x_start];
    }
    return 1;
}

/* Small encoder tables from de879e8, de87970 and the branches selected by de87948. */
static const uint8_t texture_target_codes[] = {0, 1, 2, 4, 5, 1, 5, 1, 3, 8};
static const uint8_t texture_device_rows[][5] = {
    {0, 2, 0, 0, 0}, {0, 2, 1, 1, 0}, {1, 2, 0, 0, 0},
    {1, 3, 0, 0, 0}, {3, 3, 0, 0, 9}, {3, 3, 0, 1, 31}
};
static const struct { uint32_t samples, code; } texture_sample_codes[] = {{2, 5}, {4, 2}, {8, 4}, {16, 6}};
#define TEXTURE_DEVICE_ROW_COUNT 6u
#define TEXTURE_QUALITY_LIMIT 19u
#define TEXTURE_MIP_LIMIT 16u
#define TEXTURE_QUALITY_SINGLE 0x914b83afu
#define TEXTURE_QUALITY_CLEAR_BIT 0xab0341cfu
#define TEXTURE_QUALITY_ALL_BITS 0x13478abeu
#define TEXTURE_QUALITY_ZERO 0x034e1413u
#define TEXTURE_IMAGE_CUBE_TARGET_CODE 5u
#define TEXTURE_DESCRIPTOR_IMAGE_BIT 0x01000000u

static void texture_bits(uint32_t* word, unsigned bit, unsigned count, uint32_t value) {
    uint32_t mask = (1u << count) - 1;
    *word = (*word & ~(mask << bit)) | ((value & mask) << bit);
}
static uint32_t texture_swizzle(uint32_t seed, const uint32_t sw[4], uint8_t one_code) {
    uint32_t codes[4], result = seed & 0x8007ffffu;
    for (unsigned i = 0; i < 4; i++) codes[i] = (seed >> (19 + 3 * i)) & 7u;
    for (unsigned i = 0; i < 4; i++) {
        uint32_t code = sw[i] == 0 ? 0 : sw[i] == 1 ? (one_code ? 6 : 7) :
                        sw[i] <= 5 ? codes[sw[i] - 2] : codes[0];
        result |= code << (19 + 3 * i);
    }
    return result;
}
static void texture_descriptor_storage(const NvnTexture* t, uint32_t target, NvnTextureDescriptorMode mode,
                                       const NvnTextureDeviceWords* dev, uint32_t q[8]) {
    int linear = (t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) != 0;
    if (target == NVN_TEXTURE_TARGET_BUFFER) {
        texture_bits(&q[4], 23, 4, 6);
    } else {
        texture_bits(&q[2], 21, 3, linear ? 2 : 3);
        texture_bits(&q[5], 31, 1, target != NVN_TEXTURE_TARGET_RECTANGLE);
        uint32_t code = linear ? 7 : texture_target_codes[target];
        if (!linear && target == NVN_TEXTURE_TARGET_RECTANGLE &&
            (t->layout.width > dev->w08 || t->layout.height > dev->w08)) code = 7;
        /* Image mode uses code 5 for both cube targets (d858e0c: (target|1)==9 and mode bit 0). */
        if (mode == NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE && !linear &&
            (target | 1u) == NVN_TEXTURE_TARGET_CUBE_ARRAY) code = TEXTURE_IMAGE_CUBE_TARGET_CODE;
        texture_bits(&q[4], 23, 4, code);
        q[4] |= 0xe0000000u;
    }
    if (linear) {
        texture_bits(&q[3], 0, 16, t->layout.linear_stride >> 5);
    } else if (target != NVN_TEXTURE_TARGET_BUFFER) {
        uint32_t sector = dev->w1a48 == 1 ? 2 : dev->w1a48 == 2 ? 1 : dev->w1a48 == 3 ? 3 : 0;
        texture_bits(&q[4], 27, 2, sector);
        if (dev->w1a4c) q[3] |= 0x10000u;
        texture_bits(&q[3], 3, 3, t->layout.tile_y);
        texture_bits(&q[3], 6, 3, t->layout.tile_z);
        texture_bits(&q[3], 17, 2, dev->w1a50 > TEXTURE_QUALITY_LIMIT ? 2 : 3);
        switch (dev->w1a54) {
        case TEXTURE_QUALITY_SINGLE: texture_bits(&q[3], 17, 2, 1); break;
        case TEXTURE_QUALITY_CLEAR_BIT: q[3] &= ~0x20000u; break;
        case TEXTURE_QUALITY_ALL_BITS: q[3] |= 0x60000u; break;
        case TEXTURE_QUALITY_ZERO: texture_bits(&q[3], 17, 2, 0); break;
        default: break;
        }
    }
    if (target == NVN_TEXTURE_TARGET_2D_MS || target == NVN_TEXTURE_TARGET_2D_MS_ARRAY)
        for (unsigned i = 0; i < sizeof texture_sample_codes / sizeof texture_sample_codes[0]; i++)
            if (t->layout.samples == texture_sample_codes[i].samples)
                texture_bits(&q[7], 8, 4, texture_sample_codes[i].code);
}
static uint64_t texture_descriptor_dimensions(const NvnTexture* t, const NvnTextureView* v,
    uint32_t target, NvnTextureDescriptorMode mode, const NvnTextureDeviceWords* dev, uint32_t q[8]) {
    uint64_t address = t->address;
    uint32_t depth = t->layout.depth;
    if (v && (v->flags & NVN_VIEW_LAYERS)) {
        address += t->layout.layer_stride * v->base_layer;
        if (target == NVN_TEXTURE_TARGET_1D_ARRAY || target == NVN_TEXTURE_TARGET_2D_ARRAY ||
            target == NVN_TEXTURE_TARGET_2D_MS_ARRAY || target == NVN_TEXTURE_TARGET_CUBE_ARRAY)
            depth = v->layer_count ? v->layer_count : depth - v->base_layer;
    }
    /* Image mode keeps the raw depth for both cube targets (d858fe8, d8590a8). */
    if (mode == NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE && target == NVN_TEXTURE_TARGET_CUBE_ARRAY)
        depth /= NVN_TEXTURE_CUBE_FACES;
    if (mode == NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE && target == NVN_TEXTURE_TARGET_CUBE) depth = 1;
    texture_bits(&q[4], 0, 16, t->layout.width - 1u);
    if (target == NVN_TEXTURE_TARGET_BUFFER) {
        texture_bits(&q[3], 0, 16, (t->layout.width - 1u) >> 16);
        return address;
    }
    texture_bits(&q[5], 0, 16, t->layout.height - 1u);
    texture_bits(&q[5], 16, 14, depth - 1u);
    /* The library clears word 3 bits 10..12 for linear textures too, so a pitch with bit 15 set loses it
       (the library function at d858c70 run on 1800 cases agrees). */
    q[3] &= 0xfc07e3ffu;
    q[6] &= 0x387fffffu;
    texture_bits(&q[3], 10, 3, t->layout.tile_x);
    uint32_t row = dev->w1a88 < TEXTURE_DEVICE_ROW_COUNT ? dev->w1a88 : TEXTURE_DEVICE_ROW_COUNT - 1;
    const uint8_t* r = texture_device_rows[row];
    texture_bits(&q[6], 25, 2, r[0]);
    texture_bits(&q[6], 23, 2, r[1]);
    texture_bits(&q[3], 19, 2, r[2]);
    texture_bits(&q[6], 30, 2, r[3]);
    q[3] |= (uint32_t)r[4] << 21;
    return address;
}
static uint32_t texture_descriptor_depth(uint32_t seed, uint32_t format, uint32_t mode,
                                         const NvnTextureFormatRecord* r, const uint32_t sw[4]) {
    uint32_t bits = 0x24900000u;
    if (r->depth_size) {
        int special = format == 0x33 || format == 0x35;
        bits = special ? (mode == 1 ? 0x24900000u : 0x76d80000u) :
                         (mode == 1 ? 0x36d80000u : 0x74900000u);
    }
    seed = (seed & 0x8007ffffu) | bits;
    if (mode == 1 && r->stencil_size == 8) {
        if (r->depth_size == 24) texture_bits(&seed, 0, 6, 14);
        if (r->depth_size == 32) texture_bits(&seed, 0, 6, 5);
    }
    return texture_swizzle(seed, sw, r->one_code);
}
static uint32_t texture_min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static void texture_descriptor_mips(const NvnTexture* t, const NvnTextureView* v, uint32_t q[8]) {
    uint32_t m = t->layout.levels < TEXTURE_MIP_LIMIT ? (uint32_t)t->layout.levels - 1u : TEXTURE_MIP_LIMIT - 1;
    uint32_t hi = texture_min(t->builder.levels - 1u, m), lo = 0;
    if (v && (v->flags & NVN_VIEW_LEVELS)) {
        lo = texture_min(v->base_level, m);
        if (v->level_count) hi = texture_min(hi, (uint32_t)v->base_level + v->level_count - 1u);
    }
    texture_bits(&q[7], 0, 4, lo);
    texture_bits(&q[7], 4, 4, hi);
    texture_bits(&q[3], 28, 4, m);
}
/* Registration adds raw layer bits even when the view's LAYERS flag is clear (d83d99c). */
static void texture_descriptor_registration(uint32_t flags, uint32_t layer, NvnTextureDescriptorMode mode,
    uint32_t q[8]) {
    uint32_t kind = (q[2] >> 21) & 7u;
    texture_bits(&q[4], 16, 3, layer);
    q[2] = (q[2] & 0x1fe0ffffu) | (((layer << 21) | (layer << 13)) & 0xe01f0000u);
    texture_bits(&q[5], 30, 1, flags >> 5);
    if (kind == 0) texture_bits(&q[5], 31, 1, flags >> 3);
    if (kind == 2 || kind == 3) texture_bits(&q[6], 0, 1, flags >> 3);
    /* The mode bit at word 2 bit 24 marks an image descriptor (d83dbc0). */
    if (mode == NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE) q[2] |= TEXTURE_DESCRIPTOR_IMAGE_BIT;
    else q[2] &= ~TEXTURE_DESCRIPTOR_IMAGE_BIT;
}
NvnTextureDescriptorResult nvn_texture_descriptor(const NvnTexture* t, const NvnTextureView* v,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* r, NvnTextureDescriptorMode mode,
    uint32_t out[NVN_TEXTURE_DESCRIPTOR_WORDS]) {
    uint32_t vf = v ? v->flags : 0;
    uint32_t format = (vf & NVN_VIEW_FORMAT) ? v->format : t->builder.format;
    uint32_t target = (vf & NVN_VIEW_TARGET) ? v->target : t->builder.target;
    if (format > NVN_TEXTURE_FORMAT_MAX) return NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT;
    if (target > NVN_TEXTURE_TARGET_BUFFER) return NVN_TEXTURE_DESCRIPTOR_BAD_TARGET;
    uint32_t q[NVN_TEXTURE_DESCRIPTOR_WORDS] = {0};
    const uint32_t* sw = (vf & NVN_VIEW_SWIZZLE) ? v->swizzle : t->builder.swizzle;
    for (unsigned i = 0; i < 4; i++)
        if (sw[i] > NVN_TEXTURE_SWIZZLE_MAX) return NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE;
    q[0] = texture_swizzle(r->seed, sw, r->one_code);
    texture_bits(&q[4], 22, 1, r->srgb != 0);
    texture_descriptor_storage(t, target, mode, dev, q);
    uint64_t address = texture_descriptor_dimensions(t, v, target, mode, dev, q);
    if (r->depth_size || r->stencil_size) {
        uint32_t ds_mode = (vf & NVN_VIEW_DEPTH_STENCIL_MODE) ? v->depth_stencil_mode : t->builder.depth_stencil_mode;
        q[0] = texture_descriptor_depth(q[0], format, ds_mode, r, sw);
    }
    if (!(t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) && target != NVN_TEXTURE_TARGET_BUFFER)
        texture_descriptor_mips(t, v, q);
    q[1] |= (uint32_t)address;
    q[2] |= (uint32_t)(address >> 32);
    texture_descriptor_registration(t->builder.flags, v ? v->base_layer : 0, mode, q);
    memcpy(out, q, sizeof q);
    return NVN_TEXTURE_DESCRIPTOR_OK;
}

/* Maxwell RT format words, row +0x1c of the library format table at 0xe029028 (sdk). Rows that are not listed hold 0. */
static const uint8_t rt_format_words[NVN_TEXTURE_FORMAT_MAX + 1] = {
    [0x01] = 0xf3, [0x02] = 0xf4, [0x03] = 0xf6, [0x04] = 0xf5, [0x05] = 0xf2, [0x06] = 0xee, [0x07] = 0xef,
    [0x08] = 0xf1, [0x09] = 0xf0, [0x0a] = 0xe5, [0x0b] = 0xe4, [0x0c] = 0xe3, [0x0d] = 0xea, [0x0e] = 0xeb,
    [0x0f] = 0xed, [0x10] = 0xec, [0x11] = 0xde, [0x12] = 0xda, [0x13] = 0xdb, [0x14] = 0xdd, [0x15] = 0xdc,
    [0x16] = 0xcb, [0x17] = 0xcd, [0x18] = 0xcc, [0x25] = 0xd5, [0x26] = 0xd7, [0x27] = 0xd9, [0x28] = 0xd8,
    [0x29] = 0xca, [0x2a] = 0xc6, [0x2b] = 0xc7, [0x2c] = 0xc9, [0x2d] = 0xc8, [0x2e] = 0xc0, [0x2f] = 0xc2,
    [0x30] = 0xc1, [0x31] = 0x17, [0x32] = 0x13, [0x33] = 0x14, [0x34] = 0x0a, [0x35] = 0x14, [0x36] = 0x19,
    [0x37] = 0xfa, [0x38] = 0xd6, [0x3d] = 0xd1, [0x3e] = 0xd2, [0x3f] = 0xe0, [0x6d] = 0xf9, [0x6e] = 0xd7,
    [0x6f] = 0xd9, [0x70] = 0xd8, [0x71] = 0xce, [0x72] = 0xc6, [0x73] = 0xc7, [0x74] = 0xc9, [0x75] = 0xc8,
    [0x76] = 0xc3, [0x77] = 0xc5, [0x78] = 0xc4, [0x95] = 0xe8, [0x96] = 0xf8, [0x97] = 0xe9, [0x99] = 0xe6,
    [0x9a] = 0xcf, [0x9b] = 0xe7, [0x9c] = 0xd0,
};
uint32_t nvn_rt_format_word(uint32_t format) {
    return format > NVN_TEXTURE_FORMAT_MAX ? 0 : rt_format_words[format];
}

#define NVN_PRESENT_RT_WORD 0xD5u /* RGBA8: the only format that present_buffer shows as RGBA8 */
#define NVN_PRESENT_BYTES_PER_PIXEL 4u

int nvn_window_present_args(const NvnTexture* t, uint64_t pool_storage, const NvnWindowCrop* crop, NvnPresentArgs* out) {
    if (!t || !crop || !out) return 0;
    if (nvn_rt_format_word(t->builder.format) != NVN_PRESENT_RT_WORD) return 0;
    if (t->builder.offset > UINT64_MAX - pool_storage) return 0;
    uint32_t w = t->builder.width, h = t->builder.height;
    if (w > UINT32_MAX / NVN_PRESENT_BYTES_PER_PIXEL) return 0;
    if (crop->x > INT32_MAX || crop->y > INT32_MAX || crop->width > INT32_MAX || crop->height > INT32_MAX) return 0;
    if (crop->x > INT32_MAX - crop->width || crop->y > INT32_MAX - crop->height) return 0;
    NvnPresentArgs args = {0};
    args.cpu_addr = pool_storage + t->builder.offset;
    args.w = w;
    args.h = h;
    if (t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        args.layout = NVN_PRESENT_LAYOUT_PITCH;
        args.pitch = t->layout.linear_stride;
        args.bhl = 0;
    } else {
        args.layout = NVN_PRESENT_LAYOUT_BLOCK;
        args.pitch = w * NVN_PRESENT_BYTES_PER_PIXEL;
        args.bhl = t->layout.tile_y;
    }
    args.fmt = NVN_PRESENT_FORMAT_RGBA8888;
    args.crop[0] = (int32_t)crop->x;
    args.crop[1] = (int32_t)crop->y;
    args.crop[2] = (int32_t)(crop->x + crop->width);
    args.crop[3] = (int32_t)(crop->y + crop->height);
    *out = args;
    return 1;
}

NvnSyncState nvn_sync_after_token(NvnSyncState state, uint32_t* fences) {
    if (!fences) return state;
    if (*fences > 0) (*fences)--;
    return *fences == 0 ? NVN_SYNC_SIGNALED : state;
}

int nvn_program_header_stage(uint32_t version, uint32_t layout, uint32_t size_word, uint32_t kind_word,
                             uint32_t stage_word, uint32_t* index_out) {
    if (!index_out || version != NVN_PROGRAM_HEADER_VERSION || layout < NVN_PROGRAM_LAYOUT_FIRST ||
        layout > NVN_PROGRAM_LAYOUT_LAST || size_word != NVN_PROGRAM_HEADER_SIZE ||
        kind_word != NVN_PROGRAM_HEADER_KIND || stage_word >= NVN_PROGRAM_STAGES)
        return 0;
    *index_out = stage_word;
    return 1;
}

/* The Maxwell header word (analyst-prog-builder-1.md, section 1): mode in bits 29..31, count or value in bits 16..28,
   method in bits 0..11. */
#define NVN_PROGRAM_MODE_INCREASING 0x20000000u    /* mode 1: count data words follow */
#define NVN_PROGRAM_MODE_INLINE 0x80000000u        /* mode 4: the value is in the count field, no data word follows */
#define NVN_PROGRAM_MODE_INCREASE_ONCE 0xA0000000u /* mode 5: a fixed macro of count data words follows */
#define NVN_PROGRAM_IMMEDIATE_VALUE_MAX 0x1FFFu    /* the 13 bits of an inline value */
#define NVN_PROGRAM_COUNT_SHIFT 16u

#define NVN_PROGRAM_METHOD_PROGRAM 0xE18u
#define NVN_PROGRAM_METHOD_VERTEX 0x800u
#define NVN_PROGRAM_METHOD_VERTEX_HEADER 0x813u
#define NVN_PROGRAM_METHOD_FRAGMENT 0x850u
#define NVN_PROGRAM_METHOD_FRAGMENT_HEADER 0x853u
#define NVN_PROGRAM_METHOD_FRAGMENT_3D0 0x3D0u
#define NVN_PROGRAM_METHOD_FRAGMENT_3D1 0x3D1u
#define NVN_PROGRAM_METHOD_FRAGMENT_084 0x084u
#define NVN_PROGRAM_METHOD_FRAGMENT_3C7 0x3C7u
#define NVN_PROGRAM_METHOD_FRAGMENT_D8 0x0D8u
#define NVN_PROGRAM_METHOD_FRAGMENT_489 0x489u
#define NVN_PROGRAM_METHOD_FRAGMENT_65B 0x65Bu
#define NVN_PROGRAM_METHOD_FRAGMENT_1D5 0x1D5u

#define NVN_PROGRAM_MACRO_COUNT 5u
#define NVN_PROGRAM_D8_COUNT 2u
#define NVN_PROGRAM_FRAGMENT_3D1_COUNT 4u
#define NVN_PROGRAM_CB_ADDRESS_SHIFT 8u

#define NVN_PROGRAM_SLOT_VERTEX 1u
#define NVN_PROGRAM_SLOT_FRAGMENT 5u
#define NVN_PROGRAM_PROGRAM_ID 0u
#define NVN_PROGRAM_GROUP_VERTEX 0u
#define NVN_PROGRAM_GROUP_FRAGMENT 4u

#define NVN_PROGRAM_FRAGMENT_CONTROL 0x51u
#define NVN_PROGRAM_FRAGMENT_1D5_OPAQUE 1u
#define NVN_PROGRAM_FRAGMENT_1D5_BLEND 0x30u
#define NVN_PROGRAM_FRAGMENT_65B_SHIFT 4u
#define NVN_PROGRAM_FRAGMENT_489_KEYS 4u

#define NVN_PROGRAM_D8_TOP_SHIFT 24u
#define NVN_PROGRAM_D8_LOW_MASK 0x00FFFFFFu
#define NVN_PROGRAM_D8_TOP_18 0x18u
#define NVN_PROGRAM_D8_TOP_20 0x20u
#define NVN_PROGRAM_D8_WORD_MEDIUM 0x06164010u
#define NVN_PROGRAM_D8_DATA1_MEDIUM 0x20u
#define NVN_PROGRAM_D8_SAMPLES 3u

static uint32_t program_cb_address(uint64_t region_base, uint32_t cb_offset) {
    return (uint32_t)((region_base + cb_offset) >> NVN_PROGRAM_CB_ADDRESS_SHIFT);
}

static void program_push_word(uint32_t* words, uint32_t* at, uint32_t word) { words[(*at)++] = word; }

static int program_push_immediate(uint32_t* words, uint32_t* at, uint32_t method, uint32_t value) {
    if (value > NVN_PROGRAM_IMMEDIATE_VALUE_MAX) return 0;
    words[(*at)++] = NVN_PROGRAM_MODE_INLINE | (value << NVN_PROGRAM_COUNT_SHIFT) | method;
    return 1;
}

/* The 0xE18 macro with its five data words: pipeline slot, program id, code offset, bind group, cb address. */
static void program_push_macro(uint32_t* words, uint32_t* at, uint32_t slot, uint32_t code_offset, uint32_t group,
                               uint32_t cb_address) {
    program_push_word(words, at, NVN_PROGRAM_MODE_INCREASE_ONCE | (NVN_PROGRAM_MACRO_COUNT << NVN_PROGRAM_COUNT_SHIFT) |
                                   NVN_PROGRAM_METHOD_PROGRAM);
    program_push_word(words, at, slot);
    program_push_word(words, at, NVN_PROGRAM_PROGRAM_ID);
    program_push_word(words, at, code_offset);
    program_push_word(words, at, group);
    program_push_word(words, at, cb_address);
}

static int program_build_vertex(const NvnProgramStageIn* in, uint64_t region_base, uint32_t* words, uint32_t* at) {
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_VERTEX, 0)) return 0;
    program_push_macro(words, at, NVN_PROGRAM_SLOT_VERTEX, in->code_offset, NVN_PROGRAM_GROUP_VERTEX,
                       program_cb_address(region_base, in->cb_offset));
    return program_push_immediate(words, at, NVN_PROGRAM_METHOD_VERTEX_HEADER, in->header_value);
}

static uint32_t program_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

static uint32_t program_d8_data0(uint32_t word, uint32_t top) {
    return (word & NVN_PROGRAM_D8_LOW_MASK) | (top << NVN_PROGRAM_D8_TOP_SHIFT);
}

/* The two data words of the 0xD8 method. */
static void program_build_d8(const NvnProgramFragmentIn* frag, const NvnProgramDeviceIn* dev, uint32_t out[2]) {
    uint32_t top = dev->d1a38 >> NVN_PROGRAM_D8_TOP_SHIFT;
    if (frag->g168 != 0) {
        out[0] = program_d8_data0(dev->d1a38, program_min(top, NVN_PROGRAM_D8_TOP_18));
        out[1] = dev->d1a68;
    } else if (frag->g170 != 0) {
        out[0] = NVN_PROGRAM_D8_WORD_MEDIUM;
        out[1] = NVN_PROGRAM_D8_DATA1_MEDIUM;
    } else {
        if (frag->g198 >= NVN_PROGRAM_D8_SAMPLES && dev->d94 > dev->da4 && dev->d1a70 != 0)
            top = program_min(top, dev->d1a74 >> NVN_PROGRAM_D8_TOP_SHIFT);
        out[0] = program_d8_data0(dev->d1a38, program_min(top, NVN_PROGRAM_D8_TOP_20));
        out[1] = dev->d1a68;
    }
}

static uint32_t program_489_value(const NvnProgramFragmentIn* frag, const NvnProgramDeviceIn* dev) {
    static const uint8_t opaque[NVN_PROGRAM_FRAGMENT_489_KEYS] = {2, 10, 1, 9};
    static const uint8_t blended[NVN_PROGRAM_FRAGMENT_489_KEYS] = {1, 9, 1, 9};
    if (dev->d1a40 != 0) return dev->d1a40;
    uint32_t key = frag->g190 - 1u;
    if (key >= NVN_PROGRAM_FRAGMENT_489_KEYS) return 0;
    return frag->g188 == 0 ? opaque[key] : blended[key];
}

static int program_build_fragment(const NvnProgramStageIn* in, const NvnProgramFragmentIn* frag,
                                  const NvnProgramDeviceIn* dev, uint64_t region_base, uint32_t* words,
                                  uint32_t* at) {
    uint32_t d8[2];
    program_build_d8(frag, dev, d8);
    uint32_t value_3d0 = frag->g158 == 0 ? 0u : (frag->g160 == 0 ? 1u : 0u);
    int branch_2004 = frag->g158 != 0 && frag->g160 == 0;
    uint32_t value_084 = (dev->d1a3c != 0 || frag->g148 != 0) ? 1u : 0u;
    uint32_t value_65b = (uint32_t)frag->g178 | ((uint32_t)frag->g180 << NVN_PROGRAM_FRAGMENT_65B_SHIFT);
    uint32_t value_1d5 =
        frag->g188 == 0 ? NVN_PROGRAM_FRAGMENT_1D5_OPAQUE : NVN_PROGRAM_FRAGMENT_1D5_BLEND;
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT, NVN_PROGRAM_FRAGMENT_CONTROL)) return 0;
    program_push_macro(words, at, NVN_PROGRAM_SLOT_FRAGMENT, in->code_offset, NVN_PROGRAM_GROUP_FRAGMENT,
                       program_cb_address(region_base, in->cb_offset));
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_HEADER, in->header_value)) return 0;
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_3D0, value_3d0)) return 0;
    if (branch_2004) {
        program_push_word(words, at, NVN_PROGRAM_MODE_INCREASING | (NVN_PROGRAM_FRAGMENT_3D1_COUNT << NVN_PROGRAM_COUNT_SHIFT) |
                                       NVN_PROGRAM_METHOD_FRAGMENT_3D1);
        for (uint32_t i = 0; i < NVN_PROGRAM_FRAGMENT_3D1_COUNT; i++) program_push_word(words, at, frag->g1a0[i]);
    }
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_084, value_084)) return 0;
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_3C7, frag->g150)) return 0;
    program_push_word(words, at, NVN_PROGRAM_MODE_INCREASING | (NVN_PROGRAM_D8_COUNT << NVN_PROGRAM_COUNT_SHIFT) |
                                   NVN_PROGRAM_METHOD_FRAGMENT_D8);
    program_push_word(words, at, d8[0]);
    program_push_word(words, at, d8[1]);
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_489, program_489_value(frag, dev))) return 0;
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_65B, value_65b)) return 0;
    return program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT_1D5, value_1d5);
}

/* The stage 5 block: 18 plain data words, no method header. The library leaves word 17 as stack content, so the host
   writes 0. */
static void program_build_compute(const NvnProgramComputeIn* in, uint32_t* words, uint32_t* at) {
    program_push_word(words, at, in->code_offset);
    program_push_word(words, at, in->cb_offset);
    program_push_word(words, at, in->h6fc);
    program_push_word(words, at, in->h710);
    program_push_word(words, at, in->h70c);
    program_push_word(words, at, in->h718);
    program_push_word(words, at, in->h71c);
    program_push_word(words, at, in->h720);
    program_push_word(words, at, in->h724);
    program_push_word(words, at, in->h728);
    program_push_word(words, at, in->h72c);
    program_push_word(words, at, in->h730);
    program_push_word(words, at, in->h734);
    program_push_word(words, at, in->h6f8);
    program_push_word(words, at, in->h750_low);
    program_push_word(words, at, in->h750_high);
    program_push_word(words, at, in->flags);
    program_push_word(words, at, 0u);
}

int nvn_program_build_words(const NvnProgramComputeIn* compute, const NvnProgramStageIn* vertex,
                            const NvnProgramStageIn* fragment, const NvnProgramFragmentIn* frag,
                            const NvnProgramDeviceIn* dev, uint64_t region_base, uint32_t* words, uint32_t* count) {
    if (!words || !count) return 0;
    if (compute && (vertex || fragment)) return 0;
    if (fragment && (!frag || !dev)) return 0;
    uint32_t at = 0;
    if (compute) program_build_compute(compute, words, &at);
    if (vertex && !program_build_vertex(vertex, region_base, words, &at)) return 0;
    if (fragment && !program_build_fragment(fragment, frag, dev, region_base, words, &at)) return 0;
    *count = at;
    return 1;
}

int nvn_program_compute_from_words(const uint32_t* words, uint32_t count, NvnProgramComputeIn* out) {
    if (!words || !out || count != NVN_PROGRAM_COMPUTE_WORDS) return 0;
    out->code_offset = words[0];
    out->cb_offset = words[1];
    out->h6fc = words[2];
    out->h710 = words[3];
    out->h70c = words[4];
    out->h718 = words[5];
    out->h71c = words[6];
    out->h720 = words[7];
    out->h724 = words[8];
    out->h728 = words[9];
    out->h72c = words[10];
    out->h730 = words[11];
    out->h734 = words[12];
    out->h6f8 = words[13];
    out->h750_low = words[14];
    out->h750_high = words[15];
    out->flags = words[16];
    return 1;
}

#define NVN_WORD_MODE_SHIFT 29u
#define NVN_WORD_MODE_INCREASING 1u
#define NVN_WORD_MODE_INLINE 4u
#define NVN_WORD_MODE_NON_INCREASING 3u
#define NVN_WORD_MODE_INCREASE_ONCE 5u
#define NVN_WORD_COUNT_MASK 0x1FFFu
#define NVN_WORD_METHOD_MASK 0xFFFu

int nvn_word_stream_next(NvnWordStream* s, uint32_t word, uint32_t* method, uint32_t* value, int* last) {
    if (!s || !method || !value || !last) return -1;
    if (s->remaining) {
        s->remaining--;
        *method = s->method;
        *value = word;
        *last = s->remaining == 0;
        if (s->mode == NVN_WORD_MODE_INCREASING) {
            s->method++;
        } else if (s->mode == NVN_WORD_MODE_INCREASE_ONCE) { /* the first word, then the next method for the rest */
            s->method++;
            s->mode = NVN_WORD_MODE_NON_INCREASING;
        }
        return 1;
    }
    uint32_t mode = word >> NVN_WORD_MODE_SHIFT;
    uint32_t count = (word >> NVN_PROGRAM_COUNT_SHIFT) & NVN_WORD_COUNT_MASK;
    uint32_t header_method = word & NVN_WORD_METHOD_MASK;
    if (mode == NVN_WORD_MODE_INLINE) {
        *method = header_method;
        *value = count;
        *last = 1;
        return 1;
    }
    if (mode != NVN_WORD_MODE_INCREASING && mode != NVN_WORD_MODE_INCREASE_ONCE) return -1;
    if (!count) return -1;
    s->mode = mode;
    s->method = header_method;
    s->remaining = count;
    return 0;
}

int nvn_word_stream_complete(const NvnWordStream* s) { return s && s->remaining == 0; }

/* The pushbuffer command header (pushbuffer.h): type in bits 31:29, count in 28:16, subchannel in 15:13, method in
   12:0. Types 1, 3, 4 and 5 carry method words; type 0 is a no-op. */
#define NVN_PUSHBUF_TYPE_SHIFT 29u
#define NVN_PUSHBUF_TYPE_NOOP 0u
#define NVN_PUSHBUF_TYPE_INCREASING 1u
#define NVN_PUSHBUF_TYPE_NON_INCREASING 3u
#define NVN_PUSHBUF_TYPE_IMMEDIATE 4u
#define NVN_PUSHBUF_TYPE_INCREASE_ONCE 5u
#define NVN_PUSHBUF_COUNT_SHIFT 16u
#define NVN_PUSHBUF_COUNT_MASK 0x1FFFu
#define NVN_PUSHBUF_SUBCHANNEL_SHIFT 13u
#define NVN_PUSHBUF_SUBCHANNEL_MASK 7u
#define NVN_PUSHBUF_SUBCHANNEL_3D 0u
#define NVN_PUSHBUF_METHOD_MASK 0xFFFu

/* Checks the stream and, when emit is set, calls the callback for each method word. The checks come first, so a
   refused stream changes no state. */
static int pushbuf_pass(const uint32_t* words, uint32_t count, NvnPushbufMethod callback, void* ctx, int emit,
                        uint32_t* error_index) {
    uint32_t i = 0;
    while (i < count) {
        uint32_t header = words[i];
        uint32_t type = header >> NVN_PUSHBUF_TYPE_SHIFT;
        uint32_t n = (header >> NVN_PUSHBUF_COUNT_SHIFT) & NVN_PUSHBUF_COUNT_MASK;
        uint32_t sub = (header >> NVN_PUSHBUF_SUBCHANNEL_SHIFT) & NVN_PUSHBUF_SUBCHANNEL_MASK;
        uint32_t method = header & NVN_PUSHBUF_METHOD_MASK;
        if (type == NVN_PUSHBUF_TYPE_NOOP) {
            i++;
            continue;
        }
        if (sub != NVN_PUSHBUF_SUBCHANNEL_3D ||
            (type != NVN_PUSHBUF_TYPE_INCREASING && type != NVN_PUSHBUF_TYPE_NON_INCREASING &&
             type != NVN_PUSHBUF_TYPE_IMMEDIATE && type != NVN_PUSHBUF_TYPE_INCREASE_ONCE)) {
            if (error_index) *error_index = i;
            return -1;
        }
        if (type == NVN_PUSHBUF_TYPE_IMMEDIATE) { /* the count field is the value (IMMD) */
            if (emit) callback(ctx, method, n);
            i++;
            continue;
        }
        if (n > count - i - 1u) { /* the data words run past the end */
            if (error_index) *error_index = i;
            return -1;
        }
        if (emit) {
            for (uint32_t k = 0; k < n; k++) {
                uint32_t m = method;
                if (type == NVN_PUSHBUF_TYPE_INCREASING)
                    m = (method + k) & NVN_PUSHBUF_METHOD_MASK;
                else if (type == NVN_PUSHBUF_TYPE_INCREASE_ONCE && k)
                    m = (method + 1u) & NVN_PUSHBUF_METHOD_MASK;
                callback(ctx, m, words[i + 1u + k]);
            }
        }
        i += 1u + n;
    }
    return 0;
}

int nvn_pushbuf_decode(const uint32_t* words, uint32_t count, NvnPushbufMethod callback, void* ctx,
                       uint32_t* error_index) {
    if (error_index) *error_index = 0;
    if (!callback || (!words && count)) return -1;
    uint32_t bad = 0;
    if (pushbuf_pass(words, count, NULL, NULL, 0, &bad) != 0) {
        if (error_index) *error_index = bad;
        return -1;
    }
    return pushbuf_pass(words, count, callback, ctx, 1, error_index);
}

#define NVN_PROGRAM_DISABLE_BIT_FRAGMENT 1u
#define NVN_PROGRAM_DISABLE_BIT_GEOMETRY 2u
#define NVN_PROGRAM_DISABLE_BIT_TESS_CONTROL 3u
#define NVN_PROGRAM_DISABLE_BIT_TESS_EVALUATION 4u

#define NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL 0x820u
#define NVN_PROGRAM_DISABLE_VALUE_TESS_CONTROL 0x20u
#define NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL_OFFSET 0x821u
#define NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL_OFF 0xD1Eu
#define NVN_PROGRAM_DISABLE_METHOD_TESS_EVALUATION 0x830u
#define NVN_PROGRAM_DISABLE_VALUE_TESS_EVALUATION 0x30u
#define NVN_PROGRAM_DISABLE_METHOD_GEOMETRY 0x840u
#define NVN_PROGRAM_DISABLE_VALUE_GEOMETRY 0x40u
#define NVN_PROGRAM_DISABLE_METHOD_GEOMETRY_OFF 0x47Cu
#define NVN_PROGRAM_DISABLE_METHOD_FRAGMENT 0x850u
#define NVN_PROGRAM_DISABLE_VALUE_FRAGMENT 0x50u

static const NvnProgramMethod disable_tess_control[] = {
    {NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL, NVN_PROGRAM_DISABLE_VALUE_TESS_CONTROL},
    {NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL_OFFSET, NVN_PROGRAM_DISABLE_TCS_OFFSET},
    {NVN_PROGRAM_DISABLE_METHOD_TESS_CONTROL_OFF, 0u},
};
static const NvnProgramMethod disable_tess_evaluation[] = {
    {NVN_PROGRAM_DISABLE_METHOD_TESS_EVALUATION, NVN_PROGRAM_DISABLE_VALUE_TESS_EVALUATION},
};
static const NvnProgramMethod disable_geometry[] = {
    {NVN_PROGRAM_DISABLE_METHOD_GEOMETRY, NVN_PROGRAM_DISABLE_VALUE_GEOMETRY},
    {NVN_PROGRAM_DISABLE_METHOD_GEOMETRY_OFF, 0u},
};
static const NvnProgramMethod disable_fragment[] = {
    {NVN_PROGRAM_DISABLE_METHOD_FRAGMENT, NVN_PROGRAM_DISABLE_VALUE_FRAGMENT},
};

/* The library writes the stages in this order: 3, 4, 2, 1. */
static const struct {
    uint32_t bit, count;
    const NvnProgramMethod* pair;
} disable_table[] = {
    {NVN_PROGRAM_DISABLE_BIT_TESS_CONTROL, 3u, disable_tess_control},
    {NVN_PROGRAM_DISABLE_BIT_TESS_EVALUATION, 1u, disable_tess_evaluation},
    {NVN_PROGRAM_DISABLE_BIT_GEOMETRY, 2u, disable_geometry},
    {NVN_PROGRAM_DISABLE_BIT_FRAGMENT, 1u, disable_fragment},
};

uint32_t nvn_program_disable_methods(uint32_t requested_mask, uint32_t program_mask,
                                     NvnProgramMethod out[NVN_PROGRAM_DISABLE_MAX]) {
    if (!out) return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < sizeof disable_table / sizeof disable_table[0]; i++) {
        uint32_t bit = disable_table[i].bit;
        if (!(requested_mask & (1u << bit)) || (program_mask & (1u << bit))) continue;
        for (uint32_t j = 0; j < disable_table[i].count; j++) out[n++] = disable_table[i].pair[j];
    }
    return n;
}

/* The command headers of the bind packets (analyst-s2-A-1.md): a mode 1 run of count words, a mode 4 inline value, or
   a mode 5 macro of count words. The existing program builders define these bits, and the stream decoder names them. */
typedef struct {
    uint32_t* out;
    uint32_t at;
} NvnBindWriter;

static void bind_word(NvnBindWriter* w, uint32_t word) { w->out[w->at++] = word; }

static void bind_run(NvnBindWriter* w, uint32_t method, uint32_t count) {
    bind_word(w, NVN_PROGRAM_MODE_INCREASING | (count << NVN_PROGRAM_COUNT_SHIFT) | method);
}

static void bind_value(NvnBindWriter* w, uint32_t method, uint32_t value) {
    bind_word(w, NVN_PROGRAM_MODE_INLINE | ((value & NVN_PROGRAM_IMMEDIATE_VALUE_MAX) << NVN_PROGRAM_COUNT_SHIFT) |
                     method);
}

static void bind_macro(NvnBindWriter* w, uint32_t method, uint32_t count) {
    bind_word(w, NVN_PROGRAM_MODE_INCREASE_ONCE | (count << NVN_PROGRAM_COUNT_SHIFT) | method);
}

#define NVN_BIND_COLOR_LOGIC_SHIFT 16u
#define NVN_BIND_COLOR_LOGIC_MASK 0xFFu
#define NVN_BIND_COLOR_ALPHA_SHIFT 24u
#define NVN_BIND_COLOR_ALPHA_MASK 0xFu
#define NVN_BIND_COLOR_LOGIC_OP 3u
#define NVN_BIND_COLOR_ALPHA_8 8u
#define NVN_BIND_COLOR_LOGIC_BASE 0x1500u
#define NVN_BIND_COLOR_METHOD_LOGIC 0x671u
#define NVN_BIND_COLOR_METHOD_LOGIC_OP 0xE10u
#define NVN_BIND_COLOR_METHOD_ALPHA_TEST 0x4BBu
#define NVN_BIND_COLOR_METHOD_ALPHA_REF 0x4C5u

/* The logic op block, then the alpha test block. */
uint32_t nvn_bind_color_words(uint32_t w0, uint32_t* out) {
    if (!out) return 0;
    uint32_t logic = (w0 >> NVN_BIND_COLOR_LOGIC_SHIFT) & NVN_BIND_COLOR_LOGIC_MASK;
    uint32_t alpha = (w0 >> NVN_BIND_COLOR_ALPHA_SHIFT) & NVN_BIND_COLOR_ALPHA_MASK;
    NvnBindWriter w = {out, 0};
    if (logic == NVN_BIND_COLOR_LOGIC_OP) {
        bind_run(&w, NVN_BIND_COLOR_METHOD_LOGIC_OP, 1);
        bind_word(&w, w0);
    } else {
        bind_run(&w, NVN_BIND_COLOR_METHOD_LOGIC, 2);
        bind_word(&w, 1);
        bind_word(&w, logic | NVN_BIND_COLOR_LOGIC_BASE);
    }
    if (alpha == NVN_BIND_COLOR_ALPHA_8) {
        bind_value(&w, NVN_BIND_COLOR_METHOD_ALPHA_TEST, 0);
    } else {
        bind_value(&w, NVN_BIND_COLOR_METHOD_ALPHA_TEST, 1);
        bind_value(&w, NVN_BIND_COLOR_METHOD_ALPHA_REF, alpha);
    }
    return w.at;
}

#define NVN_BIND_CHANNEL_MASK_TARGETS 8u
#define NVN_BIND_CHANNEL_MASK_COUNT 4u
#define NVN_BIND_CHANNEL_MASK_METHOD 0xE12u

/* One packed word per pair of targets: channel t of a target becomes bit 4t of its packed mask. */
uint32_t nvn_bind_channel_mask_words(uint32_t w0, uint32_t* out) {
    if (!out) return 0;
    uint32_t mask[NVN_BIND_CHANNEL_MASK_TARGETS];
    for (uint32_t t = 0; t < NVN_BIND_CHANNEL_MASK_TARGETS; t++) {
        uint32_t n = (w0 >> (4u * t)) & 0xFu;
        mask[t] = (n & 1u) | (((n >> 1) & 1u) << 4) | (((n >> 2) & 1u) << 8) | (((n >> 3) & 1u) << 12);
    }
    NvnBindWriter w = {out, 0};
    bind_macro(&w, NVN_BIND_CHANNEL_MASK_METHOD, NVN_BIND_CHANNEL_MASK_COUNT);
    for (uint32_t i = 0; i < NVN_BIND_CHANNEL_MASK_COUNT; i++)
        bind_word(&w, mask[2u * i] | (mask[2u * i + 1u] << 16));
    return w.at;
}

#define NVN_BIND_DEPTH_TEST_BIT 0x1u
#define NVN_BIND_DEPTH_WRITE_SHIFT 1u
#define NVN_BIND_STENCIL_TEST_BIT 0x4u
#define NVN_BIND_DEPTH_FUNC_SHIFT 4u
#define NVN_BIND_DEPTH_FUNC_MASK 0xFu
#define NVN_BIND_DEPTH_METHOD_WRITE 0x4BAu
#define NVN_BIND_DEPTH_METHOD_TEST 0x4B3u
#define NVN_BIND_DEPTH_METHOD_FUNC 0x4C3u
#define NVN_BIND_DEPTH_METHOD_STENCIL 0xE1Au
#define NVN_BIND_DEPTH_METHOD_STENCIL_OFF 0x4E0u

/* The depth test block, then the stencil block when bit 2 is set. */
uint32_t nvn_bind_depth_stencil_words(uint32_t w0, uint32_t w1, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    if (w0 & NVN_BIND_DEPTH_TEST_BIT) {
        bind_value(&w, NVN_BIND_DEPTH_METHOD_WRITE, (w0 >> NVN_BIND_DEPTH_WRITE_SHIFT) & 1u);
        bind_value(&w, NVN_BIND_DEPTH_METHOD_TEST, 1);
        bind_value(&w, NVN_BIND_DEPTH_METHOD_FUNC, (w0 >> NVN_BIND_DEPTH_FUNC_SHIFT) & NVN_BIND_DEPTH_FUNC_MASK);
    } else {
        bind_value(&w, NVN_BIND_DEPTH_METHOD_TEST, 0);
    }
    if (w0 & NVN_BIND_STENCIL_TEST_BIT) {
        bind_run(&w, NVN_BIND_DEPTH_METHOD_STENCIL, 1);
        bind_word(&w, w1);
    } else {
        bind_value(&w, NVN_BIND_DEPTH_METHOD_STENCIL_OFF, 0);
    }
    return w.at;
}

#define NVN_BIND_POLYGON_FRONT_SHIFT 2u
#define NVN_BIND_POLYGON_MODE_SHIFT 3u
#define NVN_BIND_POLYGON_MODE_MASK 3u
#define NVN_BIND_POLYGON_MODE_REFUSED 3u
#define NVN_BIND_POLYGON_CULL_MASK 3u
#define NVN_BIND_POLYGON_FRONT_CW 0x900u
#define NVN_BIND_POLYGON_OFFSET_SHIFT 15u
#define NVN_BIND_POLYGON_METHOD_FRONT 0x647u
#define NVN_BIND_POLYGON_METHOD_CULL 0x646u
#define NVN_BIND_POLYGON_METHOD_CULL_FACE 0x648u
#define NVN_BIND_POLYGON_METHOD_MODE 0x36Bu
#define NVN_BIND_POLYGON_METHOD_MODE_ALIAS 0x36Cu
#define NVN_BIND_POLYGON_METHOD_OFFSET 0x370u

static const uint32_t bind_polygon_cull[4] = {0x405u, 0x404u, 0x405u, 0x408u};
static const uint32_t bind_polygon_mode[3] = {0x1B00u, 0x1B01u, 0x1B02u};

/* The front face, cull, polygon mode and polygon offset words. Polygon mode 3 is a host refusal. */
uint32_t nvn_bind_polygon_words(uint32_t w0, uint32_t* out) {
    if (!out) return 0;
    uint32_t pm = (w0 >> NVN_BIND_POLYGON_MODE_SHIFT) & NVN_BIND_POLYGON_MODE_MASK;
    if (pm == NVN_BIND_POLYGON_MODE_REFUSED) return 0;
    uint32_t front = (w0 >> NVN_BIND_POLYGON_FRONT_SHIFT) & 1u;
    uint32_t cull = w0 & NVN_BIND_POLYGON_CULL_MASK;
    uint32_t offset = pm < 2u ? pm : 2u;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_POLYGON_METHOD_FRONT, NVN_BIND_POLYGON_FRONT_CW | front);
    if (cull) {
        bind_value(&w, NVN_BIND_POLYGON_METHOD_CULL, 1);
        bind_value(&w, NVN_BIND_POLYGON_METHOD_CULL_FACE, bind_polygon_cull[cull]);
    } else {
        bind_value(&w, NVN_BIND_POLYGON_METHOD_CULL, 0);
    }
    bind_value(&w, NVN_BIND_POLYGON_METHOD_MODE, bind_polygon_mode[pm]);
    bind_value(&w, NVN_BIND_POLYGON_METHOD_MODE_ALIAS, bind_polygon_mode[pm]);
    bind_value(&w, NVN_BIND_POLYGON_METHOD_OFFSET + offset,
               (w0 >> (NVN_BIND_POLYGON_OFFSET_SHIFT + offset)) & 1u);
    return w.at;
}

#define NVN_BIND_BLEND_ADVANCED_SHIFT 8u
#define NVN_BIND_BLEND_ADVANCED_MASK 0x3Fu
#define NVN_BIND_BLEND_TARGET_MASK 7u
#define NVN_BIND_BLEND_FUNC_COUNT 6u
#define NVN_BIND_BLEND_FUNC_STEP 8u
#define NVN_BIND_BLEND_METHOD_FLAGS 0xD35u
#define NVN_BIND_BLEND_METHOD_ENABLE 0x374u
#define NVN_BIND_BLEND_METHOD_TARGET 0x4B9u
#define NVN_BIND_BLEND_METHOD_FUNC 0x781u

/* Advanced blend is a host refusal: its further words are not decoded. */
uint32_t nvn_bind_blend_words(uint32_t w0, uint32_t w1, uint32_t* out) {
    if (!out) return 0;
    if (((w0 >> NVN_BIND_BLEND_ADVANCED_SHIFT) & NVN_BIND_BLEND_ADVANCED_MASK) != 0) return 0;
    uint32_t target = w0 & NVN_BIND_BLEND_TARGET_MASK;
    uint32_t flags = ((w0 >> 19) & 1u) | (((w0 >> 18) & 1u) << 1) | (((w0 >> 16) & 1u) << 2) |
                     (((w0 >> 17) & 1u) << 3);
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_BLEND_METHOD_FLAGS, flags);
    bind_value(&w, NVN_BIND_BLEND_METHOD_ENABLE, 0);
    bind_value(&w, NVN_BIND_BLEND_METHOD_TARGET, 1);
    bind_run(&w, NVN_BIND_BLEND_METHOD_FUNC + NVN_BIND_BLEND_FUNC_STEP * target, NVN_BIND_BLEND_FUNC_COUNT);
    bind_word(&w, (w0 >> 24) & 7u);
    bind_word(&w, (w1 & 0x1Fu) | (((w1 >> 5) & 3u) << 14));
    bind_word(&w, ((w1 << 1) & 0xC000u) | ((w1 >> 8) & 0x1Fu));
    bind_word(&w, (w0 >> 28) & 7u);
    bind_word(&w, ((w1 >> 7) & 0xC000u) | ((w1 >> 16) & 0x1Fu));
    bind_word(&w, ((w1 >> 24) & 0x1Fu) | (((w1 >> 29) & 3u) << 14));
    return w.at;
}

#define NVN_BIND_VERTEX_ATTRIB_FORMAT_SHIFT 21u
#define NVN_BIND_VERTEX_ATTRIB_FORMAT_MASK 0x3FFu
#define NVN_BIND_VERTEX_ATTRIB_COUNT 16u
#define NVN_BIND_VERTEX_ATTRIB_SET_BIT 0x40u
#define NVN_BIND_VERTEX_ATTRIB_KEEP 0x001FFF9Fu
#define NVN_BIND_VERTEX_ATTRIB_DEFAULT 0x3A400040u
#define NVN_BIND_VERTEX_ATTRIB_METHOD 0x458u

uint32_t nvn_vertex_attrib_format_index(uint32_t s) {
    return (s >> NVN_BIND_VERTEX_ATTRIB_FORMAT_SHIFT) & NVN_BIND_VERTEX_ATTRIB_FORMAT_MASK;
}

uint32_t nvn_highest_enabled_index(const uint8_t* enabled, uint32_t count) {
    uint32_t highest = 0;
    if (!enabled) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (enabled[i]) highest = i + 1u;
    return highest;
}

/* 16 attribute words, from the state words below count and the default word above it. */
uint32_t nvn_bind_vertex_attrib_words(uint32_t count, const uint32_t* s, const uint32_t* fmt, uint32_t* out) {
    if (!out || count > NVN_BIND_VERTEX_ATTRIB_COUNT || (count && (!s || !fmt))) return 0;
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_BIND_VERTEX_ATTRIB_METHOD, NVN_BIND_VERTEX_ATTRIB_COUNT);
    for (uint32_t i = 0; i < count; i++) {
        if ((s[i] & NVN_BIND_VERTEX_ATTRIB_SET_BIT) == 0)
            bind_word(&w, NVN_BIND_VERTEX_ATTRIB_DEFAULT);
        else
            bind_word(&w, (s[i] & NVN_BIND_VERTEX_ATTRIB_KEEP) | fmt[i]);
    }
    while (w.at < NVN_BIND_VERTEX_ATTRIB_WORDS_MAX) bind_word(&w, NVN_BIND_VERTEX_ATTRIB_DEFAULT);
    return w.at;
}

#define NVN_BIND_VERTEX_STREAM_COUNT 16u
#define NVN_BIND_VERTEX_STREAM_STRIDE_MASK 0xFFFu
#define NVN_BIND_VERTEX_STREAM_STRIDE_VALID 0x1000u
#define NVN_BIND_VERTEX_STREAM_METHOD_STRIDE 0x700u
#define NVN_BIND_VERTEX_STREAM_STRIDE_STEP 4u
#define NVN_BIND_VERTEX_STREAM_METHOD_DIVISOR 0x620u
#define NVN_BIND_VERTEX_STREAM_METHOD_DIVISOR_DATA 0x703u

/* The stride word and the divisor words of each used stream, then a zero stride for each unused stream. */
uint32_t nvn_bind_vertex_stream_words(uint32_t count, const uint32_t* stride, const uint32_t* divisor,
                                      uint32_t* out) {
    if (!out || count > NVN_BIND_VERTEX_STREAM_COUNT || (count && (!stride || !divisor))) return 0;
    NvnBindWriter w = {out, 0};
    for (uint32_t i = 0; i < count; i++) {
        bind_value(&w, NVN_BIND_VERTEX_STREAM_METHOD_STRIDE + NVN_BIND_VERTEX_STREAM_STRIDE_STEP * i,
                   (stride[i] & NVN_BIND_VERTEX_STREAM_STRIDE_MASK) | NVN_BIND_VERTEX_STREAM_STRIDE_VALID);
        if (divisor[i] == 0) {
            bind_value(&w, NVN_BIND_VERTEX_STREAM_METHOD_DIVISOR + i, 0);
        } else {
            bind_value(&w, NVN_BIND_VERTEX_STREAM_METHOD_DIVISOR + i, 1);
            bind_run(&w, NVN_BIND_VERTEX_STREAM_METHOD_DIVISOR_DATA + NVN_BIND_VERTEX_STREAM_STRIDE_STEP * i, 1);
            bind_word(&w, divisor[i]);
        }
    }
    for (uint32_t i = count; i < NVN_BIND_VERTEX_STREAM_COUNT; i++)
        bind_value(&w, NVN_BIND_VERTEX_STREAM_METHOD_STRIDE + NVN_BIND_VERTEX_STREAM_STRIDE_STEP * i, 0);
    return w.at;
}

#define NVN_BIND_VERTEX_BUFFER_COUNT 16u
#define NVN_BIND_VERTEX_BUFFER_METHOD_ADDR 0x701u
#define NVN_BIND_VERTEX_BUFFER_ADDR_STEP 4u
#define NVN_BIND_VERTEX_BUFFER_METHOD_LIMIT 0x7C0u
#define NVN_BIND_VERTEX_BUFFER_LIMIT_STEP 2u
#define NVN_BIND_VERTEX_BUFFER_DISABLED_ADDR 0x1000u
#define NVN_BIND_VERTEX_BUFFER_DISABLED_LIMIT 0xFFFu

/* The address and limit packets. A zero size disables the buffer, and an address sum that wraps is a host refusal. */
uint32_t nvn_bind_vertex_buffer_words(uint32_t index, uint64_t addr, uint64_t size, uint32_t* out) {
    if (!out || index >= NVN_BIND_VERTEX_BUFFER_COUNT) return 0;
    uint64_t start = addr, limit;
    if (size) {
        if (addr + size < addr) return 0;
        limit = addr + size - 1u;
    } else {
        start = NVN_BIND_VERTEX_BUFFER_DISABLED_ADDR;
        limit = NVN_BIND_VERTEX_BUFFER_DISABLED_LIMIT;
    }
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_BIND_VERTEX_BUFFER_METHOD_ADDR + NVN_BIND_VERTEX_BUFFER_ADDR_STEP * index, 2);
    bind_word(&w, (uint32_t)(start >> 32));
    bind_word(&w, (uint32_t)start);
    bind_run(&w, NVN_BIND_VERTEX_BUFFER_METHOD_LIMIT + NVN_BIND_VERTEX_BUFFER_LIMIT_STEP * index, 2);
    bind_word(&w, (uint32_t)(limit >> 32));
    bind_word(&w, (uint32_t)limit);
    return w.at;
}

#define NVN_BIND_STAGE_COUNT 5u
#define NVN_BIND_UNIFORM_SLOT_COUNT 32u
#define NVN_BIND_UNIFORM_SLOT_BIAS 3u
#define NVN_BIND_UNIFORM_SLOT_MASK 0x1Fu
#define NVN_BIND_UNIFORM_SLOT_SHIFT 4u
#define NVN_BIND_UNIFORM_ALIGN 0x100u
#define NVN_BIND_UNIFORM_DATA_COUNT 3u
#define NVN_BIND_UNIFORM_METHOD_DATA 0x8E0u
#define NVN_BIND_UNIFORM_METHOD_GROUP 0x904u
#define NVN_BIND_UNIFORM_GROUP_STEP 8u

static const uint32_t bind_uniform_stage_group[NVN_BIND_STAGE_COUNT] = {0u, 4u, 3u, 1u, 2u};

/* The size and address packet, then the bind group word with its valid bit. A zero size writes the invalid bind. */
uint32_t nvn_bind_uniform_buffer_words(uint32_t stage, uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out) {
    if (!out || stage >= NVN_BIND_STAGE_COUNT || slot >= NVN_BIND_UNIFORM_SLOT_COUNT) return 0;
    uint32_t group = bind_uniform_stage_group[stage];
    uint32_t method = NVN_BIND_UNIFORM_METHOD_GROUP + NVN_BIND_UNIFORM_GROUP_STEP * group;
    uint32_t index = ((slot + NVN_BIND_UNIFORM_SLOT_BIAS) & NVN_BIND_UNIFORM_SLOT_MASK) << NVN_BIND_UNIFORM_SLOT_SHIFT;
    NvnBindWriter w = {out, 0};
    if (size == 0) {
        bind_value(&w, method, index);
        return w.at;
    }
    if (size > UINT32_MAX - (NVN_BIND_UNIFORM_ALIGN - 1u)) return 0;
    uint32_t rounded = (size + (NVN_BIND_UNIFORM_ALIGN - 1u)) & ~(NVN_BIND_UNIFORM_ALIGN - 1u);
    bind_run(&w, NVN_BIND_UNIFORM_METHOD_DATA, NVN_BIND_UNIFORM_DATA_COUNT);
    bind_word(&w, rounded);
    bind_word(&w, (uint32_t)(addr >> 32));
    bind_word(&w, (uint32_t)addr);
    bind_value(&w, method, 1u | index);
    return w.at;
}

#define NVN_BIND_STORAGE_SLOT_COUNT 128u
#define NVN_BIND_STORAGE_PARAM_SCALE 4u
#define NVN_BIND_STORAGE_DATA_COUNT 3u
#define NVN_BIND_STORAGE_METHOD_PARAM 0xE20u
#define NVN_BIND_STORAGE_METHOD_DATA 0x8E4u

static const uint32_t bind_storage_stage_base[NVN_BIND_STAGE_COUNT] = {0x11u, 0x51u, 0x41u, 0x21u, 0x31u};

/* The parameter word, then the address and size packet. The parameter must fit the 13-bit inline field. */
uint32_t nvn_bind_storage_buffer_words(uint32_t stage, uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out) {
    if (!out || stage >= NVN_BIND_STAGE_COUNT || slot >= NVN_BIND_STORAGE_SLOT_COUNT) return 0;
    uint32_t param = NVN_BIND_STORAGE_PARAM_SCALE * (bind_storage_stage_base[stage] + slot);
    if (param > NVN_PROGRAM_IMMEDIATE_VALUE_MAX) return 0;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_STORAGE_METHOD_PARAM, param);
    bind_run(&w, NVN_BIND_STORAGE_METHOD_DATA, NVN_BIND_STORAGE_DATA_COUNT);
    bind_word(&w, (uint32_t)addr);
    bind_word(&w, (uint32_t)(addr >> 32));
    bind_word(&w, size);
    return w.at;
}

#define NVN_BIND_TEXTURE_SLOT_COUNT 128u
#define NVN_BIND_TEXTURE_SLOT_STEP 8u
#define NVN_BIND_TEXTURE_SLOT_BASE 0x20u
#define NVN_BIND_TEXTURE_HANDLE_COUNT 2u
#define NVN_BIND_TEXTURE_METHOD_PARAM 0xE0Eu
#define NVN_BIND_TEXTURE_METHOD_DESCRIPTOR 0x68Bu
#define NVN_BIND_TEXTURE_METHOD_HANDLE 0x8E3u

static const uint32_t bind_texture_stage_map[NVN_BIND_STAGE_COUNT] = {0u, 4u, 3u, 1u, 2u};

/* The stage parameter, the optional descriptor flag, and the slot and low handle word. The high handle word is not
   written. */
uint32_t nvn_bind_texture_words(uint32_t stage, uint32_t slot, uint64_t handle, int descriptor_flag, uint32_t* out) {
    if (!out || stage >= NVN_BIND_STAGE_COUNT || slot >= NVN_BIND_TEXTURE_SLOT_COUNT) return 0;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_TEXTURE_METHOD_PARAM, bind_texture_stage_map[stage]);
    if (descriptor_flag) bind_value(&w, NVN_BIND_TEXTURE_METHOD_DESCRIPTOR, 0);
    bind_run(&w, NVN_BIND_TEXTURE_METHOD_HANDLE, NVN_BIND_TEXTURE_HANDLE_COUNT);
    bind_word(&w, slot * NVN_BIND_TEXTURE_SLOT_STEP + NVN_BIND_TEXTURE_SLOT_BASE);
    bind_word(&w, (uint32_t)handle);
    return w.at;
}

/* The draw macros and methods that the five library draw functions write. The headers use the same forms as the bind
   packets: a mode 5 macro of count words, a mode 1 run and a mode 4 inline value. */
#define NVN_DRAW_TYPE_MAX                 0x1FFFu
#define NVN_DRAW_MACRO_ARRAYS             0xE32u
#define NVN_DRAW_MACRO_ARRAYS_INSTANCED   0xE36u
#define NVN_DRAW_MACRO_ARRAYS_CONSTANTS   0xE44u
#define NVN_DRAW_MACRO_ELEMENTS           0xE38u
#define NVN_DRAW_MACRO_ELEMENTS_INSTANCED 0xE3Cu
#define NVN_DRAW_MACRO_ELEMENTS_CONSTANTS 0xE46u
#define NVN_DRAW_METHOD_CONSTANTS         0xE20u
#define NVN_DRAW_CONSTANTS_DRIVER_BUFFER  0x190u
#define NVN_DRAW_METHOD_TYPE              0x5F6u
#define NVN_DRAW_METHOD_ADDRESS           0x5F2u
#define NVN_DRAW_METHOD_BASE_VERTEX       0x8E4u
#define NVN_DRAW_METHOD_VERTEX_HIGH       0x446u
#define NVN_DRAW_METHOD_VERTEX_LOW        0x50Du
#define NVN_DRAW_METHOD_STREAM            0x8E3u
#define NVN_DRAW_STREAM_LIMIT             0x640u
#define NVN_DRAW_ARRAYS_WORDS             3u
#define NVN_DRAW_ARRAYS_INSTANCED_WORDS   5u
#define NVN_DRAW_ELEMENTS_WORDS           5u
#define NVN_DRAW_ELEMENTS_INSTANCED_WORDS 6u
#define NVN_DRAW_SINGLE_WORD_RUN          1u
#define NVN_DRAW_TWO_WORD_RUN             2u

static uint32_t draw_arrays_words(NvnBindWriter* w, uint32_t mode, uint32_t first, uint32_t count) {
    bind_macro(w, NVN_DRAW_MACRO_ARRAYS, NVN_DRAW_ARRAYS_WORDS);
    bind_word(w, mode);
    bind_word(w, first);
    bind_word(w, count);
    return w->at;
}

static uint32_t draw_arrays_instanced_words(NvnBindWriter* w, uint32_t mode, uint32_t count, uint32_t first,
                                            uint32_t base_instance, uint32_t instances, int* uses_constants) {
    if (base_instance == 0) {
        bind_macro(w, NVN_DRAW_MACRO_ARRAYS_INSTANCED, NVN_DRAW_ARRAYS_INSTANCED_WORDS);
    } else {
        bind_value(w, NVN_DRAW_METHOD_CONSTANTS, NVN_DRAW_CONSTANTS_DRIVER_BUFFER);
        bind_macro(w, NVN_DRAW_MACRO_ARRAYS_CONSTANTS, NVN_DRAW_ARRAYS_INSTANCED_WORDS);
        if (uses_constants) *uses_constants = 1;
    }
    bind_word(w, mode);
    bind_word(w, count);
    bind_word(w, instances);
    bind_word(w, first);
    bind_word(w, base_instance);
    return w->at;
}

static void draw_elements_block(NvnBindWriter* w, uint32_t macro, uint32_t mode, uint32_t type, uint32_t count,
                                uint32_t a_hi, uint32_t a_lo) {
    bind_macro(w, macro, NVN_DRAW_ELEMENTS_WORDS);
    bind_word(w, mode);
    bind_word(w, a_hi);
    bind_word(w, a_lo);
    bind_word(w, type);
    bind_word(w, count);
}

static uint32_t draw_elements_words(NvnBindWriter* w, uint32_t mode, uint32_t type, uint32_t count, uint32_t a_hi,
                                    uint32_t a_lo) {
    draw_elements_block(w, NVN_DRAW_MACRO_ELEMENTS, mode, type, count, a_hi, a_lo);
    return w->at;
}

static uint32_t draw_elements_base_vertex_words(NvnBindWriter* w, uint32_t mode, uint32_t type, uint32_t count,
                                                uint32_t a_hi, uint32_t a_lo, uint32_t base_vertex,
                                                int* uses_constants) {
    if (base_vertex != 0) {
        bind_value(w, NVN_DRAW_METHOD_CONSTANTS, NVN_DRAW_CONSTANTS_DRIVER_BUFFER);
        bind_run(w, NVN_DRAW_METHOD_BASE_VERTEX, NVN_DRAW_SINGLE_WORD_RUN);
        bind_word(w, base_vertex);
        if (uses_constants) *uses_constants = 1;
    }
    bind_run(w, NVN_DRAW_METHOD_VERTEX_HIGH, NVN_DRAW_SINGLE_WORD_RUN);
    bind_word(w, base_vertex);
    bind_run(w, NVN_DRAW_METHOD_VERTEX_LOW, NVN_DRAW_SINGLE_WORD_RUN);
    bind_word(w, base_vertex);
    draw_elements_block(w, NVN_DRAW_MACRO_ELEMENTS, mode, type, count, a_hi, a_lo);
    bind_value(w, NVN_DRAW_METHOD_VERTEX_LOW, 0);
    bind_value(w, NVN_DRAW_METHOD_VERTEX_HIGH, 0);
    if (base_vertex != 0) {
        bind_run(w, NVN_DRAW_METHOD_STREAM, NVN_DRAW_TWO_WORD_RUN);
        bind_word(w, NVN_DRAW_STREAM_LIMIT);
        bind_word(w, 0);
    }
    return w->at;
}

static uint32_t draw_elements_instanced_words(NvnBindWriter* w, uint32_t mode, uint32_t type, uint32_t count,
                                              uint32_t a_hi, uint32_t a_lo, uint32_t base_vertex,
                                              uint32_t base_instance, uint32_t instances, int* uses_constants) {
    int constants = (base_vertex | base_instance) != 0;
    if (constants) {
        bind_value(w, NVN_DRAW_METHOD_CONSTANTS, NVN_DRAW_CONSTANTS_DRIVER_BUFFER);
        if (uses_constants) *uses_constants = 1;
    }
    bind_value(w, NVN_DRAW_METHOD_TYPE, type);
    bind_run(w, NVN_DRAW_METHOD_ADDRESS, NVN_DRAW_TWO_WORD_RUN);
    bind_word(w, a_hi);
    bind_word(w, a_lo);
    bind_macro(w, constants ? NVN_DRAW_MACRO_ELEMENTS_CONSTANTS : NVN_DRAW_MACRO_ELEMENTS_INSTANCED,
               NVN_DRAW_ELEMENTS_INSTANCED_WORDS);
    bind_word(w, mode);
    bind_word(w, count);
    bind_word(w, instances);
    bind_word(w, 0);
    bind_word(w, base_vertex);
    bind_word(w, base_instance);
    return w->at;
}

uint32_t nvn_draw_words(uint32_t kind, uint32_t mode, uint32_t type, uint32_t count, uint64_t address, uint32_t first,
                        uint32_t base_vertex, uint32_t base_instance, uint32_t instances, uint32_t* out,
                        int* uses_constants) {
    if (!out || kind >= (uint32_t)NVN_DRAW_KIND_COUNT || type > NVN_DRAW_TYPE_MAX) return 0;
    if (uses_constants) *uses_constants = 0;
    NvnBindWriter w = {out, 0};
    uint32_t a_hi = (uint32_t)(address >> 32);
    uint32_t a_lo = (uint32_t)address;
    switch (kind) {
    case NVN_DRAW_ARRAYS: return draw_arrays_words(&w, mode, first, count);
    case NVN_DRAW_ARRAYS_INSTANCED:
        return draw_arrays_instanced_words(&w, mode, count, first, base_instance, instances, uses_constants);
    case NVN_DRAW_ELEMENTS: return draw_elements_words(&w, mode, type, count, a_hi, a_lo);
    case NVN_DRAW_ELEMENTS_BASE_VERTEX:
        return draw_elements_base_vertex_words(&w, mode, type, count, a_hi, a_lo, base_vertex, uses_constants);
    case NVN_DRAW_ELEMENTS_INSTANCED:
        return draw_elements_instanced_words(&w, mode, type, count, a_hi, a_lo, base_vertex, base_instance,
                                             instances, uses_constants);
    default: return 0;
    }
}

/* The launch description words that a compute dispatch writes (maxwell/kepler_compute_regs.h's QMD_* names). */
enum {
    COMPUTE_QMD_PROGRAM_START = 0x8,
    COMPUTE_QMD_GRID_DIM_X = 0xC,
    COMPUTE_QMD_GRID_DIM_YZ = 0xD,
    COMPUTE_QMD_SHARED_ALLOC = 0x11,
    COMPUTE_QMD_BLOCK_DIM_X = 0x12,
    COMPUTE_QMD_BLOCK_DIM_YZ = 0x13,
    COMPUTE_QMD_CONST_BUFFER_ENABLE = 0x14,
    COMPUTE_QMD_CONST_BUFFER_CONFIG = 0x1D,
    COMPUTE_QMD_LOCAL_POS_ALLOC = 0x2D,
    COMPUTE_QMD_LOCAL_NEG_ALLOC = 0x2E,
    COMPUTE_QMD_LOCAL_CRS_ALLOC = 0x2F
};
#define COMPUTE_QMD_GRID_X_MAX 0x7FFFFFFFu
#define COMPUTE_QMD_GRID_YZ_MAX 0xFFFFu
#define COMPUTE_QMD_GRID_Z_SHIFT 16u
#define COMPUTE_QMD_BLOCK_X_SHIFT 16u
#define COMPUTE_QMD_BLOCK_Z_SHIFT 16u
#define COMPUTE_QMD_SHARED_ALLOC_SHIFT 8u
#define COMPUTE_QMD_SHARED_ALLOC_MASK 0x3FFu
#define COMPUTE_QMD_LOCAL_MASK 0xFFFFFu
#define COMPUTE_QMD_BARRIER_SHIFT 27u
#define COMPUTE_QMD_BARRIER_MASK 0x1Fu
#define COMPUTE_QMD_GPR_SHIFT 24u
#define COMPUTE_QMD_GPR_ALLOC_UNKNOWN 0u    /* the byte at x21+0x823: vk_dispatch does not read it */
#define COMPUTE_QMD_SASS_VERSION_UNKNOWN 0u /* the byte at x21+0x82b: vk_dispatch does not read it */
#define COMPUTE_QMD_CBUF0_ENABLE 0x1u       /* the library leaves it clear; vk_dispatch reads cbuf 0 for storage */

/* Writes the address and size pair of one QMD constant buffer. The size sits in bits 31:15 of the second word. */
static void compute_qmd_cbuf_config(uint32_t* qmd, uint32_t index, uint64_t address, uint32_t size) {
    uint32_t at = COMPUTE_QMD_CONST_BUFFER_CONFIG + index * 2u;
    qmd[at] = (uint32_t)address;
    qmd[at + 1] = ((uint32_t)(address >> 32) & 0xFFu) | (size << NVN_COMPUTE_CBUF_ADDRESS_HIGH_SHIFT);
}

uint32_t nvn_compute_qmd_check(const NvnProgramComputeIn* prog, uint32_t x, uint32_t y, uint32_t z,
                               const NvnComputeBinds* binds, uint32_t* detail) {
    if (detail) *detail = 0;
    if (!prog || !binds) return NVN_QMD_NULL_ARGUMENT;
    if (x > COMPUTE_QMD_GRID_X_MAX) return NVN_QMD_GRID_X;
    if (y > COMPUTE_QMD_GRID_YZ_MAX) return NVN_QMD_GRID_Y;
    if (z > COMPUTE_QMD_GRID_YZ_MAX) return NVN_QMD_GRID_Z;
    if ((uint64_t)prog->h6fc + 0xFFu > UINT32_MAX) return NVN_QMD_CBUF1_SIZE;
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_SLOTS; slot++) {
        const NvnComputeBindEntry* entry = &binds->uniform[slot];
        if (!entry->bound || entry->size <= UINT32_MAX) continue;
        if (detail) *detail = slot;
        return NVN_QMD_UNIFORM_SIZE;
    }
    if (detail) *detail = 0;
    return NVN_QMD_OK;
}

const char* nvn_compute_qmd_reason_name(uint32_t reason) {
    switch (reason) {
    case NVN_QMD_OK: return "ok";
    case NVN_QMD_NULL_ARGUMENT: return "null argument";
    case NVN_QMD_GRID_X: return "grid x above its field";
    case NVN_QMD_GRID_Y: return "grid y above its field";
    case NVN_QMD_GRID_Z: return "grid z above its field";
    case NVN_QMD_CBUF1_SIZE: return "constant buffer 1 size above 32 bits";
    case NVN_QMD_UNIFORM_SIZE: return "uniform size above 32 bits";
    default: return "unknown";
    }
}

int nvn_compute_qmd_build(const NvnProgramComputeIn* prog, uint32_t x, uint32_t y, uint32_t z,
                          const NvnComputeBinds* binds, uint32_t qmd[NVN_COMPUTE_QMD_WORDS]) {
    if (!qmd || nvn_compute_qmd_check(prog, x, y, z, binds, NULL) != NVN_QMD_OK) return 0;
    uint64_t cb1_size = (uint64_t)prog->h6fc + 0xFFu;
    memset(qmd, 0, NVN_COMPUTE_QMD_WORDS * sizeof qmd[0]);
    qmd[COMPUTE_QMD_PROGRAM_START] = prog->code_offset;
    qmd[COMPUTE_QMD_GRID_DIM_X] = x & COMPUTE_QMD_GRID_X_MAX;
    qmd[COMPUTE_QMD_GRID_DIM_YZ] = (y & COMPUTE_QMD_GRID_YZ_MAX) | (z << COMPUTE_QMD_GRID_Z_SHIFT);
    qmd[COMPUTE_QMD_SHARED_ALLOC] =
        (((prog->h724 + 0xFFu) >> COMPUTE_QMD_SHARED_ALLOC_SHIFT) & COMPUTE_QMD_SHARED_ALLOC_MASK)
        << COMPUTE_QMD_SHARED_ALLOC_SHIFT;
    qmd[COMPUTE_QMD_BLOCK_DIM_X] = (prog->h718 & COMPUTE_QMD_GRID_YZ_MAX) << COMPUTE_QMD_BLOCK_X_SHIFT;
    qmd[COMPUTE_QMD_BLOCK_DIM_YZ] = (prog->h71c & COMPUTE_QMD_GRID_YZ_MAX) |
                                   ((prog->h720 & COMPUTE_QMD_GRID_YZ_MAX) << COMPUTE_QMD_BLOCK_Z_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_POS_ALLOC] =
        (prog->h728 & COMPUTE_QMD_LOCAL_MASK) | ((prog->h734 & COMPUTE_QMD_BARRIER_MASK) << COMPUTE_QMD_BARRIER_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_NEG_ALLOC] =
        (prog->h72c & COMPUTE_QMD_LOCAL_MASK) | (COMPUTE_QMD_GPR_ALLOC_UNKNOWN << COMPUTE_QMD_GPR_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_CRS_ALLOC] =
        (prog->h730 & COMPUTE_QMD_LOCAL_MASK) | (COMPUTE_QMD_SASS_VERSION_UNKNOWN << COMPUTE_QMD_GPR_SHIFT);
    uint32_t enable = COMPUTE_QMD_CBUF0_ENABLE;
    compute_qmd_cbuf_config(qmd, 0, binds->cbuf0_address, NVN_COMPUTE_CBUF0_BYTES);
    if (prog->h6fc) {
        enable |= 1u << 1;
        compute_qmd_cbuf_config(qmd, 1, NVN_COMPUTE_SHADER_REGION_BASE + prog->cb_offset, (uint32_t)cb1_size);
    }
    /* The library enables the constant buffer of a bound slot when its size rounds up to 0x100 or more (size >= 1). */
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_QBUF_SLOTS; slot++) {
        const NvnComputeBindEntry* entry = &binds->uniform[slot];
        if (!entry->bound || entry->size == 0) continue;
        enable |= 1u << (slot + 3u);
        compute_qmd_cbuf_config(qmd, slot + 3u, entry->addr,
                                ((uint32_t)entry->size + 0xFFu) & ~0xFFu);
    }
    qmd[COMPUTE_QMD_CONST_BUFFER_ENABLE] = enable;
    return 1;
}

/* Writes one 16-byte descriptor: address low, address high, size, then a zero word. */
static void compute_descriptor_write(uint32_t* words, uint32_t base, uint32_t slot,
                                     const NvnComputeBindEntry* entry) {
    uint32_t at = (base + slot * NVN_COMPUTE_DESCRIPTOR_BYTES) / 4u;
    words[at] = (uint32_t)entry->addr;
    words[at + 1] = (uint32_t)(entry->addr >> 32) & 0xFFu;
    words[at + 2] = (uint32_t)entry->size;
    words[at + 3] = 0;
}

int nvn_compute_cbuf0_build(const NvnComputeBindEntry* uniform, const NvnComputeBindEntry* storage,
                            uint32_t words[NVN_COMPUTE_CBUF0_WORDS]) {
    if (!uniform || !storage || !words) return 0;
    for (uint32_t slot = 0; slot < NVN_COMPUTE_STORAGE_SLOTS; slot++)
        if (storage[slot].bound && (slot >= NVN_COMPUTE_DESCRIPTOR_SLOTS || storage[slot].size > UINT32_MAX))
            return 0;
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_SLOTS; slot++)
        if (uniform[slot].bound && (slot >= NVN_COMPUTE_DESCRIPTOR_SLOTS || uniform[slot].size > UINT32_MAX))
            return 0;
    /* The rest of the block that the library copies is not decoded; those bytes are zero (guess). */
    memset(words, 0, NVN_COMPUTE_CBUF0_BYTES);
    for (uint32_t slot = 0; slot < NVN_COMPUTE_DESCRIPTOR_SLOTS; slot++) {
        if (uniform[slot].bound)
            compute_descriptor_write(words, NVN_COMPUTE_CBUF0_UNIFORM_OFFSET, slot, &uniform[slot]);
        if (storage[slot].bound)
            compute_descriptor_write(words, NVN_COMPUTE_CBUF0_STORAGE_OFFSET, slot, &storage[slot]);
    }
    return 1;
}

