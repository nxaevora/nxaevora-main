/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_replay.h"
#include <math.h>
#include <string.h>

/* Maxwell 3D method numbers, as the library writes them (analyst-3c-C-1.md, section algorithm). */
#define NVN_REPLAY_TEX_POOL_HI      0x55Du
#define NVN_REPLAY_TEX_POOL_LO      0x55Eu
#define NVN_REPLAY_TEX_POOL_LIMIT   0x55Fu
#define NVN_REPLAY_SAMPLER_POOL_HI    0x557u
#define NVN_REPLAY_SAMPLER_POOL_LO    0x558u
#define NVN_REPLAY_SAMPLER_POOL_LIMIT 0x559u

#define NVN_REPLAY_VIEWPORT_SCALE_X     0x280u
#define NVN_REPLAY_VIEWPORT_SCALE_Y     0x281u
#define NVN_REPLAY_VIEWPORT_TRANSLATE_X 0x283u
#define NVN_REPLAY_VIEWPORT_TRANSLATE_Y 0x284u
#define NVN_REPLAY_VIEWPORT_CLIP_X      0x300u
#define NVN_REPLAY_VIEWPORT_CLIP_Y      0x301u
#define NVN_REPLAY_VIEWPORT_METHOD_STRIDE 8u
#define NVN_REPLAY_VIEWPORT_CLIP_STRIDE   4u

#define NVN_REPLAY_SCISSOR_X      0x381u
#define NVN_REPLAY_SCISSOR_Y      0x382u
#define NVN_REPLAY_SCISSOR_STRIDE 4u
#define NVN_REPLAY_SCISSOR_MAX    0xFFFFu

#define NVN_REPLAY_CLEAR_COLOR_BASE        0x360u
#define NVN_REPLAY_CLEAR_LAYERS_REG_BASE   0x206u
#define NVN_REPLAY_CLEAR_LAYERS_REG_STRIDE 16u
#define NVN_REPLAY_CLEAR_SURFACE           0x674u
#define NVN_REPLAY_CLEAR_LAYER_STRIDE      0x400u
#define NVN_REPLAY_CLEAR_ARG_TARGET_SHIFT  6u
#define NVN_REPLAY_CLEAR_ARG_MASK_SHIFT    2u
#define NVN_REPLAY_CLEAR_ARG_FIELD_MASK    0xFu

#define NVN_REPLAY_CLIP_LO_MAX 0xFFFEu
#define NVN_REPLAY_CLIP_HI_MAX 0xFFFFu

#define NVN_REPLAY_BARRIER_BITS 9u

#define NVN_REPLAY_STENCIL_REF_FRONT        0x4E5u
#define NVN_REPLAY_STENCIL_REF_BACK         0x3D5u
#define NVN_REPLAY_STENCIL_VALUE_MASK_FRONT 0x4E6u
#define NVN_REPLAY_STENCIL_VALUE_MASK_BACK  0x3D7u
#define NVN_REPLAY_STENCIL_MASK_FRONT       0x4E7u
#define NVN_REPLAY_STENCIL_MASK_BACK        0x3D6u
#define NVN_REPLAY_STENCIL_FACE_FRONT       0x1u
#define NVN_REPLAY_STENCIL_FACE_BACK        0x2u
#define NVN_REPLAY_STENCIL_VALUE_MASK       0xFFu
#define NVN_REPLAY_BLEND_COLOR_BASE         0x4C7u
#define NVN_REPLAY_BLEND_COLOR_WORDS        4u
#define NVN_REPLAY_ALPHA_REF                0x4C4u

static uint32_t padded_payload(size_t bytes) {
    return (uint32_t)((bytes + (NVN_OP_ALIGN - 1u)) & ~(size_t)(NVN_OP_ALIGN - 1u));
}

static int payload_matches(const NvnOpHeader* header, size_t struct_bytes) {
    return header->bytes == padded_payload(struct_bytes);
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void replay_fault(const NvnReplayEnv* env, NvnReplayStats* stats, const char* what, uint64_t value) {
    stats->faults++;
    env->fault(env->ctx, what, value);
}

static void replay_method(const NvnReplayEnv* env, NvnReplayStats* stats, uint32_t method, uint32_t value) {
    env->method(env->ctx, method, value);
    stats->methods++;
}

static uint32_t viewport_clip_word(float origin, float size) {
    if (!(size > 0.0f)) return 0u;
    double a = (double)origin;
    double s = (double)size;
    double floor_a = floor(a);
    double hi_d = ceil(a + s) - floor_a;
    double lo_d = floor_a;
    if (lo_d < 0.0) lo_d = 0.0;
    if (lo_d > (double)NVN_REPLAY_CLIP_LO_MAX) lo_d = (double)NVN_REPLAY_CLIP_LO_MAX;
    if (hi_d < 0.0) hi_d = 0.0;
    double room = (double)NVN_REPLAY_CLIP_HI_MAX - lo_d;
    if (hi_d > room) hi_d = room;
    return (uint32_t)lo_d | ((uint32_t)hi_d << 16);
}

static uint32_t scissor_axis_word(int32_t origin, int32_t size, int* empty) {
    int64_t a = (int64_t)origin;
    int64_t s = (int64_t)size;
    int64_t lo = a > 0 ? a : 0;
    int64_t hi = a + s;
    if (hi > (int64_t)NVN_REPLAY_SCISSOR_MAX) hi = (int64_t)NVN_REPLAY_SCISSOR_MAX;
    *empty = s <= 0 || hi <= lo;
    return *empty ? 0u : ((uint32_t)lo | ((uint32_t)hi << 16));
}

static int replay_pool(const NvnReplayEnv* env, NvnReplayStats* stats, NvnOpCode code, const NvnOpPool* op,
                       uint32_t method_hi, uint32_t method_lo, uint32_t method_limit) {
    uint64_t base = 0;
    uint32_t count = 0;
    if (!env->descriptor_pool(env->ctx, code, op, &base, &count)) {
        replay_fault(env, stats, "pool gone", op->pool);
        return 0;
    }
    if (count == 0) {
        replay_fault(env, stats, "pool empty", op->pool);
        return 0;
    }
    replay_method(env, stats, method_hi, (uint32_t)(base >> 32));
    replay_method(env, stats, method_lo, (uint32_t)base);
    replay_method(env, stats, method_limit, count - 1u);
    return 1;
}

static int replay_viewport(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpViewport* op) {
    if (op->index >= NVN_VIEWPORTS_MAX) {
        replay_fault(env, stats, "viewport index", op->index);
        return 0;
    }
    if (!isfinite(op->x) || !isfinite(op->y) || !isfinite(op->width) || !isfinite(op->height)) {
        replay_fault(env, stats, "viewport value", op->index);
        return 0;
    }
    uint32_t i = op->index;
    float half_width = op->width * 0.5f;
    float half_height = op->height * 0.5f;
    float scale_y = env->window_origin_flip ? -half_height : half_height;
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_SCALE_X + NVN_REPLAY_VIEWPORT_METHOD_STRIDE * i,
                  float_bits(half_width));
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_SCALE_Y + NVN_REPLAY_VIEWPORT_METHOD_STRIDE * i,
                  float_bits(scale_y));
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_TRANSLATE_X + NVN_REPLAY_VIEWPORT_METHOD_STRIDE * i,
                  float_bits(op->x + half_width));
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_TRANSLATE_Y + NVN_REPLAY_VIEWPORT_METHOD_STRIDE * i,
                  float_bits(op->y + half_height));
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_CLIP_X + NVN_REPLAY_VIEWPORT_CLIP_STRIDE * i,
                  viewport_clip_word(op->x, op->width));
    replay_method(env, stats, NVN_REPLAY_VIEWPORT_CLIP_Y + NVN_REPLAY_VIEWPORT_CLIP_STRIDE * i,
                  viewport_clip_word(op->y, op->height));
    return 1;
}

static int replay_scissor(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpScissor* op) {
    if (op->index >= NVN_VIEWPORTS_MAX) {
        replay_fault(env, stats, "scissor index", op->index);
        return 0;
    }
    uint32_t i = op->index;
    int empty_x, empty_y;
    uint32_t x_word = scissor_axis_word(op->x, op->width, &empty_x);
    uint32_t y_word = scissor_axis_word(op->y, op->height, &empty_y);
    if (empty_x || empty_y) {
        x_word = 0u;
        y_word = 0u;
    }
    replay_method(env, stats, NVN_REPLAY_SCISSOR_X + NVN_REPLAY_SCISSOR_STRIDE * i, x_word);
    replay_method(env, stats, NVN_REPLAY_SCISSOR_Y + NVN_REPLAY_SCISSOR_STRIDE * i, y_word);
    return 1;
}

static int replay_clear(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpClearColor* op) {
    if (op->index >= NVN_RENDER_TARGETS_MAX) {
        replay_fault(env, stats, "clear index", op->index);
        return 0;
    }
    for (uint32_t k = 0; k < 4u; k++)
        replay_method(env, stats, NVN_REPLAY_CLEAR_COLOR_BASE + k, op->color[k]);
    uint32_t layers = env->read_register(env->ctx,
        NVN_REPLAY_CLEAR_LAYERS_REG_BASE + NVN_REPLAY_CLEAR_LAYERS_REG_STRIDE * op->index);
    if (layers > NVN_REPLAY_LAYER_MAX) {
        replay_fault(env, stats, "clear layers", layers);
        return 0;
    }
    uint32_t arg = ((op->index & NVN_REPLAY_CLEAR_ARG_FIELD_MASK) << NVN_REPLAY_CLEAR_ARG_TARGET_SHIFT) |
                   ((op->mask & NVN_REPLAY_CLEAR_ARG_FIELD_MASK) << NVN_REPLAY_CLEAR_ARG_MASK_SHIFT);
    for (uint32_t layer = 0; layer < layers; layer++)
        replay_method(env, stats, NVN_REPLAY_CLEAR_SURFACE, arg + NVN_REPLAY_CLEAR_LAYER_STRIDE * layer);
    return 1;
}

static int replay_barrier(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBarrier* op) {
    (void)env;
    for (uint32_t bit = 0; bit < NVN_REPLAY_BARRIER_BITS; bit++)
        if (op->bits & (1u << bit)) stats->barrier_bits[bit]++;
    return 1;
}

/* The method pair of a stencil kind: front first, then back. 0 for a kind the library does not write. */
static int stencil_method_pair(uint32_t which, uint32_t* front, uint32_t* back) {
    switch (which) {
    case NVN_STENCIL_REF:
        *front = NVN_REPLAY_STENCIL_REF_FRONT;
        *back = NVN_REPLAY_STENCIL_REF_BACK;
        return 1;
    case NVN_STENCIL_VALUE_MASK:
        *front = NVN_REPLAY_STENCIL_VALUE_MASK_FRONT;
        *back = NVN_REPLAY_STENCIL_VALUE_MASK_BACK;
        return 1;
    case NVN_STENCIL_MASK:
        *front = NVN_REPLAY_STENCIL_MASK_FRONT;
        *back = NVN_REPLAY_STENCIL_MASK_BACK;
        return 1;
    default:
        return 0;
    }
}

static int replay_stencil(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpStencil* op) {
    uint32_t front, back;
    if (!stencil_method_pair(op->which, &front, &back)) {
        replay_fault(env, stats, "stencil kind", op->which);
        return 0;
    }
    uint32_t value = op->value & NVN_REPLAY_STENCIL_VALUE_MASK;
    if (op->face & NVN_REPLAY_STENCIL_FACE_FRONT) replay_method(env, stats, front, value);
    if (op->face & NVN_REPLAY_STENCIL_FACE_BACK) replay_method(env, stats, back, value);
    return 1;
}

static int replay_blend_color(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBlendColor* op) {
    for (uint32_t k = 0; k < NVN_REPLAY_BLEND_COLOR_WORDS; k++)
        replay_method(env, stats, NVN_REPLAY_BLEND_COLOR_BASE + k, op->color[k]);
    return 1;
}

static int replay_alpha_ref(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpAlphaRef* op) {
    replay_method(env, stats, NVN_REPLAY_ALPHA_REF, op->bits);
    return 1;
}

static int replay_bind_texture(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBindTexture* op) {
    if (!env->bind_texture || !env->bind_texture(env->ctx, op->stage, op->slot, op->handle)) {
        replay_fault(env, stats, "bind texture", op->handle);
        return 0;
    }
    return 1;
}

static int replay_bind_buffer(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBindBuffer* op) {
    if (!env->bind_buffer || !env->bind_buffer(env->ctx, op)) {
        replay_fault(env, stats, "bind buffer", op->addr);
        return 0;
    }
    return 1;
}

static int replay_bind_state(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBindState* op) {
    if (!env->bind_state || !env->bind_state(env->ctx, op)) {
        replay_fault(env, stats, "bind state", op->kind);
        return 0;
    }
    return 1;
}

static int replay_draw(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpDraw* op) {
    if (!env->draw || !env->draw(env->ctx, op)) {
        replay_fault(env, stats, "draw", op->kind);
        return 0;
    }
    return 1;
}

static int replay_dispatch(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpDispatch* op) {
    if (!env->dispatch || !env->dispatch(env->ctx, op)) {
        replay_fault(env, stats, "dispatch", op->x);
        return 0;
    }
    return 1;
}

static int replay_bind_program(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpBindProgram* op) {
    if (!env->bind_program || !env->bind_program(env->ctx, op)) {
        replay_fault(env, stats, "bind program", op->stage_mask);
        return 0;
    }
    return 1;
}

static int replay_raster(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpRaster* op) {
    NvnRasterMethod methods[NVN_RASTER_METHODS_MAX];
    uint32_t count = 0;
    if (!nvn_raster_methods((NvnRasterKind)op->which, op->w, methods, &count)) {
        replay_fault(env, stats, "raster kind", op->which);
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) replay_method(env, stats, methods[i].method, methods[i].value);
    return 1;
}

static int replay_bind_vertex_attrib(const NvnReplayEnv* env, NvnReplayStats* stats,
                                     const NvnOpBindVertexAttrib* op) {
    if (!env->bind_vertex_attrib || !env->bind_vertex_attrib(env->ctx, op)) {
        replay_fault(env, stats, "bind vertex attrib", op->count);
        return 0;
    }
    return 1;
}

static int replay_bind_vertex_stream(const NvnReplayEnv* env, NvnReplayStats* stats,
                                     const NvnOpBindVertexStream* op) {
    if (!env->bind_vertex_stream || !env->bind_vertex_stream(env->ctx, op)) {
        replay_fault(env, stats, "bind vertex stream", op->count);
        return 0;
    }
    return 1;
}

static int replay_depth_range(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpDepthRange* op) {
    if (op->index >= NVN_VIEWPORTS_MAX) {
        replay_fault(env, stats, "depth range index", op->index);
        return 0;
    }
    float near_value, far_value;
    memcpy(&near_value, &op->near_bits, sizeof near_value);
    memcpy(&far_value, &op->far_bits, sizeof far_value);
    NvnDepthRangeMethod methods[NVN_DEPTH_RANGE_METHODS_MAX];
    nvn_depth_range_methods(op->index, op->depth_mode, near_value, far_value, methods);
    for (uint32_t i = 0; i < NVN_DEPTH_RANGE_METHODS_MAX; i++)
        replay_method(env, stats, methods[i].method, methods[i].value);
    return 1;
}

static int replay_render_targets(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpRenderTargets* op) {
    if (!env->render_targets(env->ctx, op)) {
        replay_fault(env, stats, "render targets", op->count);
        return 0;
    }
    return 1;
}

static int replay_gpu_op(const NvnReplayEnv* env, NvnReplayStats* stats, NvnOpCode code, const void* payload) {
    if (!env->gpu_op || !env->gpu_op(env->ctx, code, payload)) {
        replay_fault(env, stats, "gpu op", (uint64_t)code);
        return 0;
    }
    return 1;
}

static void replay_list_at(const NvnReplayEnv* env, const NvnOpList* list, NvnReplayStats* stats, uint32_t depth);

static int replay_call(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpCall* op, uint32_t depth) {
    const NvnOpList* target = env->find_list(env->ctx, op->handle);
    if (!target) {
        replay_fault(env, stats, "call dead", op->handle);
        return 0;
    }
    if (depth >= NVN_CALL_DEPTH_MAX) {
        replay_fault(env, stats, "call depth", op->handle);
        return 0;
    }
    replay_list_at(env, target, stats, depth + 1u);
    return 1;
}

static void replay_op(const NvnReplayEnv* env, NvnReplayStats* stats, const NvnOpHeader* header, const void* payload,
                      uint32_t depth) {
    int ok;
    switch (header->code) {
    case NVN_OP_SET_TEXTURE_POOL:
        if (!payload_matches(header, sizeof(NvnOpPool))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_pool(env, stats, NVN_OP_SET_TEXTURE_POOL, payload, NVN_REPLAY_TEX_POOL_HI, NVN_REPLAY_TEX_POOL_LO,
                         NVN_REPLAY_TEX_POOL_LIMIT);
        break;
    case NVN_OP_SET_SAMPLER_POOL:
        if (!payload_matches(header, sizeof(NvnOpPool))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_pool(env, stats, NVN_OP_SET_SAMPLER_POOL, payload, NVN_REPLAY_SAMPLER_POOL_HI,
                         NVN_REPLAY_SAMPLER_POOL_LO, NVN_REPLAY_SAMPLER_POOL_LIMIT);
        break;
    case NVN_OP_SET_RENDER_TARGETS:
        if (!payload_matches(header, sizeof(NvnOpRenderTargets))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_render_targets(env, stats, payload);
        break;
    case NVN_OP_SET_VIEWPORT:
        if (!payload_matches(header, sizeof(NvnOpViewport))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_viewport(env, stats, payload);
        break;
    case NVN_OP_SET_SCISSOR:
        if (!payload_matches(header, sizeof(NvnOpScissor))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_scissor(env, stats, payload);
        break;
    case NVN_OP_CLEAR_COLOR:
        if (!payload_matches(header, sizeof(NvnOpClearColor))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_clear(env, stats, payload);
        break;
    case NVN_OP_BARRIER:
        if (!payload_matches(header, sizeof(NvnOpBarrier))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_barrier(env, stats, payload);
        break;
    case NVN_OP_BIND_TEXTURE:
        if (!payload_matches(header, sizeof(NvnOpBindTexture))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_texture(env, stats, payload);
        break;
    case NVN_OP_BIND_BUFFER:
        if (!payload_matches(header, sizeof(NvnOpBindBuffer))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_buffer(env, stats, payload);
        break;
    case NVN_OP_BIND_STATE:
        if (!payload_matches(header, sizeof(NvnOpBindState))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_state(env, stats, payload);
        break;
    case NVN_OP_DRAW:
        if (!payload_matches(header, sizeof(NvnOpDraw))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_draw(env, stats, payload);
        break;
    case NVN_OP_DISPATCH:
        if (!payload_matches(header, sizeof(NvnOpDispatch))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_dispatch(env, stats, payload);
        break;
    case NVN_OP_BIND_PROGRAM:
        if (!payload_matches(header, sizeof(NvnOpBindProgram))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_program(env, stats, payload);
        break;
    case NVN_OP_SET_RASTER:
        if (!payload_matches(header, sizeof(NvnOpRaster))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_raster(env, stats, payload);
        break;
    case NVN_OP_BIND_VERTEX_ATTRIB:
        if (!payload_matches(header, sizeof(NvnOpBindVertexAttrib))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_vertex_attrib(env, stats, payload);
        break;
    case NVN_OP_BIND_VERTEX_STREAM:
        if (!payload_matches(header, sizeof(NvnOpBindVertexStream))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_bind_vertex_stream(env, stats, payload);
        break;
    case NVN_OP_SET_DEPTH_RANGE:
        if (!payload_matches(header, sizeof(NvnOpDepthRange))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_depth_range(env, stats, payload);
        break;
    case NVN_OP_CLEAR_DEPTH_STENCIL:
        if (!payload_matches(header, sizeof(NvnOpClearDepthStencil))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_CLEAR_DEPTH_STENCIL, payload);
        break;
    case NVN_OP_CLEAR_TEXTURE:
        if (!payload_matches(header, sizeof(NvnOpClearTexture))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_CLEAR_TEXTURE, payload);
        break;
    case NVN_OP_COPY_TEXTURE:
        if (!payload_matches(header, sizeof(NvnOpCopyTexture))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_COPY_TEXTURE, payload);
        break;
    case NVN_OP_REPORT_COUNTER:
        if (!payload_matches(header, sizeof(NvnOpReportCounter))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        if (((const NvnOpReportCounter*)payload)->type > NVN_REPORT_TYPE_MAX) {
            replay_fault(env, stats, "report type", ((const NvnOpReportCounter*)payload)->type);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_REPORT_COUNTER, payload);
        break;
    case NVN_OP_RESET_COUNTER: {
        uint32_t word;
        if (!payload_matches(header, sizeof(NvnOpResetCounter))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        if (!nvn_reset_counter_word(((const NvnOpResetCounter*)payload)->type, &word)) {
            replay_fault(env, stats, "counter type", ((const NvnOpResetCounter*)payload)->type);
            return;
        }
        replay_method(env, stats, NVN_RESET_COUNTER_METHOD, word);
        ok = 1;
        break;
    }
    case NVN_OP_TRANSFORM_FEEDBACK:
        if (!payload_matches(header, sizeof(NvnOpTransformFeedback))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        if (((const NvnOpTransformFeedback*)payload)->kind >= NVN_XFB_KIND_COUNT) {
            replay_fault(env, stats, "transform feedback kind", ((const NvnOpTransformFeedback*)payload)->kind);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_TRANSFORM_FEEDBACK, payload);
        break;
    case NVN_OP_BIND_IMAGE:
        if (!payload_matches(header, sizeof(NvnOpBindImage))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        if (((const NvnOpBindImage*)payload)->count > NVN_IMAGE_BINDS_MAX) {
            replay_fault(env, stats, "image count", ((const NvnOpBindImage*)payload)->count);
            return;
        }
        ok = replay_gpu_op(env, stats, NVN_OP_BIND_IMAGE, payload);
        break;
    case NVN_OP_CALL:
        if (!payload_matches(header, sizeof(NvnOpCall))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_call(env, stats, payload, depth);
        break;
    case NVN_OP_SET_STENCIL:
        if (!payload_matches(header, sizeof(NvnOpStencil))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_stencil(env, stats, payload);
        break;
    case NVN_OP_SET_BLEND_COLOR:
        if (!payload_matches(header, sizeof(NvnOpBlendColor))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_blend_color(env, stats, payload);
        break;
    case NVN_OP_SET_ALPHA_REF:
        if (!payload_matches(header, sizeof(NvnOpAlphaRef))) {
            replay_fault(env, stats, "op size", header->code);
            return;
        }
        ok = replay_alpha_ref(env, stats, payload);
        break;
    default:
        replay_fault(env, stats, "op code", header->code);
        return;
    }
    if (ok) stats->replayed[header->code]++;
}

static void replay_list_at(const NvnReplayEnv* env, const NvnOpList* list, NvnReplayStats* stats, uint32_t depth) {
    size_t pos = 0;
    NvnOpHeader header;
    const void* payload;
    while (nvn_oplist_next(list, &pos, &header, &payload)) replay_op(env, stats, &header, payload, depth);
}

void nvn_replay_list(const NvnReplayEnv* env, const NvnOpList* list, NvnReplayStats* stats) {
    replay_list_at(env, list, stats, 0);
}
