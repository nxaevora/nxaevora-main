/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_params.h"
#include "nx/gpu/nvn_cmdlist.h"
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
void nvn_window_builder_set_active_textures(NvnWindowBuilder* b, uint32_t count) {
    b->active_texture_count = count;
}
uint32_t nvn_window_builder_present_interval(const NvnWindowBuilder* b) {
    return b ? b->interval : 0;
}
uint32_t nvn_window_active_textures(uint32_t count) { return count; }
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
int nvn_window_has_presented(const uint8_t* states, uint32_t count) {
    if (!states || count > NVN_WINDOW_MAX_TEXTURES) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (states[i] == NVN_IMAGE_PRESENTED) return 1;
    return 0;
}
int nvn_window_texture_slot(uint64_t textures, uint32_t texture_count, int32_t index, uint64_t* at) {
    if ((int32_t)texture_count <= index) return 0;
    *at = textures + (uint64_t)((int64_t)index * (int64_t)sizeof(uint64_t));
    return 1;
}
const char* nvn_window_acquire_refusal(int recheck, int queue_ok, int window_ok, int sync_ok, int index_writable) {
    if (recheck) {
        if (!window_ok) return "window changed";
        if (!sync_ok) return "sync changed";
        if (!index_writable) return "index changed";
        return NULL;
    }
    if (!queue_ok) return "unknown queue";
    if (!window_ok) return "unknown window";
    if (!sync_ok) return "unknown sync";
    if (!index_writable) return "unwritable index";
    return NULL;
}
void nvn_window_acquire_take(uint8_t* states, int32_t* last_index, int32_t index) {
    states[index] = NVN_IMAGE_ACQUIRED;
    *last_index = index;
}
NvnSyncAction nvn_sync_wait_action(NvnSyncState state, int reached) {
    switch (state) {
    case NVN_SYNC_SIGNALED: return NVN_SYNC_ACTION_SIGNALED;
    case NVN_SYNC_PENDING: return NVN_SYNC_ACTION_WAIT_FRAME;
    case NVN_SYNC_FENCED: return reached ? NVN_SYNC_ACTION_SIGNALED : NVN_SYNC_ACTION_WAIT_FENCE;
    default: return NVN_SYNC_ACTION_REFUSE; /* UNSIGNALED never gets a frame job */
    }
}
uint32_t nvn_sync_fence_value(int has_posted, uint32_t last_posted, uint32_t syncpoint_now) {
    return has_posted ? last_posted : syncpoint_now;
}
NvnWaitDeadline nvn_wait_deadline(uint64_t timeout_ns, uint64_t now_ns) {
    if (timeout_ns == NVN_SYNC_WAIT_NO_LIMIT) return (NvnWaitDeadline){0, 0};
    uint64_t deadline = now_ns + timeout_ns;
    if (deadline < now_ns) deadline = UINT64_MAX; /* the clock cannot hold now + timeout: keep it finite */
    return (NvnWaitDeadline){deadline, 1};
}
int64_t nvn_wait_remaining_ns(NvnWaitDeadline limit, uint64_t now_ns) {
    if (!limit.has_deadline) return -1;
    if (now_ns >= limit.deadline_ns) return 0;
    uint64_t left = limit.deadline_ns - now_ns;
    return left > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)left;
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

/* nvnCommandBufferGetControlMemorySize: the library reads the limit at cb+0x40 and the base at cb+0x60 and adds the
   reserve that AddControlMemory subtracts. Before any Add both fields are 0 and the result is the reserve alone. */
uint64_t nvn_control_memory_size(uint64_t begin, uint64_t end, uint32_t adds) {
    if (!adds) return NVN_CONTROL_MEMORY_RESERVE;
    return end - begin;
}

/* nvnDeviceRegisterFastClearColor: the library takes a format in w2 and accepts the float range 10..156. The Colori and
   Colorui forms use 11..120. Outside the range the library returns 0 and makes no driver call. */
int nvn_fast_clear_color_format_ok(NvnFastClearKind kind, uint32_t format) {
    if (kind == NVN_FAST_CLEAR_KIND_INT)
        return format >= NVN_FAST_CLEAR_COLORI_FORMAT_MIN && format <= NVN_FAST_CLEAR_COLORI_FORMAT_MAX;
    return format >= NVN_FAST_CLEAR_COLOR_FORMAT_MIN && format <= NVN_FAST_CLEAR_COLOR_FORMAT_MAX;
}

/* The library masks no index and sign extends index bit 31: texture bits above 0xFFFFF spill into the sampler field and
   a high index reaches the tag. Both are kept, so a handle matches the library. An image handle has no tag. */
uint64_t nvn_texture_handle(uint32_t texture_index, uint32_t sampler_index) {
    int32_t packed = (int32_t)(texture_index | (sampler_index << NVN_HANDLE_SAMPLER_SHIFT));
    return (uint64_t)(int64_t)packed | NVN_TEXTURE_HANDLE_TAG;
}

uint64_t nvn_image_handle(uint32_t image_index) { return (uint64_t)(int64_t)(int32_t)image_index; }

/* The separate sampler handle is the sampler field of a combined handle: index bit 31 sign extends, then the u64
   shifts into the sampler field. No tag. */
uint64_t nvn_separate_sampler_handle(uint32_t sampler_index) {
    uint64_t extended = (uint64_t)(int64_t)(int32_t)sampler_index;
    return extended << NVN_HANDLE_SAMPLER_SHIFT;
}

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

void nvn_vertex_attrib_state_get_format(uint32_t word, uint32_t* format, uint64_t* relative_offset) {
    *format = (word >> 21) & 0x3FFu;
    *relative_offset = (word >> 7) & 0x3FFFu;
}

uint32_t nvn_vertex_attrib_state_get_stream_index(uint32_t word) { return word & 0x1Fu; }

/* The library table at sdk 0xde877bc (update 1.1.1), one word for each counter type. */
static const uint32_t reset_counter_words[] = {0x01u, 0x01u, 0x12u, 0x13u, 0x15u, 0x16u, 0x17u, 0x1Au, 0x1Eu,
                                               0x18u, 0x1Bu, 0x1Cu, 0x1Du, 0x1Fu, 0x10u, 0x02u, 0x01u};

int nvn_reset_counter_word(uint32_t type, uint32_t* word) {
    if (type >= sizeof reset_counter_words / sizeof reset_counter_words[0]) return 0;
    *word = reset_counter_words[type];
    return 1;
}

void nvn_color_state_defaults(uint32_t* words) { words[0] = 0x08030000u; }

void nvn_depth_stencil_state_defaults(uint32_t* words) {
    words[0] = 0x00000022u;
    words[1] = 0x11181118u;
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

uint32_t nvn_blend_state_get_advanced_mode(uint32_t word) { return (word >> 8) & 0x3Fu; }
uint32_t nvn_blend_state_get_advanced_overlap(uint32_t word) { return (word >> 16) & 0x3u; }
uint32_t nvn_blend_state_get_advanced_premultiplied_src(uint32_t word) { return (word >> 18) & 1u; }
uint32_t nvn_blend_state_get_advanced_normalized_dst(uint32_t word) { return (word >> 19) & 1u; }
uint32_t nvn_blend_state_set_advanced_mode(uint32_t word, uint32_t value) {
    return nvn_state_insert(word, 0x3F00u, 8, value);
}
uint32_t nvn_blend_state_set_advanced_overlap(uint32_t word, uint32_t value) {
    return nvn_state_insert(word, 0x30000u, 16, value);
}
uint32_t nvn_blend_state_set_advanced_premultiplied_src(uint32_t word, uint32_t value) {
    return nvn_state_set_flag(word, 18, value);
}
uint32_t nvn_blend_state_set_advanced_normalized_dst(uint32_t word, uint32_t value) {
    return nvn_state_set_flag(word, 19, value);
}

uint32_t nvn_multisample_state_get_sample_locations_enable(uint32_t word) { return (word >> 3) & 1u; }
uint32_t nvn_multisample_state_get_sample_locations_grid_enable(uint32_t word) { return (word >> 4) & 1u; }

/* The jump table at sdk de860a2. The count of 0 and 1 both give the 4 by 4 grid. */
int nvn_multisample_state_sample_grid(uint32_t samples, uint32_t raster_samples, uint32_t* x, uint32_t* y) {
    uint32_t index = samples > raster_samples ? samples : raster_samples;
    uint32_t width, height;
    switch (index) {
    case 0: case 1: width = 4; height = 4; break;
    case 2: width = 2; height = 4; break;
    case 4: width = 2; height = 2; break;
    case 8: width = 1; height = 2; break;
    case 16: width = 1; height = 1; break;
    default: return 0;
    }
    if (x) *x = width;
    if (y) *y = height;
    return 1;
}

#define MULTISAMPLE_LOCATION_MAX 0.9375f
#define MULTISAMPLE_LOCATION_SCALE 16.0f

static uint32_t sample_location_nibble(float value) {
    if (value < 0) value = 0;
    if (value > 1.0f) value = 1.0f;
    value = fminf(value, MULTISAMPLE_LOCATION_MAX); /* fminnm: a NaN gives the maximum, as the library */
    return (uint32_t)(value * MULTISAMPLE_LOCATION_SCALE + 0.5f);
}

uint8_t nvn_multisample_sample_location(float x, float y) {
    return (uint8_t)((sample_location_nibble(y) << 4) | sample_location_nibble(x));
}

int nvn_multisample_locations_fit(uint32_t index, uint32_t count) {
    return index <= NVN_MULTISAMPLE_LOCATION_BYTES && count <= NVN_MULTISAMPLE_LOCATION_BYTES - index;
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
    case NVN_RASTER_POINT_SIZE:
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_POINT_SIZE, w[0]};
        out[n++] = (NvnRasterMethod){NVN_RASTER_METHOD_POINT_SIZE_FOLLOW, 0u};
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

/* The device image that the queue init builders read. The offsets are bytes in the image. */
#define NVN_QUEUE_INIT_DEV_OFFSET_W8C 0x08Cu
#define NVN_QUEUE_INIT_DEV_OFFSET_SM_VERSION 0x090u
#define NVN_QUEUE_INIT_DEV_OFFSET_W94 0x094u
#define NVN_QUEUE_INIT_DEV_OFFSET_W98 0x098u
#define NVN_QUEUE_INIT_DEV_OFFSET_FORM 0x0B0u
#define NVN_QUEUE_INIT_DEV_OFFSET_WE0 0x0E0u
#define NVN_QUEUE_INIT_DEV_OFFSET_WE4 0x0E4u
#define NVN_QUEUE_INIT_DEV_OFFSET_99C 0x99Cu
#define NVN_QUEUE_INIT_DEV_OFFSET_9A4 0x9A4u
#define NVN_QUEUE_INIT_DEV_OFFSET_WINDOW_ORIGIN 0x19D8u
#define NVN_QUEUE_INIT_DEV_OFFSET_DEPTH_MODE 0x19DCu
#define NVN_QUEUE_INIT_DEV_OFFSET_1A84 0x1A84u
#define NVN_QUEUE_INIT_DEV_OFFSET_1AAC 0x1AACu
#define NVN_QUEUE_INIT_DEV_OFFSET_1AB0 0x1AB0u
#define NVN_QUEUE_INIT_DEV_OFFSET_1AC0 0x1AC0u
#define NVN_QUEUE_INIT_DEV_OFFSET_W1AF4 0x1AF4u
#define NVN_QUEUE_INIT_DEV_VALUE_SM_VERSION 0x503u /* the GM20B sm_arch_sm_version */
#define NVN_QUEUE_INIT_DEV_VALUE_FORM 0u           /* the form byte at +0xB0: the builder's value is 0 */
#define NVN_QUEUE_INIT_DEV_VALUE_99C 0x230u
#define NVN_QUEUE_INIT_DEV_VALUE_9A4 0x430u
#define NVN_QUEUE_INIT_DEV_VALUE_1A84 0x111u
#define NVN_QUEUE_INIT_DEV_VALUE_1AAC 0x3Fu
#define NVN_QUEUE_INIT_DEV_VALUE_1AB0 0x00110011u
#define NVN_QUEUE_INIT_DEV_VALUE_1AC0 5u

uint32_t nvn_queue_init_device_words(const NvnQueueDeviceWords* dev, uint32_t window_origin, uint32_t depth_mode,
                                     NvnQueueInitWord* out, uint32_t max) {
    if (!dev || !out || max < NVN_QUEUE_INIT_DEVICE_WORD_COUNT) return 0;
    const NvnQueueInitWord words[NVN_QUEUE_INIT_DEVICE_WORD_COUNT] = {
        {NVN_QUEUE_INIT_DEV_OFFSET_W8C, dev->w8c},
        {NVN_QUEUE_INIT_DEV_OFFSET_SM_VERSION, NVN_QUEUE_INIT_DEV_VALUE_SM_VERSION},
        {NVN_QUEUE_INIT_DEV_OFFSET_W94, dev->w94},
        {NVN_QUEUE_INIT_DEV_OFFSET_W98, dev->w98},
        {NVN_QUEUE_INIT_DEV_OFFSET_FORM, NVN_QUEUE_INIT_DEV_VALUE_FORM},
        {NVN_QUEUE_INIT_DEV_OFFSET_WE0, dev->we0},
        {NVN_QUEUE_INIT_DEV_OFFSET_WE4, dev->we4},
        {NVN_QUEUE_INIT_DEV_OFFSET_99C, NVN_QUEUE_INIT_DEV_VALUE_99C},
        {NVN_QUEUE_INIT_DEV_OFFSET_9A4, NVN_QUEUE_INIT_DEV_VALUE_9A4},
        {NVN_QUEUE_INIT_DEV_OFFSET_WINDOW_ORIGIN, window_origin},
        {NVN_QUEUE_INIT_DEV_OFFSET_DEPTH_MODE, depth_mode},
        {NVN_QUEUE_INIT_DEV_OFFSET_1A84, NVN_QUEUE_INIT_DEV_VALUE_1A84},
        {NVN_QUEUE_INIT_DEV_OFFSET_1AAC, NVN_QUEUE_INIT_DEV_VALUE_1AAC},
        {NVN_QUEUE_INIT_DEV_OFFSET_1AB0, NVN_QUEUE_INIT_DEV_VALUE_1AB0},
        {NVN_QUEUE_INIT_DEV_OFFSET_1AC0, NVN_QUEUE_INIT_DEV_VALUE_1AC0},
        {NVN_QUEUE_INIT_DEV_OFFSET_W1AF4, dev->w1af4},
    };
    memcpy(out, words, sizeof words);
    return NVN_QUEUE_INIT_DEVICE_WORD_COUNT;
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
    if (f & NVN_POOL_IMPORT) return NVN_POOL_UNSUPPORTED_IMPORT;
    /* A virtual pool reserves GPU addresses only: no storage, a whole number of 64 KiB pages (vtbl[0] d83ff88). */
    if (f & NVN_POOL_VIRTUAL)
        return b->storage || !b->size || b->size % NVN_POOL_VIRTUAL_PAGE_SIZE ? NVN_POOL_INVALID_STORAGE
                                                                              : NVN_POOL_OK;
    if (!b->storage || !b->size || b->storage % NVN_POOL_STORAGE_ALIGNMENT ||
        b->size % NVN_POOL_STORAGE_ALIGNMENT) return NVN_POOL_INVALID_STORAGE;
    if (page_size < NVN_POOL_STORAGE_ALIGNMENT || page_size % NVN_POOL_STORAGE_ALIGNMENT ||
        ((f & NVN_POOL_PHYSICAL) && b->size % page_size)) return NVN_POOL_INVALID_PAGE_SIZE;
    return NVN_POOL_OK;
}

int nvn_pool_range_fits(uint64_t pool_size, uint64_t offset, uint64_t size) {
    return offset <= pool_size && size <= pool_size - offset;
}

int nvn_texture_uses_pitch_range(uint32_t texture_flags, uint32_t target) {
    return (texture_flags & NVN_TEXTURE_LINEAR_FLAGS) != 0 || target == NVN_TEXTURE_TARGET_BUFFER; /* d83f418 */
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

uint64_t nvn_memory_pool_map(uint32_t flags, uint64_t storage) {
    return (flags & NVN_POOL_CPU_NO_ACCESS) ? 0 : storage;
}

uint32_t nvn_memory_pool_flags(uint32_t flags) { return flags & NVN_POOL_PUBLIC_FLAGS; }

NvnMapVirtualResult nvn_memory_pool_map_virtual_range(const NvnMapVirtualRequest* request, uint64_t virtual_base,
                                                      uint64_t virtual_size, NvnMapVirtualRange* range) {
    const uint64_t page = NVN_POOL_VIRTUAL_PAGE_SIZE;
    const uint64_t virtual_offset = request->virtual_offset & ~(page - 1);
    const uint64_t size = request->size & ~(page - 1);
    uint64_t pages;
    range->physical_pool = request->physical_pool;
    range->physical_offset = request->physical_offset & ~(page - 1);
    range->physical_base = 0;
    range->virtual_base = 0;
    range->size = size;
    range->pages = 0;
    if (!size) return NVN_MAP_VIRTUAL_SKIP;
    if (!nvn_pool_range_fits(virtual_size, virtual_offset, size)) return NVN_MAP_VIRTUAL_BAD_VIRTUAL;
    pages = size / page;
    if (pages > UINT32_MAX) return NVN_MAP_VIRTUAL_BAD_VIRTUAL;
    range->virtual_base = virtual_base + virtual_offset;
    range->pages = (uint32_t)pages;
    return NVN_MAP_VIRTUAL_OK;
}

uint32_t nvn_pool_map_virtual_batch(uint32_t count, uint32_t done) {
    uint32_t left;
    if (done >= count) return 0;
    left = count - done;
    return left < NVN_POOL_MAP_VIRTUAL_BATCH ? left : NVN_POOL_MAP_VIRTUAL_BATCH;
}

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
const NvnZcullDeviceWords nvn_zcull_device_words = {
    NVN_ZCULL_WIDTH_ALIGN_PIXELS, NVN_ZCULL_HEIGHT_ALIGN_PIXELS,
    NVN_ZCULL_PIXEL_SQUARES_BY_ALIQUOTS, NVN_ZCULL_ALIQUOT_TOTAL,
    NVN_ZCULL_REGION_BYTE_MULTIPLIER, NVN_ZCULL_REGION_HEADER_SIZE,
    NVN_ZCULL_SUBREGION_HEADER_SIZE, NVN_ZCULL_SUBREGION_COUNT,
    NVN_ZCULL_SUBREGION_WIDTH_ALIGN_PIXELS, NVN_ZCULL_SUBREGION_HEIGHT_ALIGN_PIXELS,
    NVN_ZCULL_CTX_SIZE, NVN_ZCULL_CTX_ALIGN
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

void nvn_sampler_border(const NvnSampler* s, uint32_t out[4]) {
    if (!s || !out) return;
    for (unsigned i = 0; i < 4; i++) out[i] = s->border[i];
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
/* The texel-width divisor of a multisample format: 2 for 2 and 4 samples, 4 for 8 and 16. */
static uint8_t texture_sample_x(uint32_t samples) {
    return samples == 2 || samples == 4 ? 2u : samples == 8 || samples == 16 ? 4u : 1u;
}
/* The texel-height divisor of a multisample format: 2 for 4 and 8 samples, 4 for 16. */
static uint8_t texture_sample_y(uint32_t samples) {
    return samples == 4 || samples == 8 ? 2u : samples == 16 ? 4u : 1u;
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
    if (texture_format(b->format, &f)) return texture_alignment(b, dev, f);
    /* d83c4c0 does not check the format. Kind 0 (no format) gives the GOB size, a buffer uses the table bytes (0). */
    if (b->target != NVN_TEXTURE_TARGET_BUFFER) return NVN_TEXTURE_GOB_BYTES;
    uint32_t kind = dev->w1b18 & 255u;
    return kind != 0 && kind != NVN_TEXTURE_KIND_PITCH ? dev->wac : 0;
}

/* The kind byte of d83563c: kind table de7f0b1, chip 0x12b. One array for each sample column (1 or 2, 4, 8, other).
   Format codes above the table end give kind 0. */
static const uint8_t texture_kind_samples12[NVN_TEXTURE_FORMAT_MAX + 1] = {
    0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xcc, 0xcc, 0xcc, 0xfe, 0xfe, 0xfe, /* 00 */
    0xfe, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd, 0xcd, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
    0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xf7, 0xf7, /* 20 */
    0xf7, 0x2b, 0x08, 0x52, 0x87, 0x52, 0xcf, 0xcc, 0xcc, 0xfe, 0xfe, 0xfe, 0xfe, 0xcc, 0xcc, 0xcc, /* 30 */
    0xcc, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 40 */
    0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, /* 60 */
    0xcc, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xf7, 0xf7, 0xf7, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 70 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 80 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xcc, 0xcc, 0xcc, 0xcc, 0xfe, 0xfe, 0xfe, /* 90 */
    0xfe, /* a0 */
};
static const uint8_t texture_kind_samples4[NVN_TEXTURE_FORMAT_MAX + 1] = {
    0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xe0, 0xe0, 0xe0, 0xfe, 0xfe, 0xfe, /* 00 */
    0xfe, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xee, 0xee, 0xee, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
    0x00, 0x00, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xee, 0xee, 0xee, 0xee, 0xee, 0xf9, 0xf9, /* 20 */
    0xf9, 0x2b, 0x09, 0x53, 0x88, 0x53, 0xd0, 0xe0, 0xe0, 0xfe, 0xfe, 0xfe, 0xfe, 0xe0, 0xe0, 0xe0, /* 30 */
    0xe0, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 40 */
    0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xe0, 0xe0, /* 60 */
    0xe0, 0xee, 0xee, 0xee, 0xee, 0xee, 0xf9, 0xf9, 0xf9, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 70 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 80 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xe0, 0xe0, 0xe0, 0xe0, 0xfe, 0xfe, 0xfe, /* 90 */
    0xfe, /* a0 */
};
static const uint8_t texture_kind_samples8[NVN_TEXTURE_FORMAT_MAX + 1] = {
    0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xe5, 0xe5, 0xe5, 0xfe, 0xfe, 0xfe, /* 00 */
    0xfe, 0xe5, 0xe5, 0xe5, 0xe5, 0xe5, 0xf3, 0xf3, 0xf3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
    0x00, 0x00, 0xe5, 0xe5, 0xe5, 0xe5, 0xe5, 0xe5, 0xe5, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xfb, 0xfb, /* 20 */
    0xfb, 0x2b, 0x0a, 0x54, 0x89, 0x54, 0xd1, 0xe5, 0xe5, 0xfe, 0xfe, 0xfe, 0xfe, 0xe5, 0xe5, 0xe5, /* 30 */
    0xe5, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 40 */
    0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe5, 0xe5, 0xe5, /* 60 */
    0xe5, 0xf3, 0xf3, 0xf3, 0xf3, 0xf3, 0xfb, 0xfb, 0xfb, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 70 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 80 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xe5, 0xe5, 0xe5, 0xe5, 0xfe, 0xfe, 0xfe, /* 90 */
    0xfe, /* a0 */
};
static const uint8_t texture_kind_other[NVN_TEXTURE_FORMAT_MAX + 1] = {
    0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xdb, 0xdb, 0xdb, 0xfe, 0xfe, 0xfe, /* 00 */
    0xfe, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xe9, 0xe9, 0xe9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
    0x00, 0x00, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xe9, 0xe9, 0xe9, 0xe9, 0xe9, 0xf5, 0xf5, /* 20 */
    0xf5, 0x2b, 0x07, 0x51, 0x86, 0x51, 0xce, 0xdb, 0xdb, 0xfe, 0xfe, 0xfe, 0xfe, 0xdb, 0xdb, 0xdb, /* 30 */
    0xdb, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 40 */
    0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 50 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdb, 0xdb, 0xdb, /* 60 */
    0xdb, 0xe9, 0xe9, 0xe9, 0xe9, 0xe9, 0xf5, 0xf5, 0xf5, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 70 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 80 */
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xdb, 0xdb, 0xdb, 0xdb, 0xfe, 0xfe, 0xfe, /* 90 */
    0xfe, /* a0 */
};
static const uint8_t* const texture_kind_table[4] = {
    texture_kind_samples12, texture_kind_samples4, texture_kind_samples8, texture_kind_other
};
/* The sample column of the kind table: 1 or 2, 4, 8, other. */
static uint32_t texture_kind_column(uint32_t samples) {
    return samples == 1 || samples == 2 ? 0u : samples == 4 ? 1u : samples == 8 ? 2u : 3u;
}
/* The two families whose S 4 kind flags bit 12 raises by two. */
static int texture_kind_s4_extended(uint32_t format) {
    return (format >= 0x0a && format <= 0x0c) || (format >= 0x11 && format <= 0x15) ||
           (format >= 0x22 && format <= 0x28) || (format >= 0x3d && format <= 0x40) ||
           (format >= 0x6d && format <= 0x70) || (format >= 0x99 && format <= 0x9c) ||
           format == 0x37 || format == 0x38 ||
           (format >= 0x16 && format <= 0x18) || (format >= 0x29 && format <= 0x2d) ||
           (format >= 0x71 && format <= 0x75);
}
/* w3 of d83c620: compression is on and the format has the shape that the device compresses. */
static int texture_compression_wanted(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev,
                                      const TextureFormat* f) {
    if (!(b->flags & NVN_TEXTURE_FLAG_COMPRESS) || !dev->w1b1c) return 0;
    if (f->depth_stencil) return dev->w19fc != NVN_TEXTURE_COMPRESSION_DISABLED;
    return f->width <= 1 && f->bytes >= 4 && dev->w19f8 != NVN_TEXTURE_COMPRESSION_DISABLED;
}
static uint32_t texture_compressed_kind(const NvnTextureBuilder* b) {
    if (b->format > NVN_TEXTURE_FORMAT_MAX) return 0;
    uint32_t column = texture_kind_column(b->samples);
    uint32_t kind = texture_kind_table[column][b->format];
    if (b->format == 0x32 && (b->flags & NVN_TEXTURE_FLAG_KIND_MINUS_FIVE)) kind -= 5;
    if (column == 1u && (b->flags & NVN_TEXTURE_FLAG_KIND_S4_PLUS_TWO) && texture_kind_s4_extended(b->format))
        kind += 2;
    return kind;
}
/* w3 = 0 kinds: the pitch kind for color, the fixed depth kinds, and 0 for an invalid record. */
static uint32_t texture_uncompressed_kind(uint32_t format, const TextureFormat* f) {
    if (!f->depth_stencil) return NVN_TEXTURE_KIND_PITCH;
    switch (format) {
    case 0x31u: return 0x2au;
    case 0x32u: return 0x01u;
    case 0x33u: case 0x35u: return 0x46u;
    case 0x34u: return 0x7bu;
    case 0x36u: return 0xc3u;
    default: return 0;
    }
}
uint32_t nvn_texture_storage_class(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev) {
    if (!b || !dev) return 0;
    if (b->target == NVN_TEXTURE_TARGET_BUFFER || (b->flags & NVN_TEXTURE_LINEAR_FLAGS))
        return NVN_TEXTURE_STORAGE_CLASS_BASE | (dev->w1b18 & 0xffu);
    TextureFormat f;
    if (!texture_format(b->format, &f)) return NVN_TEXTURE_STORAGE_CLASS_BASE;
    uint32_t kind = texture_compression_wanted(b, dev, &f) ? texture_compressed_kind(b) :
                                                             texture_uncompressed_kind(b->format, &f);
    return NVN_TEXTURE_STORAGE_CLASS_BASE | kind;
}

static uint64_t texture_zcull_up(uint64_t n, uint64_t k) { return (n + k - 1) / k; }
int nvn_texture_zcull_size(const NvnTextureBuilder* b, const NvnZcullDeviceWords* zdev, uint32_t* size) {
    if (size) *size = 0;
    if (!b || !zdev || !size) return NVN_TEXTURE_ZCULL_REFUSAL;
    TextureFormat f = {0, 0, 0, 0};
    if (!texture_format(b->format, &f) ||
        (!(f.depth_stencil & 1u) && !((f.depth_stencil & 2u) && (b->flags & NVN_TEXTURE_FLAG_ZCULL_STENCIL))))
        return 0;
    if (b->flags & NVN_TEXTURE_FLAG_ZCULL_INFO) return NVN_TEXTURE_ZCULL_REFUSAL;
    uint64_t aw = zdev->width_align_pixels, ah = zdev->height_align_pixels;
    uint64_t u = zdev->pixel_squares_by_aliquots;
    if (!aw || !ah || !u) return NVN_TEXTURE_ZCULL_REFUSAL;
    uint64_t width = texture_zcull_up(b->width, aw) * aw;
    uint64_t height = texture_zcull_up(b->height, ah) * ah;
    uint64_t depth = b->depth ? b->depth : 1u;
    uint64_t blocks = texture_zcull_up(width * height, u) * depth;
    uint64_t result = (uint64_t)zdev->region_byte_multiplier * blocks + zdev->region_header_size +
                      zdev->subregion_header_size;
    *size = (uint32_t)result;
    return 1;
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
typedef struct { uint64_t size; uint32_t gobs_x, rows, depth; uint8_t block_height_log2, depth_log2; } TextureLevelParts;
/* One level of a block linear texture with the tile exponents x, y and z: the GOBs across (rounded to the tile),
   the block rows, the depth, the trimmed height and depth exponents and the bytes of the level. */
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
        .depth = d,
        .block_height_log2 = (uint8_t)ey,
        .depth_log2 = (uint8_t)ez,
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
    l->sample_x = texture_sample_x(b->samples);
    l->sample_y = texture_sample_y(b->samples);
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

int nvn_texture_sparse_layout(const NvnTextureBuilder* b, uint32_t out[4]) {
    if (!b || !out) return 0;
    TextureFormat f;
    /* Format 0 has table bytes 0 and the library writes zeros (selftest of 2026-11-10). Other unknown formats stay refused. */
    if (b->format != 0 && !texture_format(b->format, &f)) return 0;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (b->format == 0) return 1;
    if (b->target > NVN_TEXTURE_TARGET_BUFFER || b->target == NVN_TEXTURE_TARGET_1D ||
        b->target == NVN_TEXTURE_TARGET_1D_ARRAY || b->target == NVN_TEXTURE_TARGET_BUFFER || f.bytes == 12 ||
        (b->flags & NVN_TEXTURE_SPARSE_TILE_MASK) != NVN_TEXTURE_FLAG_SPARSE)
        return 1;
    uint32_t x, y, z;
    if (b->target == NVN_TEXTURE_TARGET_3D) {
        x = f.bytes == 4 ? 1u : f.bytes == 8 || f.bytes == 16 ? 2u : 0u;
        y = f.bytes == 8 || f.bytes == 16 ? 1u : 2u;
        z = f.bytes == 4 || f.bytes == 8 || f.bytes == 16 ? 4u : 5u;
    } else {
        x = f.bytes == 2 || f.bytes == 4 ? 3u : f.bytes == 8 || f.bytes == 16 ? 4u : 2u;
        y = f.bytes == 2 || f.bytes == 4 ? 4u : f.bytes == 8 || f.bytes == 16 ? 3u : 5u;
        z = 0;
    }
    uint32_t tile_w = ((NVN_TEXTURE_GOB_WIDTH / f.bytes) << x) / texture_sample_x(b->samples);
    uint32_t tile_h = (NVN_TEXTURE_GOB_HEIGHT << y) / texture_sample_y(b->samples);
    uint32_t tile_d = 1u << z;
    /* A block width of 2 or more scales both tile sides by the block shape (d822538). */
    if (f.width >= 2) {
        tile_w *= f.width;
        tile_h *= f.height;
    }
    uint32_t w = b->width, h = b->height, d = b->depth;
    out[0] = b->levels;
    for (uint32_t i = 0; i < b->levels; i++) {
        if ((int32_t)w < (int32_t)tile_w || (int32_t)d < (int32_t)tile_d || (int32_t)h < (int32_t)tile_h) {
            out[0] = i;
            break;
        }
        w >>= 1;
        h >>= 1;
        if (b->target == NVN_TEXTURE_TARGET_3D) d >>= 1;
    }
    if (out[0]) {
        out[1] = tile_w;
        out[2] = tile_h;
        out[3] = tile_d;
    }
    return 1;
}

uint16_t nvn_texture_packaged_tile(uint32_t layout) { return (uint16_t)(((layout & 0xfffu) << 4) | 1u); }

uint32_t nvn_texture_packaged_layout(uint16_t tile, uint32_t* layout) {
    uint32_t low = tile & 0xFu;
    if (!low) {
        if (layout) *layout = 0;
        return 0;
    }
    if (layout) *layout = (tile >> 4) & 0xFFFu;
    return low;
}

uint32_t nvn_texture_raw_storage_class(uint32_t flags, uint32_t format) {
    if (flags & NVN_TEXTURE_LINEAR_FLAGS) return 1;
    if (format > NVN_TEXTURE_RAW_STORAGE_FORMAT_LAST) return 2;
    if (!format) return 0;
    if (format <= 0x30u) return 2;
    switch (format) {
    case 0x31u: case 0x32u: return 3;
    case 0x33u: case 0x35u: return 4;
    case 0x34u: return 2;
    default: return 5;
    }
}

uint32_t nvn_texture_storage_size(const NvnTexture* t) { return t ? (uint32_t)t->layout.size : 0; }

int nvn_texture_equal(const NvnTexture* a, const NvnTexture* b) {
    if (!a || !b) return 0;
    const NvnTextureBuilder* x = &a->builder;
    const NvnTextureBuilder* y = &b->builder;
    if (x->device != y->device || x->pool != y->pool || x->offset != y->offset) return 0;
    if (x->flags != y->flags || x->target != y->target || x->width != y->width || x->height != y->height ||
        x->depth != y->depth || x->levels != y->levels || x->format != y->format || x->samples != y->samples)
        return 0;
    for (unsigned i = 0; i < 4; i++)
        if (x->swizzle[i] != y->swizzle[i]) return 0;
    if (x->depth_stencil_mode != y->depth_stencil_mode) return 0;
    const NvnTextureLayout* p = &a->layout;
    const NvnTextureLayout* q = &b->layout;
    if ((p->depth_stencil & 3u) != (q->depth_stencil & 3u)) return 0;
    if (p->layer_stride != q->layer_stride || p->linear_stride != q->linear_stride ||
        p->bytes_per_block != q->bytes_per_block || p->log2_bytes != q->log2_bytes ||
        p->sample_x != q->sample_x || p->sample_y != q->sample_y || p->samples != q->samples ||
        p->block_width != q->block_width || p->block_height != q->block_height || p->levels != q->levels ||
        p->dim != q->dim || p->kind != q->kind || p->size != q->size || p->width != q->width ||
        p->height != q->height || p->depth != q->depth || p->tile_x != q->tile_x || p->tile_y != q->tile_y ||
        p->tile_z != q->tile_z)
        return 0;
    return a->address == b->address;
}

int nvn_texture_view_equal(const NvnTextureView* a, const NvnTextureView* b) {
    if (!a || !b) return 0;
    if ((a->flags & NVN_VIEW_COMPARE_MASK) != (b->flags & NVN_VIEW_COMPARE_MASK)) return 0;
    if ((a->flags & NVN_VIEW_LEVELS) && (a->base_level != b->base_level || a->level_count != b->level_count))
        return 0;
    if ((a->flags & NVN_VIEW_LAYERS) && (a->base_layer != b->base_layer || a->layer_count != b->layer_count))
        return 0;
    if ((a->flags & NVN_VIEW_FORMAT) && a->format != b->format) return 0;
    if (a->flags & NVN_VIEW_SWIZZLE)
        for (unsigned i = 0; i < 4; i++)
            if (a->swizzle[i] != b->swizzle[i]) return 0;
    if ((a->flags & NVN_VIEW_DEPTH_STENCIL_MODE) && a->depth_stencil_mode != b->depth_stencil_mode) return 0;
    if ((a->flags & NVN_VIEW_TARGET) && a->target != b->target) return 0;
    return 1;
}

/* The offset of byte (x, y) of slice z of a level (gpu.c's bl_offset with bh and bd). */
static uint64_t texture_level_offset(const NvnTextureLevel* lv, uint32_t x, uint32_t y, uint32_t z) {
    uint64_t bh = lv->block_height_log2, bd = lv->depth_log2, gy = y >> GOB_SIZE_Y_SHIFT;
    uint64_t block = (uint64_t)NVN_TEXTURE_GOB_BYTES << (bh + bd);
    uint64_t block_rows = (lv->rows + (NVN_TEXTURE_GOB_HEIGHT << bh) - 1) >> (GOB_SIZE_Y_SHIFT + bh);
    uint64_t block_row = (uint64_t)(z >> bd) * block_rows + (gy >> bh);
    return ((uint64_t)(x >> GOB_SIZE_X_SHIFT) << (GOB_SIZE_SHIFT + bh + bd)) + gob_x_offset(x) +
           block_row * lv->gobs_x * block +
           ((((z & ((1u << bd) - 1)) << bh) + (gy & ((1u << bh) - 1))) << GOB_SIZE_SHIFT) + gob_y_offset(y);
}
int nvn_texture_level(const NvnTexture* t, uint32_t level, NvnTextureLevel* out) {
    if (!t || !out) return 0;
    const NvnTextureLayout* l = &t->layout;
    if ((t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) || t->builder.target == NVN_TEXTURE_TARGET_BUFFER ||
        l->sample_x != 1 || l->sample_y != 1 || level >= l->levels) return 0;
    uint64_t offset = 0;
    for (uint32_t i = 0; i < level; i++)
        offset += texture_level_parts(l, i, l->tile_x, l->tile_y, l->tile_z).size;
    TextureLevelParts parts = texture_level_parts(l, level, l->tile_x, l->tile_y, l->tile_z);
    *out = (NvnTextureLevel){.offset = offset, .size = parts.size, .gobs_x = parts.gobs_x,
                             .rows = parts.rows, .depth = parts.depth, .block_height_log2 = parts.block_height_log2,
                             .depth_log2 = parts.depth_log2};
    return 1;
}
int nvn_texture_view_offset(const NvnTexture* t, const NvnTextureView* v, uint64_t* out) {
    if (!t || !out) return 0;
    uint32_t base_level = v && (v->flags & NVN_VIEW_LEVELS) ? v->base_level : 0;
    uint32_t base_layer = v && (v->flags & NVN_VIEW_LAYERS) ? v->base_layer : 0;
    if ((t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) || t->builder.target == NVN_TEXTURE_TARGET_BUFFER) {
        *out = 0;
        return 1;
    }
    if (base_level >= 32) return 0;
    const NvnTextureLayout* l = &t->layout;
    uint64_t offset = 0;
    for (uint32_t i = 0; i < base_level; i++)
        offset += texture_level_parts(l, i, l->tile_x, l->tile_y, l->tile_z).size;
    if (base_layer) {
        if (l->dim == 3) {
            TextureLevelParts p = texture_level_parts(l, base_level, l->tile_x, l->tile_y, l->tile_z);
            uint32_t ty = p.block_height_log2, tz = p.depth_log2;
            uint32_t rows_b = ceil_u32(p.rows, NVN_TEXTURE_GOB_HEIGHT << ty);
            uint32_t term = ((p.gobs_x * rows_b * (base_layer >> tz)) << (ty + tz)) +
                            ((base_layer & ((1u << tz) - 1u)) << ty);
            offset += (uint64_t)term << 9;
        } else {
            offset += l->layer_stride * base_layer;
        }
    }
    *out = offset;
    return 1;
}
int nvn_texture_write_rows(uint8_t* level_base, const NvnTextureLevel* lv, uint32_t x_start, uint32_t x_end,
                           uint32_t row_start, uint32_t row_end, const uint8_t* src, uint64_t src_stride,
                           uint32_t z) {
    if (!level_base || !lv || !src) return 0;
    /* A level without a 3D depth holds one slice. */
    uint32_t depth = lv->depth ? lv->depth : 1;
    if (z >= depth) return 0;
    /* The level holds whole blocks: a block is 2^bh GOBs, that is 8 << bh rows, high. */
    uint64_t block_rows = (uint64_t)GOB_SIZE_Y << lv->block_height_log2;
    uint64_t row_limit = (lv->rows + block_rows - 1) / block_rows * block_rows;
    if (x_end > (uint64_t)lv->gobs_x * GOB_SIZE_X || row_end > row_limit) return 0;
    if (x_start >= x_end || row_start >= row_end) return 1;
    /* Validate the last byte before the first write: the offsets grow with x and with y. */
    if (texture_level_offset(lv, x_end - 1, row_end - 1, z) >= lv->size) return 0;
    for (uint32_t r = row_start; r < row_end; r++) {
        const uint8_t* from = src + (uint64_t)(r - row_start) * src_stride;
        for (uint32_t x = x_start; x < x_end; x++)
            level_base[texture_level_offset(lv, x, r, z)] = from[x - x_start];
    }
    return 1;
}
int nvn_texture_read_rows(const uint8_t* level_base, const NvnTextureLevel* lv, uint32_t x_start, uint32_t x_end,
                          uint32_t row_start, uint32_t row_end, uint8_t* dst, uint64_t dst_stride, uint32_t z) {
    if (!level_base || !lv || !dst) return 0;
    uint32_t depth = lv->depth ? lv->depth : 1;
    if (z >= depth) return 0;
    uint64_t block_rows = (uint64_t)GOB_SIZE_Y << lv->block_height_log2;
    uint64_t row_limit = (lv->rows + block_rows - 1) / block_rows * block_rows;
    if (x_end > (uint64_t)lv->gobs_x * GOB_SIZE_X || row_end > row_limit) return 0;
    if (x_start >= x_end || row_start >= row_end) return 1;
    if (texture_level_offset(lv, x_end - 1, row_end - 1, z) >= lv->size) return 0;
    for (uint32_t r = row_start; r < row_end; r++) {
        uint8_t* to = dst + (uint64_t)(r - row_start) * dst_stride;
        for (uint32_t x = x_start; x < x_end; x++)
            to[x - x_start] = level_base[texture_level_offset(lv, x, r, z)];
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

const char* nvn_present_refusal(int queue_ok, int window_ok, int32_t index, uint32_t texture_count,
                                const uint8_t* states) {
    if (!queue_ok) return "unknown queue";
    if (!window_ok) return "unknown window";
    if (index < 0 || (uint32_t)index >= texture_count) return "index outside the window textures";
    if (states[index] != NVN_IMAGE_ACQUIRED) return "window image is not acquired";
    return NULL;
}

uint32_t nvn_event_signal_value(uint32_t mode, uint32_t old, uint32_t value) {
    return mode == NVN_EVENT_SIGNAL_ADD ? old + value : value;
}

int nvn_event_wait_met(uint32_t mode, uint32_t word, uint32_t value) {
    if (mode == NVN_EVENT_WAIT_GEQUAL_WRAP) return (int32_t)(word - value) >= 0;
    return word == value;
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

NvnProgramCountCheck nvn_program_count_check(int32_t count, uint64_t entries) {
    if (count < 0) return NVN_PROGRAM_COUNT_NEGATIVE;
    if ((uint32_t)count > NVN_PROGRAM_STAGES) return NVN_PROGRAM_COUNT_ABOVE;
    if (count > 0 && !entries) return NVN_PROGRAM_COUNT_ENTRIES_NULL;
    return NVN_PROGRAM_COUNT_OK;
}

const char* nvn_program_entry_refusal(int has_control, uint64_t entry_word, uint64_t header) {
    if (has_control && !entry_word) return NVN_PROGRAM_REFUSAL_WORD_ZERO;
    if (!header) return NVN_PROGRAM_REFUSAL_HEADER_NULL;
    if (header & (NVN_PROGRAM_HEADER_ALIGNMENT - 1u)) return NVN_PROGRAM_REFUSAL_HEADER_UNALIGNED;
    return NULL;
}

int nvn_program_stage_add(uint32_t* stage_mask, uint32_t stage_index) {
    const uint32_t bit = 1u << stage_index;
    if (*stage_mask & bit) return 0;
    *stage_mask |= bit;
    return 1;
}

int nvn_program_unsupported_stage(uint32_t stage_mask) {
    uint32_t unsupported = stage_mask & ~NVN_PROGRAM_STAGE_SUPPORTED_MASK;
    for (uint32_t stage = NVN_PROGRAM_STAGE_FRAGMENT + 1u; stage < NVN_PROGRAM_STAGES; stage++)
        if (unsupported & (1u << stage)) return (int)stage;
    return -1;
}

uint32_t nvn_bind_program_words(uint32_t program_mask, const uint32_t* program_words, uint32_t program_word_count,
                                uint32_t* out_words) {
    if (program_mask & (1u << NVN_PROGRAM_STAGE_COMPUTE)) {
        memcpy(out_words, program_words, NVN_PROGRAM_COMPUTE_WORDS * sizeof out_words[0]);
        return NVN_PROGRAM_COMPUTE_WORDS;
    }
    memcpy(out_words, program_words, NVN_PROGRAM_WORDS_MAX * sizeof out_words[0]);
    return program_word_count;
}

/* The Maxwell header word (analyst-prog-builder-1.md, section 1): mode in bits 29..31, count or value in bits 16..28,
   method in bits 0..11. */
#define NVN_PROGRAM_MODE_INCREASING 0x20000000u    /* mode 1: count data words follow */
#define NVN_PROGRAM_MODE_NON_INCREASING 0x60000000u /* mode 3: every data word goes to the same method */
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
static void program_push_macro(uint32_t* words, uint32_t* at, uint32_t slot, uint32_t program_id,
                               uint32_t code_offset, uint32_t group, uint32_t cb_address) {
    program_push_word(words, at, NVN_PROGRAM_MODE_INCREASE_ONCE | (NVN_PROGRAM_MACRO_COUNT << NVN_PROGRAM_COUNT_SHIFT) |
                                   NVN_PROGRAM_METHOD_PROGRAM);
    program_push_word(words, at, slot);
    program_push_word(words, at, program_id);
    program_push_word(words, at, code_offset);
    program_push_word(words, at, group);
    program_push_word(words, at, cb_address);
}

static int program_build_vertex(const NvnProgramStageIn* in, uint32_t program_id, uint64_t region_base,
                                uint32_t* words, uint32_t* at) {
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_VERTEX, 0)) return 0;
    program_push_macro(words, at, NVN_PROGRAM_SLOT_VERTEX, program_id, in->code_offset, NVN_PROGRAM_GROUP_VERTEX,
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
                                  const NvnProgramDeviceIn* dev, uint32_t program_id, uint64_t region_base,
                                  uint32_t* words, uint32_t* at) {
    uint32_t d8[2];
    program_build_d8(frag, dev, d8);
    uint32_t value_3d0 = frag->g158 == 0 ? 0u : (frag->g160 == 0 ? 1u : 0u);
    int branch_2004 = frag->g158 != 0 && frag->g160 == 0;
    uint32_t value_084 = (dev->d1a3c != 0 || frag->g148 != 0) ? 1u : 0u;
    uint32_t value_65b = (uint32_t)frag->g178 | ((uint32_t)frag->g180 << NVN_PROGRAM_FRAGMENT_65B_SHIFT);
    uint32_t value_1d5 =
        frag->g188 == 0 ? NVN_PROGRAM_FRAGMENT_1D5_OPAQUE : NVN_PROGRAM_FRAGMENT_1D5_BLEND;
    if (!program_push_immediate(words, at, NVN_PROGRAM_METHOD_FRAGMENT, NVN_PROGRAM_FRAGMENT_CONTROL)) return 0;
    program_push_macro(words, at, NVN_PROGRAM_SLOT_FRAGMENT, program_id, in->code_offset,
                       NVN_PROGRAM_GROUP_FRAGMENT,
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
    program_push_word(words, at, in->block_x);
    program_push_word(words, at, in->block_y);
    program_push_word(words, at, in->block_z);
    program_push_word(words, at, in->shared_bytes);
    program_push_word(words, at, in->local_pos);
    program_push_word(words, at, in->local_neg);
    program_push_word(words, at, in->local_crs);
    program_push_word(words, at, in->barriers);
    program_push_word(words, at, in->h6f8);
    program_push_word(words, at, in->h778_low);
    program_push_word(words, at, in->h778_high);
    program_push_word(words, at, in->flags);
    program_push_word(words, at, 0u);
}

int nvn_program_build_words(const NvnProgramComputeIn* compute, const NvnProgramStageIn* vertex,
                            const NvnProgramStageIn* fragment, const NvnProgramFragmentIn* frag,
                            const NvnProgramDeviceIn* dev, uint32_t program_id, uint64_t region_base, uint32_t* words,
                            uint32_t* count) {
    if (!words || !count) return 0;
    if ((vertex || fragment) && !program_id) return 0; /* bind_program skips an id equal to the slot's initial 0 */
    if (compute && (vertex || fragment)) return 0;
    if (fragment && (!frag || !dev)) return 0;
    uint32_t at = 0;
    if (compute) program_build_compute(compute, words, &at);
    if (vertex && !program_build_vertex(vertex, program_id, region_base, words, &at)) return 0;
    if (fragment && !program_build_fragment(fragment, frag, dev, program_id, region_base, words, &at)) return 0;
    *count = at;
    return 1;
}

/* The transform feedback blob after the vertex header: a signed id, a start and a count per component row, then the
   stride, the buffer and the comps per stream and the enable byte. */
#define NVN_PROGRAM_XFB_ID_OFFSET 0x000u
#define NVN_PROGRAM_XFB_START_OFFSET 0x200u
#define NVN_PROGRAM_XFB_COUNT_OFFSET 0x400u
#define NVN_PROGRAM_XFB_STRIDE_OFFSET 0x600u
#define NVN_PROGRAM_XFB_BUFFER_OFFSET 0x608u
#define NVN_PROGRAM_XFB_COMPS_OFFSET 0x60Cu
#define NVN_PROGRAM_XFB_ENABLE_OFFSET 0x610u
#define NVN_PROGRAM_XFB_ROW_BYTES 0x80u
#define NVN_PROGRAM_XFB_COMPS_MAX 0x80u
#define NVN_PROGRAM_XFB_ID_COUNT 64
#define NVN_PROGRAM_XFB_ID_POINT_FIRST (-6)
#define NVN_PROGRAM_XFB_ID_POINT_END (-3)
#define NVN_PROGRAM_XFB_TABLE_FIRST 32u
#define NVN_PROGRAM_XFB_TABLE_BASE 0x20u
#define NVN_PROGRAM_XFB_TABLE_STEP 4u
#define NVN_PROGRAM_XFB_LAYOUT_BYTES (NVN_PROGRAM_XFB_LAYOUT_WORDS_MAX * 4u)
#define NVN_PROGRAM_XFB_METHOD_FIRST 0x1C0u
#define NVN_PROGRAM_XFB_METHOD_STREAM_STEP 4u
#define NVN_PROGRAM_XFB_METHOD_LAYOUT_FIRST 0xA00u
#define NVN_PROGRAM_XFB_METHOD_LAYOUT_STEP 0x20u
#define NVN_PROGRAM_XFB_WORD_HEADER 0x20030000u
#define NVN_PROGRAM_XFB_WORD_LAYOUT_HEADER 0x20000000u
#define NVN_PROGRAM_XFB_POINT_BYTE 0xFFu

#define NVN_PROGRAM_XFB_REASON_COMPS "transform feedback: comps is above 0x80"
#define NVN_PROGRAM_XFB_REASON_ID "transform feedback: an id is outside the table"
#define NVN_PROGRAM_XFB_REASON_LAYOUT "transform feedback: the layout is above 32 words"
#define NVN_PROGRAM_XFB_REASON_STRIDE "transform feedback: the layout is above the stride allocation"
#define NVN_PROGRAM_XFB_REASON_SPACE "transform feedback: the word array is too small"

/* The low byte of the u16 table entries 0 to 31 (sdk 0xde87370). Entries 32 to 63 follow the linear rule below. */
static const uint8_t program_xfb_table[NVN_PROGRAM_XFB_TABLE_FIRST] = {
    0x1c, 0xa0, 0xa4, 0xa8, 0xac, 0xba, 0x1b, 0xc0, 0xc4, 0xc8, 0xcc, 0xd0, 0xd4, 0xd8, 0xdc, 0xe0,
    0xe4, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xbf, 0x1a, 0x19, 0xe8, 0xff, 0x18, 0xff,
};

static uint8_t program_xfb_table_byte(uint32_t id) {
    if (id < NVN_PROGRAM_XFB_TABLE_FIRST) return program_xfb_table[id];
    return (uint8_t)(NVN_PROGRAM_XFB_TABLE_BASE + NVN_PROGRAM_XFB_TABLE_STEP * (id - NVN_PROGRAM_XFB_TABLE_FIRST));
}

static int program_xfb_id_is_point(int32_t id) {
    return id >= NVN_PROGRAM_XFB_ID_POINT_FIRST && id <= NVN_PROGRAM_XFB_ID_POINT_END;
}

/* Packs the layout of stream i and pads it to a word multiple. Returns 1 with *bytes; 0 with *reason on a refusal. */
static int program_xfb_layout_bytes(const uint8_t* blob, uint32_t stream, uint32_t comps,
                                    uint8_t layout[NVN_PROGRAM_XFB_LAYOUT_BYTES], uint32_t* bytes,
                                    const char** reason) {
    uint32_t at = 0;
    for (uint32_t j = 0; j < comps; j++) {
        uint32_t s = blob[NVN_PROGRAM_XFB_START_OFFSET + NVN_PROGRAM_XFB_ROW_BYTES * stream + j];
        uint32_t e = (s + blob[NVN_PROGRAM_XFB_COUNT_OFFSET + NVN_PROGRAM_XFB_ROW_BYTES * stream + j]) & 0xFFu;
        if (s >= e) continue;
        int32_t id = (int8_t)blob[NVN_PROGRAM_XFB_ID_OFFSET + NVN_PROGRAM_XFB_ROW_BYTES * stream + j];
        if (at + (e - s) > NVN_PROGRAM_XFB_LAYOUT_BYTES) {
            *reason = NVN_PROGRAM_XFB_REASON_LAYOUT;
            return 0;
        }
        if (!program_xfb_id_is_point(id) && (id < 0 || id >= NVN_PROGRAM_XFB_ID_COUNT)) {
            *reason = NVN_PROGRAM_XFB_REASON_ID;
            return 0;
        }
        for (uint32_t k = s; k < e; k++)
            layout[at++] = program_xfb_id_is_point(id) ? NVN_PROGRAM_XFB_POINT_BYTE
                                                       : (uint8_t)(program_xfb_table_byte((uint32_t)id) + k);
    }
    while (at % 4u) layout[at++] = 0;
    *bytes = at;
    return 1;
}

int nvn_program_xfb_words(const uint8_t* blob, uint32_t* words, uint32_t max, uint32_t* count,
                          const char** reason) {
    if (!blob || !words || !count || !reason) return 0;
    *reason = NULL;
    if (!blob[NVN_PROGRAM_XFB_ENABLE_OFFSET]) {
        *count = 0;
        return 1;
    }
    uint32_t out[NVN_PROGRAM_XFB_WORDS_MAX];
    uint32_t at = 0;
    for (uint32_t i = 0; i < NVN_PROGRAM_XFB_STREAMS; i++) {
        uint32_t comps = blob[NVN_PROGRAM_XFB_COMPS_OFFSET + i];
        if (comps > NVN_PROGRAM_XFB_COMPS_MAX) {
            *reason = NVN_PROGRAM_XFB_REASON_COMPS;
            return 0;
        }
        uint32_t stride = (uint32_t)blob[NVN_PROGRAM_XFB_STRIDE_OFFSET + 2u * i] |
                          ((uint32_t)blob[NVN_PROGRAM_XFB_STRIDE_OFFSET + 2u * i + 1u] << 8);
        out[at++] = NVN_PROGRAM_XFB_WORD_HEADER | (NVN_PROGRAM_XFB_METHOD_FIRST + NVN_PROGRAM_XFB_METHOD_STREAM_STEP * i);
        out[at++] = blob[NVN_PROGRAM_XFB_BUFFER_OFFSET + i];
        out[at++] = stride >> 2;
        out[at++] = stride;
        if (!stride) continue;
        uint8_t layout[NVN_PROGRAM_XFB_LAYOUT_BYTES];
        uint32_t bytes = 0;
        if (!program_xfb_layout_bytes(blob, i, comps, layout, &bytes, reason)) return 0;
        uint32_t nwords = bytes / 4u;
        if (nwords > ((stride + 0xCu) >> 4)) {
            *reason = NVN_PROGRAM_XFB_REASON_STRIDE;
            return 0;
        }
        /* The library writes a layout header with count 0 when the layout is empty, which is not a valid word: skip it. */
        if (!nwords) continue;
        out[at++] = NVN_PROGRAM_XFB_WORD_LAYOUT_HEADER | (nwords << NVN_PROGRAM_COUNT_SHIFT) |
                    (NVN_PROGRAM_XFB_METHOD_LAYOUT_FIRST + NVN_PROGRAM_XFB_METHOD_LAYOUT_STEP * i);
        for (uint32_t w = 0; w < nwords; w++)
            out[at++] = (uint32_t)layout[4u * w] | ((uint32_t)layout[4u * w + 1u] << 8) |
                        ((uint32_t)layout[4u * w + 2u] << 16) | ((uint32_t)layout[4u * w + 3u] << 24);
    }
    if (at > max) {
        *reason = NVN_PROGRAM_XFB_REASON_SPACE;
        return 0;
    }
    memcpy(words, out, (size_t)at * sizeof out[0]);
    *count = at;
    return 1;
}

/* The compute header offsets of the library (d8363c0, d837ae8, d836f10, d8543a0). */
#define NVN_PROGRAM_COMPUTE_OFFSET_LAYOUT     0x8u
#define NVN_PROGRAM_COMPUTE_OFFSET_CODE       0x708u
#define NVN_PROGRAM_COMPUTE_OFFSET_CB         0x700u
#define NVN_PROGRAM_COMPUTE_OFFSET_H6FC       0x6FCu
#define NVN_PROGRAM_COMPUTE_OFFSET_H710       0x710u
#define NVN_PROGRAM_COMPUTE_OFFSET_H70C       0x70Cu
#define NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_X    0x748u
#define NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_Y    0x74Cu
#define NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_Z    0x750u
#define NVN_PROGRAM_COMPUTE_OFFSET_SHARED     0x754u
#define NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_POS  0x758u
#define NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_NEG  0x75Cu
#define NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_CRS  0x760u
#define NVN_PROGRAM_COMPUTE_OFFSET_BARRIERS   0x764u
#define NVN_PROGRAM_COMPUTE_OFFSET_H6F8       0x6F8u
#define NVN_PROGRAM_COMPUTE_OFFSET_H778       0x778u /* word 14 is the low u32, word 15 the high u32 */
#define NVN_PROGRAM_COMPUTE_OFFSET_FLAG_0     0x830u
#define NVN_PROGRAM_COMPUTE_OFFSET_FLAG_1     0x831u
#define NVN_PROGRAM_COMPUTE_OFFSET_FLAG_2     0x832u
#define NVN_PROGRAM_COMPUTE_LAYOUT_FLAGS      13u    /* the flag bytes exist at layout 13 and above */
#define NVN_PROGRAM_COMPUTE_HEADER_BYTES_SMALL 0x780u
#define NVN_PROGRAM_COMPUTE_HEADER_BYTES_FLAGS 0x833u

static uint32_t program_compute_read_u32(const uint8_t* at) {
    uint32_t word;
    memcpy(&word, at, sizeof word);
    return word;
}

uint32_t nvn_program_compute_header_bytes(uint32_t layout) {
    if (layout >= NVN_PROGRAM_LAYOUT_FIRST && layout < NVN_PROGRAM_COMPUTE_LAYOUT_FLAGS)
        return NVN_PROGRAM_COMPUTE_HEADER_BYTES_SMALL;
    if (layout >= NVN_PROGRAM_COMPUTE_LAYOUT_FLAGS && layout <= NVN_PROGRAM_LAYOUT_LAST)
        return NVN_PROGRAM_COMPUTE_HEADER_BYTES_FLAGS;
    return 0;
}

int nvn_program_compute_in_from_header(const uint8_t* header, uint32_t bytes, uint32_t entry_word,
                                       NvnProgramComputeIn* out) {
    if (!header || !out) return 0;
    if (bytes < NVN_PROGRAM_COMPUTE_OFFSET_LAYOUT + sizeof(uint32_t)) return 0;
    const uint32_t layout = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_LAYOUT);
    const uint32_t size = nvn_program_compute_header_bytes(layout);
    if (!size || bytes < size) return 0;
    uint64_t words_14_15;
    memcpy(&words_14_15, header + NVN_PROGRAM_COMPUTE_OFFSET_H778, sizeof words_14_15);
    NvnProgramComputeIn compute;
    compute.code_offset = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_CODE) + entry_word;
    compute.cb_offset = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_CB) + entry_word;
    compute.h6fc = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_H6FC);
    compute.h710 = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_H710);
    compute.h70c = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_H70C);
    compute.block_x = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_X);
    compute.block_y = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_Y);
    compute.block_z = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_BLOCK_Z);
    compute.shared_bytes = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_SHARED);
    compute.local_pos = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_POS);
    compute.local_neg = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_NEG);
    compute.local_crs = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_LOCAL_CRS);
    compute.barriers = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_BARRIERS);
    compute.h6f8 = program_compute_read_u32(header + NVN_PROGRAM_COMPUTE_OFFSET_H6F8);
    compute.h778_low = (uint32_t)words_14_15;
    compute.h778_high = (uint32_t)(words_14_15 >> 32);
    compute.flags = 0;
    if (layout >= NVN_PROGRAM_COMPUTE_LAYOUT_FLAGS) {
        if (header[NVN_PROGRAM_COMPUTE_OFFSET_FLAG_0] & 1u) compute.flags |= NVN_PROGRAM_COMPUTE_FLAG_0;
        if (header[NVN_PROGRAM_COMPUTE_OFFSET_FLAG_1] & 1u) compute.flags |= NVN_PROGRAM_COMPUTE_FLAG_1;
        if (header[NVN_PROGRAM_COMPUTE_OFFSET_FLAG_2] & 1u) compute.flags |= NVN_PROGRAM_COMPUTE_FLAG_2;
    }
    *out = compute;
    return 1;
}

int nvn_program_compute_from_words(const uint32_t* words, uint32_t count, NvnProgramComputeIn* out) {
    if (!words || !out || count != NVN_PROGRAM_COMPUTE_WORDS) return 0;
    out->code_offset = words[0];
    out->cb_offset = words[1];
    out->h6fc = words[2];
    out->h710 = words[3];
    out->h70c = words[4];
    out->block_x = words[5];
    out->block_y = words[6];
    out->block_z = words[7];
    out->shared_bytes = words[8];
    out->local_pos = words[9];
    out->local_neg = words[10];
    out->local_crs = words[11];
    out->barriers = words[12];
    out->h6f8 = words[13];
    out->h778_low = words[14];
    out->h778_high = words[15];
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
#define NVN_WORD_SUBCHANNEL_SHIFT 13u
#define NVN_WORD_SUBCHANNEL_MASK 7u

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
    uint32_t subchannel = (word >> NVN_WORD_SUBCHANNEL_SHIFT) & NVN_WORD_SUBCHANNEL_MASK;
    if (mode == NVN_WORD_MODE_INLINE) {
        s->subchannel = subchannel;
        *method = header_method;
        *value = count;
        *last = 1;
        return 1;
    }
    if (mode != NVN_WORD_MODE_INCREASING && mode != NVN_WORD_MODE_NON_INCREASING &&
        mode != NVN_WORD_MODE_INCREASE_ONCE)
        return -1;
    if (!count) return -1;
    s->mode = mode;
    s->method = header_method;
    s->subchannel = subchannel;
    s->remaining = count;
    return 0;
}

int nvn_word_stream_complete(const NvnWordStream* s) { return s && s->remaining == 0; }

/* The words of nvnCommandBufferBarrier (sdk d856590), in order (nvn-design.md, decision 6, item "Barrier"). */
#define NVN_BARRIER_WORD_FIRST        0x910E0002u
#define NVN_BARRIER_WORD_LAST         0x91000002u
#define NVN_BARRIER_WORD_REF_CNT      0x80000014u
#define NVN_BARRIER_WORD_3D_BARRIER   0x80000040u
#define NVN_BARRIER_WORD_WAIT_IDLE    0x80000044u
#define NVN_BARRIER_WORD_FRAGMENT     0x80000378u
#define NVN_BARRIER_WORD_TILED        0x800003DFu
#define NVN_BARRIER_WORD_INVALIDATE   0x800003DDu
#define NVN_BARRIER_WORD_4A2          0x800004A2u
#define NVN_BARRIER_WORD_SHADER_CACHE 0x90110369u
#define NVN_BARRIER_WORD_TEX_HEADER   0x8000050Au
#define NVN_BARRIER_WORD_SAMPLER      0x80000509u
#define NVN_BARRIER_WORD_4B2          0x801904B2u
#define NVN_BARRIER_WORD_MEM_REF      0x8000C014u
#define NVN_BARRIER_WORD_MEM_OP_B     0x2001C00Bu
#define NVN_BARRIER_WORD_MEM_OP_16    0x80000000u
#define NVN_BARRIER_WORD_MEM_OP_14    0x70000000u

uint32_t nvn_barrier_words(uint32_t bits, uint32_t device_byte, uint32_t* words, uint32_t cap) {
    uint32_t out[NVN_BARRIER_WORDS_MAX];
    uint32_t count = 0;
    int sent_fragment_or_tiled = 0;
    out[count++] = NVN_BARRIER_WORD_FIRST;
    if (bits & (1u << 3)) {
        out[count++] = NVN_BARRIER_WORD_REF_CNT;
        out[count++] = NVN_BARRIER_WORD_3D_BARRIER;
    } else if (bits & (1u << 0)) {
        out[count++] = NVN_BARRIER_WORD_WAIT_IDLE;
    } else if (bits & (1u << 1)) {
        out[count++] = NVN_BARRIER_WORD_FRAGMENT;
        sent_fragment_or_tiled = 1;
    } else if (bits & (1u << 2)) {
        out[count++] = NVN_BARRIER_WORD_TILED;
        sent_fragment_or_tiled = 1;
    }
    if (bits & (1u << 4))
        out[count++] = sent_fragment_or_tiled ? NVN_BARRIER_WORD_INVALIDATE : NVN_BARRIER_WORD_4A2;
    if (bits & (1u << 5)) out[count++] = NVN_BARRIER_WORD_SHADER_CACHE;
    if (bits & (1u << 6)) {
        out[count++] = NVN_BARRIER_WORD_TEX_HEADER;
        out[count++] = NVN_BARRIER_WORD_SAMPLER;
    }
    if (bits & (1u << 7)) out[count++] = NVN_BARRIER_WORD_4B2;
    if (bits & (1u << 8)) {
        out[count++] = NVN_BARRIER_WORD_MEM_REF;
        if (device_byte) {
            out[count++] = NVN_BARRIER_WORD_MEM_OP_B;
            out[count++] = NVN_BARRIER_WORD_MEM_OP_16;
        }
        out[count++] = NVN_BARRIER_WORD_MEM_OP_B;
        out[count++] = NVN_BARRIER_WORD_MEM_OP_14;
    }
    out[count++] = NVN_BARRIER_WORD_LAST;
    if (!words || count > cap) return 0;
    memcpy(words, out, count * sizeof out[0]);
    return count;
}

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

static void bind_fixed(NvnBindWriter* w, uint32_t method, uint32_t count) {
    bind_word(w, NVN_PROGRAM_MODE_NON_INCREASING | (count << NVN_PROGRAM_COUNT_SHIFT) | method);
}

static void bind_value(NvnBindWriter* w, uint32_t method, uint32_t value) {
    bind_word(w, NVN_PROGRAM_MODE_INLINE | ((value & NVN_PROGRAM_IMMEDIATE_VALUE_MAX) << NVN_PROGRAM_COUNT_SHIFT) |
                     method);
}

static void bind_macro(NvnBindWriter* w, uint32_t method, uint32_t count) {
    bind_word(w, NVN_PROGRAM_MODE_INCREASE_ONCE | (count << NVN_PROGRAM_COUNT_SHIFT) | method);
}

/* The mode 5 packet of a bind: the header, the first data word, then the caller's words at method + 1. */
static void bind_macro_first(NvnBindWriter* w, uint32_t method, uint32_t count, uint32_t first) {
    bind_macro(w, method, count);
    bind_word(w, first);
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

/* BindStorageBuffers: one parameter word for the first slot, then the mode 3 run of four words for each entry. */
uint32_t nvn_bind_storage_buffers_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* entries,
                                        uint32_t* out) {
    if (!out || !entries || !count || stage >= NVN_BIND_STAGE_COUNT) return 0;
    if ((uint64_t)first + count > NVN_BIND_STORAGE_SLOT_COUNT) return 0;
    uint32_t param = NVN_BIND_STORAGE_PARAM_SCALE * (bind_storage_stage_base[stage] + first);
    if (param > NVN_PROGRAM_IMMEDIATE_VALUE_MAX) return 0;
    if (NVN_BIND_STORAGE_PARAM_SCALE * count > NVN_PROGRAM_IMMEDIATE_VALUE_MAX) return 0;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_STORAGE_METHOD_PARAM, param);
    bind_fixed(&w, NVN_BIND_STORAGE_METHOD_DATA, NVN_BIND_STORAGE_PARAM_SCALE * count);
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t addr = entries[2u * i];
        bind_word(&w, (uint32_t)addr);
        bind_word(&w, (uint32_t)(addr >> 32));
        bind_word(&w, (uint32_t)entries[2u * i + 1u]);
        bind_word(&w, 0);
    }
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

/* BindTextures: the slot word and the two words of each handle in one mode 5 packet, as the library sends it. */
uint32_t nvn_bind_textures_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* handles,
                                 int descriptor_flag, uint32_t* out) {
    if (!out || !handles || !count || stage >= NVN_BIND_STAGE_COUNT) return 0;
    if ((uint64_t)first + count > NVN_BIND_TEXTURE_SLOT_COUNT) return 0;
    const uint32_t words = 2u * count + 1u;
    if (words > NVN_PROGRAM_IMMEDIATE_VALUE_MAX) return 0;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_BIND_TEXTURE_METHOD_PARAM, bind_texture_stage_map[stage]);
    if (descriptor_flag) bind_value(&w, NVN_BIND_TEXTURE_METHOD_DESCRIPTOR, 0);
    bind_macro_first(&w, NVN_BIND_TEXTURE_METHOD_HANDLE, words,
                     first * NVN_BIND_TEXTURE_SLOT_STEP + NVN_BIND_TEXTURE_SLOT_BASE);
    for (uint32_t i = 0; i < count; i++) {
        bind_word(&w, (uint32_t)handles[i]);
        bind_word(&w, (uint32_t)(handles[i] >> 32));
    }
    return w.at;
}

/* ReportCounter: the type selects one query of the library table, four queries for type 15, or the timestamp packet for
   type 16. Each packet is four data words at 0x6C0, then one trigger word. */
#define NVN_REPORT_TYPE_MULTI 15u
#define NVN_REPORT_QUERY_COUNT 15u
#define NVN_REPORT_DATA_COUNT 4u
#define NVN_REPORT_QUERY_STEP 4u
#define NVN_REPORT_METHOD_COUNTER 0x6C0u
#define NVN_REPORT_METHOD_TIMESTAMP 0x004u
#define NVN_REPORT_METHOD_TRIGGER 0x3E0u
#define NVN_REPORT_TIMESTAMP_QUERY 0x00100002u

static const uint32_t report_counter_query[NVN_REPORT_QUERY_COUNT] = {
    0x0000F002u, 0x0A80F002u, 0x00801002u, 0x01801002u, 0x02802002u, 0x0D808002u, 0x0E809002u, 0x03806002u,
    0x0980A002u, 0x0F809002u, 0x04806002u, 0x07804002u, 0x08804002u, 0x09005002u, 0x05805002u,
};

static const uint32_t report_counter_multi_query[NVN_REPORT_DATA_COUNT] = {
    0x15007002u, 0x16007002u, 0x17007002u, 0x18007002u,
};

uint32_t nvn_report_counter_words(uint32_t type, uint64_t address, uint32_t* out) {
    if (!out || type > NVN_REPORT_TYPE_MAX) return 0;
    NvnBindWriter w = {out, 0};
    if (type == NVN_REPORT_TYPE_MAX) {
        bind_run(&w, NVN_REPORT_METHOD_TIMESTAMP, NVN_REPORT_DATA_COUNT);
        bind_word(&w, (uint32_t)(address >> 32));
        bind_word(&w, (uint32_t)address);
        bind_word(&w, 0);
        bind_word(&w, NVN_REPORT_TIMESTAMP_QUERY);
        bind_value(&w, NVN_REPORT_METHOD_TRIGGER, 0);
        return w.at;
    }
    if (type == NVN_REPORT_TYPE_MULTI) {
        for (uint32_t i = 0; i < NVN_REPORT_DATA_COUNT; i++) {
            uint64_t at = address + (uint64_t)NVN_REPORT_QUERY_STEP * i;
            bind_run(&w, NVN_REPORT_METHOD_COUNTER, NVN_REPORT_DATA_COUNT);
            bind_word(&w, (uint32_t)(at >> 32));
            bind_word(&w, (uint32_t)at);
            bind_word(&w, 0);
            bind_word(&w, report_counter_multi_query[i]);
        }
    } else {
        bind_run(&w, NVN_REPORT_METHOD_COUNTER, NVN_REPORT_DATA_COUNT);
        bind_word(&w, (uint32_t)(address >> 32));
        bind_word(&w, (uint32_t)address);
        bind_word(&w, 0);
        bind_word(&w, report_counter_query[type]);
    }
    bind_value(&w, NVN_REPORT_METHOD_TRIGGER, 0);
    return w.at;
}

/* ReportValue: the four data words of a type 0 report with the game's value. The library's 0x800003E0 trigger word is
   not sent: method 0x3E0 has no action in the host model. */
uint32_t nvn_report_value_words(uint32_t value, uint64_t address, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_REPORT_METHOD_COUNTER, NVN_REPORT_DATA_COUNT);
    bind_word(&w, (uint32_t)(address >> 32));
    bind_word(&w, (uint32_t)address);
    bind_word(&w, value);
    bind_word(&w, report_counter_query[0]);
    return w.at;
}

/* BindImage: one stage macro and the slot and two handle words for each handle. BindImages sends these words in one
   mode 5 packet. Stage 5 is the compute record and is not a method, so a stage above 4 is a refusal. */
#define NVN_BIND_IMAGE_METHOD_PARAM 0xE0Eu
#define NVN_BIND_IMAGE_METHOD_DESCRIPTOR 0x68Bu
#define NVN_BIND_IMAGE_METHOD_HANDLE 0x8E3u
#define NVN_BIND_IMAGE_DATA_COUNT 3u
#define NVN_BIND_IMAGE_SLOT_STEP 8u
#define NVN_BIND_IMAGE_SLOT_BASE 0x120u

uint32_t nvn_bind_image_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* handles,
                              int descriptor_flag, int plural, uint32_t* out) {
    if (!out || !handles || !count || count > NVN_IMAGE_BINDS_MAX || stage >= NVN_BIND_STAGE_COUNT) return 0;
    NvnBindWriter w = {out, 0};
    if (plural) {
        bind_value(&w, NVN_BIND_IMAGE_METHOD_PARAM, bind_texture_stage_map[stage]);
        if (descriptor_flag) bind_value(&w, NVN_BIND_IMAGE_METHOD_DESCRIPTOR, 0);
        bind_macro_first(&w, NVN_BIND_IMAGE_METHOD_HANDLE, 2u * count + 1u,
                         first * NVN_BIND_IMAGE_SLOT_STEP + NVN_BIND_IMAGE_SLOT_BASE);
        for (uint32_t i = 0; i < count; i++) {
            bind_word(&w, (uint32_t)handles[i]);
            bind_word(&w, (uint32_t)(handles[i] >> 32));
        }
        return w.at;
    }
    for (uint32_t i = 0; i < count; i++) {
        bind_value(&w, NVN_BIND_IMAGE_METHOD_PARAM, bind_texture_stage_map[stage]);
        if (descriptor_flag) bind_value(&w, NVN_BIND_IMAGE_METHOD_DESCRIPTOR, 0);
        bind_run(&w, NVN_BIND_IMAGE_METHOD_HANDLE, NVN_BIND_IMAGE_DATA_COUNT);
        bind_word(&w, (first + i) * NVN_BIND_IMAGE_SLOT_STEP + NVN_BIND_IMAGE_SLOT_BASE);
        bind_word(&w, (uint32_t)handles[i]);
        bind_word(&w, (uint32_t)(handles[i] >> 32));
    }
    return w.at;
}

/* ClearDepthStencil: the zcull flag, the optional scratch macro, the depth word, then the stencil branch or the depth
   only macro. The macro 0xE04 value is 3 for depth and stencil, 2 for stencil only and 1 for depth only. */
#define NVN_CLEAR_DEPTH_ENABLE_MASK 0xFFu
#define NVN_CLEAR_DEPTH_METHOD_ZCULL 0x36Fu
#define NVN_CLEAR_DEPTH_METHOD_DEPTH 0x364u
#define NVN_CLEAR_DEPTH_METHOD_STENCIL_MASK 0x4E7u
#define NVN_CLEAR_DEPTH_METHOD_STENCIL 0x368u
#define NVN_CLEAR_DEPTH_METHOD_SHADOW 0x049u
#define NVN_CLEAR_DEPTH_MACRO_E26 0xE26u
#define NVN_CLEAR_DEPTH_MACRO_STENCIL 0xE04u
#define NVN_CLEAR_DEPTH_FLAG_BELOW_HALF 0x1u
#define NVN_CLEAR_DEPTH_FLAG_ENDPOINT 0x10000u
#define NVN_CLEAR_DEPTH_SHADOW_MODE 2u
#define NVN_CLEAR_DEPTH_SHADOW_MODE_STENCIL 3u
#define NVN_CLEAR_DEPTH_SHADOW_MODE_DONE 1u
#define NVN_CLEAR_DEPTH_MACRO_DEPTH_AND_STENCIL 3u
#define NVN_CLEAR_DEPTH_MACRO_STENCIL_ONLY 2u
#define NVN_CLEAR_DEPTH_MACRO_DEPTH_ONLY 1u

/* The library writes nothing when neither the depth nor the stencil is enabled. That early return is not a refusal. */
int nvn_clear_depth_stencil_nothing(uint32_t depth_enable, uint32_t stencil_mask) {
    return (depth_enable & NVN_CLEAR_DEPTH_ENABLE_MASK) == 0 && stencil_mask == 0;
}

uint32_t nvn_clear_depth_stencil_words(uint32_t depth_bits, uint32_t depth_enable, uint32_t stencil,
                                       uint32_t stencil_mask, int zcull_clear, int use_macro_e26, uint32_t* out) {
    if (!out) return 0;
    int depth = (depth_enable & NVN_CLEAR_DEPTH_ENABLE_MASK) != 0;
    if (!depth && stencil_mask == 0) return 0;
    float depth_value;
    memcpy(&depth_value, &depth_bits, sizeof depth_value);
    NvnBindWriter w = {out, 0};
    if (zcull_clear && depth) {
        uint32_t flag = 0;
        if (depth_value < 0.5f) flag |= NVN_CLEAR_DEPTH_FLAG_BELOW_HALF;
        if (depth_value == 0.0f || depth_value == 1.0f) flag |= NVN_CLEAR_DEPTH_FLAG_ENDPOINT;
        bind_run(&w, NVN_CLEAR_DEPTH_METHOD_ZCULL, 1);
        bind_word(&w, flag);
    }
    if (use_macro_e26 && zcull_clear && depth) bind_value(&w, NVN_CLEAR_DEPTH_MACRO_E26, 0);
    if (depth) {
        bind_run(&w, NVN_CLEAR_DEPTH_METHOD_DEPTH, 1);
        bind_word(&w, depth_bits);
    }
    if (stencil_mask != 0) {
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_SHADOW, NVN_CLEAR_DEPTH_SHADOW_MODE);
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_STENCIL_MASK, stencil_mask & 0xFFu);
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_STENCIL, stencil & 0xFFu);
        bind_value(&w, NVN_CLEAR_DEPTH_MACRO_STENCIL,
                   depth ? NVN_CLEAR_DEPTH_MACRO_DEPTH_AND_STENCIL : NVN_CLEAR_DEPTH_MACRO_STENCIL_ONLY);
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_SHADOW, NVN_CLEAR_DEPTH_SHADOW_MODE_STENCIL);
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_STENCIL_MASK, 0);
        bind_value(&w, NVN_CLEAR_DEPTH_METHOD_SHADOW, NVN_CLEAR_DEPTH_SHADOW_MODE_DONE);
    } else if (depth) {
        bind_value(&w, NVN_CLEAR_DEPTH_MACRO_STENCIL, NVN_CLEAR_DEPTH_MACRO_DEPTH_ONLY);
    }
    return w.at;
}

/* CopyBufferToBuffer and ClearBuffer: the DMA engine of subchannel 4 (analyst-u3-A-1.md). The library's open, close and
   chunk markers write method 2 of subchannel 0, which the host engines do not need, so the builders leave them out. */
#define NVN_DMA_SUBCHANNEL                 4u
#define NVN_DMA_METHOD_ADDRESS             0x100u
#define NVN_DMA_METHOD_SIZE                0x106u
#define NVN_DMA_METHOD_LAUNCH              0x0C0u
#define NVN_DMA_METHOD_REMAP_CONST         0x1C0u
#define NVN_DMA_METHOD_REMAP_CONST_FORMAT  0x1C2u
#define NVN_DMA_METHOD_REMAP_WIDTH         0x1C4u
#define NVN_DMA_COPY_LAUNCH                0x186u
#define NVN_DMA_CLEAR_LAUNCH               0x586u
#define NVN_DMA_CLEAR_CONST_FORMAT         0x34444u
#define NVN_DMA_CLEAR_HEIGHT               1u

static void dma_run(NvnBindWriter* w, uint32_t method, uint32_t count) {
    bind_word(w, NVN_PROGRAM_MODE_INCREASING | (count << NVN_PROGRAM_COUNT_SHIFT) |
                    (NVN_DMA_SUBCHANNEL << NVN_WORD_SUBCHANNEL_SHIFT) | method);
}

uint32_t nvn_copy_buffer_words(uint64_t src, uint64_t dst, uint64_t size, uint32_t* out, uint32_t cap) {
    if (!out) return 0;
    uint64_t chunks = size / NVN_COPY_BUFFER_CHUNK_BYTES + (size % NVN_COPY_BUFFER_CHUNK_BYTES != 0);
    if (chunks * NVN_COPY_BUFFER_WORDS_PER_CHUNK > cap) return 0;
    NvnBindWriter w = {out, 0};
    uint64_t done = 0;
    while (done < size) {
        uint64_t bytes = size - done;
        if (bytes > NVN_COPY_BUFFER_CHUNK_BYTES) bytes = NVN_COPY_BUFFER_CHUNK_BYTES;
        uint64_t from = src + done, to = dst + done;
        dma_run(&w, NVN_DMA_METHOD_ADDRESS, 4);
        bind_word(&w, (uint32_t)(from >> 32));
        bind_word(&w, (uint32_t)from);
        bind_word(&w, (uint32_t)(to >> 32));
        bind_word(&w, (uint32_t)to);
        dma_run(&w, NVN_DMA_METHOD_SIZE, 1);
        bind_word(&w, (uint32_t)bytes);
        dma_run(&w, NVN_DMA_METHOD_LAUNCH, 1);
        bind_word(&w, NVN_DMA_COPY_LAUNCH);
        done += bytes;
    }
    return w.at;
}

uint32_t nvn_clear_buffer_words(uint64_t dst, uint64_t size, uint32_t value, uint32_t* out) {
    if (!out) return 0;
    uint32_t words = (uint32_t)(size >> 2);
    if (!words) return 0; /* the library returns before any word */
    NvnBindWriter w = {out, 0};
    dma_run(&w, NVN_DMA_METHOD_REMAP_CONST, 1);
    bind_word(&w, value);
    dma_run(&w, NVN_DMA_METHOD_REMAP_CONST_FORMAT, 1);
    bind_word(&w, NVN_DMA_CLEAR_CONST_FORMAT);
    dma_run(&w, NVN_DMA_METHOD_ADDRESS, 4);
    bind_word(&w, (uint32_t)(dst >> 32));
    bind_word(&w, (uint32_t)dst);
    bind_word(&w, (uint32_t)(dst >> 32));
    bind_word(&w, (uint32_t)dst);
    dma_run(&w, NVN_DMA_METHOD_REMAP_WIDTH, 2);
    bind_word(&w, words);
    bind_word(&w, NVN_DMA_CLEAR_HEIGHT);
    dma_run(&w, NVN_DMA_METHOD_SIZE, 1);
    bind_word(&w, words);
    dma_run(&w, NVN_DMA_METHOD_LAUNCH, 1);
    bind_word(&w, NVN_DMA_CLEAR_LAUNCH);
    return w.at;
}

/* UpdateUniformBuffer: the 0x8E0 packet, the 0x68B PipeNop and mode 3 chunks of at most 127 data words. */
#define NVN_UPDATE_UNIFORM_METHOD_DATA    0x8E0u
#define NVN_UPDATE_UNIFORM_DATA_COUNT     4u
#define NVN_UPDATE_UNIFORM_METHOD_NOP     0x68Bu
#define NVN_UPDATE_UNIFORM_METHOD_UPLOAD  0x8E4u
#define NVN_UPDATE_UNIFORM_CHUNK_MODE     0x60000000u
#define NVN_UPDATE_UNIFORM_SIZE_MIN       0x100u
#define NVN_UPDATE_UNIFORM_SIZE_ALIGN     0xFFu

uint32_t nvn_update_uniform_size256(uint32_t size) {
    return size < NVN_UPDATE_UNIFORM_SIZE_MIN ? NVN_UPDATE_UNIFORM_SIZE_MIN
                                              : (size + NVN_UPDATE_UNIFORM_SIZE_ALIGN) & ~NVN_UPDATE_UNIFORM_SIZE_ALIGN;
}

uint32_t nvn_update_uniform_words(uint64_t address, uint32_t size256, uint32_t offset, const uint32_t* data,
                                  uint32_t words, uint32_t* out) {
    if (!out || (words && !data) || words > NVN_UPDATE_UNIFORM_DATA_WORDS) return 0;
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_UPDATE_UNIFORM_METHOD_DATA, NVN_UPDATE_UNIFORM_DATA_COUNT);
    bind_word(&w, size256);
    bind_word(&w, (uint32_t)(address >> 32));
    bind_word(&w, (uint32_t)address);
    bind_word(&w, offset);
    bind_value(&w, NVN_UPDATE_UNIFORM_METHOD_NOP, 0);
    for (uint32_t at = 0; at < words;) {
        uint32_t chunk = words - at;
        if (chunk > NVN_UPDATE_UNIFORM_CHUNK_WORDS) chunk = NVN_UPDATE_UNIFORM_CHUNK_WORDS;
        bind_word(&w, NVN_UPDATE_UNIFORM_CHUNK_MODE | (chunk << NVN_PROGRAM_COUNT_SHIFT) |
                         NVN_UPDATE_UNIFORM_METHOD_UPLOAD);
        for (uint32_t i = 0; i < chunk; i++) bind_word(&w, data[at + i]);
        at += chunk;
    }
    return w.at;
}

#define NVN_XFB_METHOD_BUFFER 0xE0u      /* enable, address high, address low, size of buffer 0 */
#define NVN_XFB_METHOD_BUFFER_STEP 8u
#define NVN_XFB_BUFFER_WORDS 4u
#define NVN_XFB_METHOD_ENABLE 0x1D1u
#define NVN_XFB_METHOD_BEGIN_MACRO 0xE00u /* macro 0 of the library's macro code */
#define NVN_XFB_METHOD_REPORT 0x6C0u      /* report semaphore: address high, address low, payload, query */
#define NVN_XFB_REPORT_WORDS 4u
#define NVN_XFB_COUNT_OFFSET 4u          /* the byte count of buffer i goes to address + 4 + 4i */

/* The query words of End, from the library's constants at sdk 0xde13dc0, 0xde135c8, 0xde13e50 and 0xde13f78. */
static const uint32_t xfb_count_query[NVN_XFB_BUFFER_COUNT] = {0x1D005002u, 0x1D005022u, 0x1D005042u, 0x1D005062u};

uint32_t nvn_bind_xfb_words(uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out) {
    if (!out || slot >= NVN_XFB_BUFFER_COUNT) return 0;
    NvnBindWriter w = {out, 0};
    uint32_t method = NVN_XFB_METHOD_BUFFER + NVN_XFB_METHOD_BUFFER_STEP * slot;
    if (!addr) {
        bind_value(&w, method, 0);
        return w.at;
    }
    bind_run(&w, method, NVN_XFB_BUFFER_WORDS);
    bind_word(&w, 1);
    bind_word(&w, (uint32_t)(addr >> 32));
    bind_word(&w, (uint32_t)addr);
    bind_word(&w, size);
    return w.at;
}

uint32_t nvn_xfb_words(uint32_t kind, uint64_t addr, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    switch (kind) {
    case NVN_XFB_BEGIN:
        bind_macro(&w, NVN_XFB_METHOD_BEGIN_MACRO, 2);
        bind_word(&w, (uint32_t)(addr >> 32));
        bind_word(&w, (uint32_t)addr);
        return w.at;
    case NVN_XFB_END:
    case NVN_XFB_PAUSE:
        bind_value(&w, NVN_XFB_METHOD_ENABLE, 0);
        /* A pause without an address is the disable alone. With an address it writes the End reports. */
        if (kind == NVN_XFB_PAUSE && !addr) return w.at;
        for (uint32_t i = 0; i < NVN_XFB_BUFFER_COUNT; i++) {
            uint64_t at = addr + NVN_XFB_COUNT_OFFSET * (i + 1u);
            bind_run(&w, NVN_XFB_METHOD_REPORT, NVN_XFB_REPORT_WORDS);
            bind_word(&w, (uint32_t)(at >> 32));
            bind_word(&w, (uint32_t)at);
            bind_word(&w, 0);
            bind_word(&w, xfb_count_query[i]);
        }
        return w.at;
    case NVN_XFB_RESUME:
        if (addr) return 0; /* a resume with an address reads the byte counts that the GPU writes */
        bind_value(&w, NVN_XFB_METHOD_ENABLE, 1);
        return w.at;
    default:
        return 0;
    }
}

/* The U4 draw state setters (analyst-u4-A-1.md, analyst-u4-B-1.md). The tables give the words of each function. */
#define NVN_TESS_METHOD_PATCH_SIZE 0x373u /* I(0x373, patches): the library does not mask the value */
#define NVN_TESS_METHOD_INNER_LEVELS 0xCDu
#define NVN_TESS_METHOD_OUTER_LEVELS 0xC9u
#define NVN_PRIMITIVE_RESTART_METHOD_ENABLE 0x591u
#define NVN_PRIMITIVE_RESTART_METHOD_INDEX 0x592u
#define NVN_RENDER_ENABLE_METHOD 0x556u
#define NVN_RENDER_ENABLE_CONDITIONAL_METHOD 0x554u
#define NVN_RENDER_ENABLE_CONDITIONAL_WORDS 3u
#define NVN_RENDER_ENABLE_CONDITION 0u
#define NVN_RENDER_ENABLE_SYNC 1u
#define NVN_RENDER_ENABLE_MODE_PARAMETER 3u /* condition 0: the draw waits on the report */
#define NVN_RENDER_ENABLE_MODE_SYNC 4u      /* condition 1: the draw waits on a sync */
#define NVN_VIEWPORT_SWIZZLE_FIELD_MASK 0x7u
#define NVN_VIEWPORT_SWIZZLE_FIELD_BITS 4u
#define NVN_VIEWPORT_SWIZZLE_IDENTITY 0x6420u /* MAXWELL3D_VIEWPORT_SWIZZLE_IDENTITY after the packing */

uint32_t nvn_patch_size_words(uint32_t patches, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    /* The library shifts the value without the 13-bit mask, so a value above 0x1FFF changes the mode bits too. */
    bind_word(&w, NVN_PROGRAM_MODE_INLINE | (patches << NVN_PROGRAM_COUNT_SHIFT) | NVN_TESS_METHOD_PATCH_SIZE);
    return w.at;
}

uint32_t nvn_inner_tessellation_levels_words(const uint32_t bits[2], uint32_t* out) {
    if (!out || !bits) return 0;
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_TESS_METHOD_INNER_LEVELS, NVN_TESS_INNER_LEVEL_COUNT);
    for (uint32_t i = 0; i < NVN_TESS_INNER_LEVEL_COUNT; i++) bind_word(&w, bits[i]);
    return w.at;
}

uint32_t nvn_outer_tessellation_levels_words(const uint32_t bits[4], uint32_t* out) {
    if (!out || !bits) return 0;
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_TESS_METHOD_OUTER_LEVELS, NVN_TESS_OUTER_LEVEL_COUNT);
    for (uint32_t i = 0; i < NVN_TESS_OUTER_LEVEL_COUNT; i++) bind_word(&w, bits[i]);
    return w.at;
}

uint32_t nvn_primitive_restart_words(uint32_t enable, uint32_t index, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    uint32_t on = (enable & 0xFFu) != 0;
    bind_value(&w, NVN_PRIMITIVE_RESTART_METHOD_ENABLE, on);
    if (on) {
        bind_run(&w, NVN_PRIMITIVE_RESTART_METHOD_INDEX, 1);
        bind_word(&w, index);
    }
    return w.at;
}

uint32_t nvn_render_enable_words(uint32_t enable, uint32_t* out) {
    if (!out) return 0;
    NvnBindWriter w = {out, 0};
    bind_value(&w, NVN_RENDER_ENABLE_METHOD, (enable & 0xFFu) != 0 ? 1u : 0u);
    return w.at;
}

uint32_t nvn_render_enable_conditional_words(uint32_t condition, uint64_t sync, uint32_t* out) {
    if (!out) return 0;
    uint32_t mode;
    if (condition == NVN_RENDER_ENABLE_CONDITION) mode = NVN_RENDER_ENABLE_MODE_PARAMETER;
    else if (condition == NVN_RENDER_ENABLE_SYNC) mode = NVN_RENDER_ENABLE_MODE_SYNC;
    else return 0; /* the library writes nothing for another condition */
    NvnBindWriter w = {out, 0};
    bind_run(&w, NVN_RENDER_ENABLE_CONDITIONAL_METHOD, NVN_RENDER_ENABLE_CONDITIONAL_WORDS);
    bind_word(&w, (uint32_t)(sync >> 32));
    bind_word(&w, (uint32_t)sync);
    bind_word(&w, mode);
    return w.at;
}

uint32_t nvn_conservative_raster_dilate_level(float dilate) {
    float scaled = ceilf(dilate * 4.0f);
    if (!(scaled > 0.0f)) return 0; /* NaN and every value at or below 0 */
    if (scaled > 3.0f) return 3;
    return (uint32_t)scaled;
}

uint32_t nvn_viewport_swizzle_word(const uint32_t entry[4]) {
    uint32_t word = entry[0] & NVN_VIEWPORT_SWIZZLE_FIELD_MASK;
    word |= (entry[1] & NVN_VIEWPORT_SWIZZLE_FIELD_MASK) << NVN_VIEWPORT_SWIZZLE_FIELD_BITS;
    word |= (entry[2] & NVN_VIEWPORT_SWIZZLE_FIELD_MASK) << (2u * NVN_VIEWPORT_SWIZZLE_FIELD_BITS);
    word |= (entry[3] & NVN_VIEWPORT_SWIZZLE_FIELD_MASK) << (3u * NVN_VIEWPORT_SWIZZLE_FIELD_BITS);
    return word;
}

int nvn_viewport_swizzles_identity(const uint32_t* words, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        if (words[i] != NVN_VIEWPORT_SWIZZLE_IDENTITY) return 0;
    return 1;
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
#define COMPUTE_QMD_CBUF_SIZE_MAX (UINT32_MAX >> NVN_COMPUTE_CBUF_ADDRESS_HIGH_SHIFT)
#define COMPUTE_QMD_CBUF_SIZE_PADDING 0xFFu /* uniform sizes round up to 256 bytes */

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
    if ((uint64_t)prog->h6fc + COMPUTE_QMD_CBUF_SIZE_PADDING > COMPUTE_QMD_CBUF_SIZE_MAX)
        return NVN_QMD_CBUF1_SIZE;
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_SLOTS; slot++) {
        const NvnComputeBindEntry* entry = &binds->uniform[slot];
        if (!entry->bound) continue;
        uint32_t reason = NVN_QMD_OK;
        if (entry->size > UINT32_MAX)
            reason = NVN_QMD_UNIFORM_SIZE;
        else if (slot < NVN_COMPUTE_UNIFORM_QBUF_SLOTS &&
                 entry->size > (COMPUTE_QMD_CBUF_SIZE_MAX & ~COMPUTE_QMD_CBUF_SIZE_PADDING))
            reason = NVN_QMD_UNIFORM_QBUF_SIZE;
        if (reason != NVN_QMD_OK) {
            if (detail) *detail = slot;
            return reason;
        }
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
    case NVN_QMD_CBUF1_SIZE: return "constant buffer 1 size above its QMD field";
    case NVN_QMD_UNIFORM_SIZE: return "uniform size above 32 bits";
    case NVN_QMD_UNIFORM_QBUF_SIZE: return "rounded uniform size above its QMD field";
    default: return "unknown";
    }
}

int nvn_compute_qmd_build(const NvnProgramComputeIn* prog, uint32_t x, uint32_t y, uint32_t z,
                          const NvnComputeBinds* binds, uint32_t qmd[NVN_COMPUTE_QMD_WORDS]) {
    if (!qmd || nvn_compute_qmd_check(prog, x, y, z, binds, NULL) != NVN_QMD_OK) return 0;
    uint64_t cb1_size = (uint64_t)prog->h6fc + COMPUTE_QMD_CBUF_SIZE_PADDING;
    memset(qmd, 0, NVN_COMPUTE_QMD_WORDS * sizeof qmd[0]);
    qmd[COMPUTE_QMD_PROGRAM_START] = prog->code_offset;
    qmd[COMPUTE_QMD_GRID_DIM_X] = x & COMPUTE_QMD_GRID_X_MAX;
    qmd[COMPUTE_QMD_GRID_DIM_YZ] = (y & COMPUTE_QMD_GRID_YZ_MAX) | (z << COMPUTE_QMD_GRID_Z_SHIFT);
    qmd[COMPUTE_QMD_SHARED_ALLOC] =
        (((prog->shared_bytes + 0xFFu) >> COMPUTE_QMD_SHARED_ALLOC_SHIFT) & COMPUTE_QMD_SHARED_ALLOC_MASK)
        << COMPUTE_QMD_SHARED_ALLOC_SHIFT;
    qmd[COMPUTE_QMD_BLOCK_DIM_X] = (prog->block_x & COMPUTE_QMD_GRID_YZ_MAX) << COMPUTE_QMD_BLOCK_X_SHIFT;
    qmd[COMPUTE_QMD_BLOCK_DIM_YZ] = (prog->block_y & COMPUTE_QMD_GRID_YZ_MAX) |
                                   ((prog->block_z & COMPUTE_QMD_GRID_YZ_MAX) << COMPUTE_QMD_BLOCK_Z_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_POS_ALLOC] =
        (prog->local_pos & COMPUTE_QMD_LOCAL_MASK) | ((prog->barriers & COMPUTE_QMD_BARRIER_MASK) << COMPUTE_QMD_BARRIER_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_NEG_ALLOC] =
        (prog->local_neg & COMPUTE_QMD_LOCAL_MASK) | (COMPUTE_QMD_GPR_ALLOC_UNKNOWN << COMPUTE_QMD_GPR_SHIFT);
    qmd[COMPUTE_QMD_LOCAL_CRS_ALLOC] =
        (prog->local_crs & COMPUTE_QMD_LOCAL_MASK) | (COMPUTE_QMD_SASS_VERSION_UNKNOWN << COMPUTE_QMD_GPR_SHIFT);
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
        uint32_t size = ((uint32_t)entry->size + COMPUTE_QMD_CBUF_SIZE_PADDING) & ~COMPUTE_QMD_CBUF_SIZE_PADDING;
        compute_qmd_cbuf_config(qmd, slot + 3u, entry->addr, size);
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

int nvn_span_inside(uint64_t at, uint64_t size, uint64_t lo, uint64_t hi) {
    return at >= lo && at <= hi && size <= hi - at;
}

/* The library's list is sorted by strcmp and holds the names without the "nvn" prefix. A binary search needs no other
   check: the table check rejects a list that is not sorted. */
int nvn_proc_name_index(const char* const* names, uint32_t count, const char* name) {
    if (!names || !name || strncmp(name, "nvn", 3)) return -1;
    name += 3;
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const char* entry = names[mid];
        if (!entry) return -1;
        int order = strcmp(name, entry);
        if (order < 0) hi = mid;
        else if (order > 0) lo = mid + 1;
        else return (int)mid;
    }
    return -1;
}

/* A table that fails this check cannot answer a lookup: a wrong entry would run another function or an address that is
   not code. The first bad index is enough for the caller's fatal line. */
int nvn_proc_table_check(const char* const* names, const uint64_t* addrs, uint32_t count, uint64_t code_lo,
                         uint64_t code_hi) {
    if (!count) return -1;
    if (!names || !addrs) return 0;
    for (uint32_t i = 0; i < count; i++) {
        const char* entry = names[i];
        if (!entry || !*entry) return (int)i;
        if (i && strcmp(names[i - 1], entry) >= 0) return (int)i;
        uint64_t addr = addrs[i];
        if (addr < code_lo || addr >= code_hi || (addr & 3)) return (int)i;
    }
    return -1;
}
