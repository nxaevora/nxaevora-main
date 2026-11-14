/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/nvn_rt.h"
#include <string.h>

/* Maxwell 3D methods and values of nvnCommandBufferSetRenderTargets (d855da0). */
#define NVN_RT_CONTROL_METHOD       0x487u
#define NVN_RT_CONTROL_BASE         0x0FAC6880u
#define NVN_RT_CONTROL_COUNT_MASK   0xFu
#define NVN_RT_COLOR_BLOCK_BASE     0x200u
#define NVN_RT_COLOR_DISABLE_BASE   0x204u
#define NVN_RT_COLOR_STRIDE         0x10u
#define NVN_RT_DEPTH_A              0x54Eu
#define NVN_RT_DEPTH_B              0xD28u
#define NVN_RT_DEPTH_ENABLE         1u
#define NVN_RT_DEPTH_BIND_BASE      0x3F8u
#define NVN_RT_DEPTH_SIZE_BASE      0x48Au
#define NVN_RT_CLIP_X               0x3FDu
#define NVN_RT_CLIP_Y               0x3FEu
#define NVN_RT_SAMPLE_MODE          0x574u

#define NVN_RT_WORDS_PER_TARGET     8u
#define NVN_RT_CLIP_MAX             0xFFFFu
#define NVN_RT_CLIP_SHIFT           16u
#define NVN_RT_TILE_NIBBLE          0xFu
#define NVN_RT_TILE_Y_SHIFT         4u
#define NVN_RT_TILE_Z_SHIFT         8u
#define NVN_RT_LAYER_STRIDE_SHIFT   2u
#define NVN_RT_SAMPLE_INDEX_MAX     7u
#define NVN_RT_SHIFT_MASK           31u
#define NVN_RT_DEPTH_WORD_MASK      0xFFFFu
#define NVN_RT_VIEW_FLAGS           (NVN_VIEW_LEVELS | NVN_VIEW_LAYERS | NVN_VIEW_FORMAT | \
                                     NVN_VIEW_SWIZZLE | NVN_VIEW_DEPTH_STENCIL_MODE | NVN_VIEW_TARGET)
#define NVN_RT_TILE_3D_BIT          0x10000u
#define NVN_RT_VIEW_TARGET_LAST     NVN_TEXTURE_TARGET_BUFFER

/* The fixed words and headers of nvnCommandBufferClearTexture (d857610, verify-s4-4b.md). */
#define NVN_RT_HEADER_INCREASING    0x20000000u /* mode 1: a run of count data words follows */
#define NVN_RT_HEADER_INLINE        0x80000000u /* mode 4: the value is in the count field */
#define NVN_RT_HEADER_INCREASE_ONCE 0xA0000000u /* mode 5: a macro of count data words follows */
#define NVN_RT_HEADER_COUNT_SHIFT   16u
#define NVN_RT_HEADER_VALUE_MAX     0x1FFFu
#define NVN_RT_CLEAR_OPEN           0x91080002u
#define NVN_RT_CLEAR_CLOSE          0x91000002u
#define NVN_RT_CLEAR_COLOR_METHOD   0x360u
#define NVN_RT_CLEAR_IMMD_METHOD    0x049u
#define NVN_RT_CLEAR_MACRO_METHOD   0xE1Eu
#define NVN_RT_CLEAR_MASK_METHOD    0x674u
#define NVN_RT_CLEAR_COLOR_WORDS    4u
#define NVN_RT_CLEAR_MACRO_WORDS    4u
#define NVN_RT_CLEAR_MASK_NIBBLE    0xFu
#define NVN_RT_CLEAR_MASK_SHIFT     2u
#define NVN_RT_CLEAR_FORMAT_SHIFT   24u
#define NVN_RT_CLEAR_LAYER_LIMIT    0xFFFFu

#define NVN_RT_CLEAR_RUN(method, count) \
    (NVN_RT_HEADER_INCREASING | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_CLEAR_VALUE(method, value) \
    (NVN_RT_HEADER_INLINE | (((value) & NVN_RT_HEADER_VALUE_MAX) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_CLEAR_MACRO(method, count) \
    (NVN_RT_HEADER_INCREASE_ONCE | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))

/* The fixed words and methods of nvnCommandBufferCopyTextureToTexture (d82bfd0, d82c5d0, verify-s4-4b.md). */
#define NVN_RT_COPY_OPEN             0x91030002u
#define NVN_RT_COPY_CLOSE            0x91000002u
#define NVN_RT_COPY_SRC_METHOD       0x608Cu
#define NVN_RT_COPY_SRC_SIZE_METHOD  0x6092u
#define NVN_RT_COPY_ADDRESS_METHOD   0x6094u
#define NVN_RT_COPY_DST_METHOD       0x6080u
#define NVN_RT_COPY_DST_SIZE_METHOD  0x6086u
#define NVN_RT_COPY_DST_ADDR_METHOD  0x6088u
#define NVN_RT_COPY_IMMD_METHOD      0x60B5u
#define NVN_RT_COPY_FLAGS_METHOD     0x6223u
#define NVN_RT_COPY_RECT_METHOD      0x622Cu
#define NVN_RT_COPY_SCALE_METHOD     0x6230u
#define NVN_RT_COPY_OFFSET_METHOD    0x6234u
#define NVN_RT_COPY_SRC_WORDS        4u
#define NVN_RT_COPY_DST_WORDS        5u
#define NVN_RT_COPY_PAIR_WORDS       2u
#define NVN_RT_COPY_RECT_WORDS       4u
#define NVN_RT_COPY_FLAGS_WORDS      1u
#define NVN_RT_COPY_IMMD_VALUE       1u
#define NVN_RT_COPY_FLAGS_MASK       0x1u   /* only bit0 is in this step */
#define NVN_RT_COPY_FLAGS_VALUE      0x10u
#define NVN_RT_COPY_FLAGS_BASE       1u
#define NVN_RT_COPY_RATIO_UNIT       0x8000
#define NVN_RT_COPY_RATIO_SHIFT      15u
#define NVN_RT_COPY_FRACTION_SHIFT   17u    /* 32 - NVN_RT_COPY_RATIO_SHIFT */
#define NVN_RT_COPY_RATIO_INT_SHIFT  12u
#define NVN_RT_COPY_ORIGIN_SHIFT     4u
#define NVN_RT_COPY_OFFSET_SHIFT     28u
#define NVN_RT_COPY_OFFSET_RIGHT     4u
#define NVN_RT_COPY_LAYER_LIMIT      0xFFFFu
#define NVN_RT_COPY_FORMAT_0X37      0x37u
#define NVN_RT_COPY_FORMAT_0X38      0x38u
#define NVN_RT_COPY_FORMAT_0X6D      0x6Du
#define NVN_RT_COPY_FORMAT_0X25      0x25u

#define NVN_RT_COPY_RUN(method, count) \
    (NVN_RT_HEADER_INCREASING | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_COPY_VALUE(method, value) \
    (NVN_RT_HEADER_INLINE | (((value) & NVN_RT_HEADER_VALUE_MAX) << NVN_RT_HEADER_COUNT_SHIFT) | (method))

typedef struct {
    uint64_t address;
    uint32_t width, height, depth, format, tile, layer_stride_words;
} NvnRtDepth;

typedef struct {
    uint64_t address;
    uint32_t depth_word;
    int target_3d;
} NvnRtViewResult;

typedef struct {
    uint32_t aligned_width, height, tile, depth_word;
} NvnRtCopySurface;

static uint32_t rt_align_up(uint32_t value, uint32_t multiple) {
    return ((value + multiple - 1u) / multiple) * multiple;
}

static uint32_t rt_clip_word(uint32_t minimum, uint32_t limit) {
    if (minimum < 1u || limit < 1u) return 0u;
    return (minimum < limit ? minimum : limit) << NVN_RT_CLIP_SHIFT;
}

/* One ClearTexture clip word: the origin in the low 16 bits and the span above it. The library compares the wrapped
   int32 values, so a negative origin moves the span start back. origin and span are already sample scaled. */
static uint32_t rt_clear_clip_word(uint32_t origin, uint32_t span, uint32_t limit) {
    int32_t p = (int32_t)origin;
    uint32_t low = p < 0 ? (uint32_t)p : 0u;          /* min(origin, 0) */
    uint32_t start = p < 0 ? 0u : origin;             /* max(origin, 0) */
    int32_t reached = (int32_t)(span + low);
    if (reached < 1 || (int32_t)(limit - start) <= 0) return 0u;
    int32_t clipped = (int32_t)(span + low + start) < (int32_t)limit ? reached : (int32_t)(limit - start);
    return start | ((uint32_t)clipped << NVN_RT_CLIP_SHIFT);
}

static uint32_t rt_sample_mode(uint32_t code) {
    static const uint8_t modes[8] = {5u, 2u, 0u, 4u, 0u, 0u, 0u, 6u};
    uint32_t index = ((code >> 1) | (code << 31)) - 1u;
    return index <= NVN_RT_SAMPLE_INDEX_MAX ? modes[index] : 0u;
}

/* d869d10: the tile row count of a block linear layout. A target below the tile size gets 1. */
static uint32_t rt_tile_factor(const NvnTextureLayout* layout, uint32_t width, uint32_t height, uint32_t depth) {
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t tile_x = layout->tile_x & NVN_RT_SHIFT_MASK;
    uint32_t tile_y = layout->tile_y & NVN_RT_SHIFT_MASK;
    uint32_t tile_z = layout->tile_z & NVN_RT_SHIFT_MASK;
    uint32_t factor = 1u << tile_x;
    if ((uint64_t)width < ((uint64_t)gob_blocks << tile_x)) return 1u;
    if ((uint64_t)height < ((uint64_t)NVN_TEXTURE_GOB_HEIGHT << tile_y)) return 1u;
    if ((depth >> tile_z) == 0u) return 1u;
    return factor;
}

static uint32_t rt_tile_word(const NvnTextureLayout* layout) {
    return ((uint32_t)layout->tile_x & NVN_RT_TILE_NIBBLE) |
           (((uint32_t)layout->tile_y & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Y_SHIFT) |
           (((uint32_t)layout->tile_z & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Z_SHIFT);
}

/* The checks that a color and a depth target share. *format_word is set when 1 is returned. */
static int rt_texture_checks(const NvnRtEnv* env, const NvnTexture* texture, const char* linear_fault,
                             uint32_t* format_word) {
    const NvnTextureLayout* layout = &texture->layout;
    if (texture->builder.target != NVN_TEXTURE_TARGET_2D && texture->builder.target != NVN_TEXTURE_TARGET_2D_ARRAY) {
        env->fault(env->ctx, "rt target", texture->builder.target);
        return 0;
    }
    if (texture->builder.levels > 1u) {
        env->fault(env->ctx, "rt levels", texture->builder.levels);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        env->fault(env->ctx, linear_fault, texture->builder.flags);
        return 0;
    }
    uint32_t word = nvn_rt_format_word(texture->builder.format);
    if (word == 0u) {
        env->fault(env->ctx, "rt format", texture->builder.format);
        return 0;
    }
    if (layout->block_width != 1u || layout->block_height != 1u) {
        env->fault(env->ctx, "rt block", texture->builder.format);
        return 0;
    }
    if (layout->bytes_per_block == 0u || layout->bytes_per_block > NVN_TEXTURE_GOB_WIDTH) {
        env->fault(env->ctx, "rt bytes per block", layout->bytes_per_block);
        return 0;
    }
    *format_word = word;
    return 1;
}

/* The address, the depth word and the 3D bit of a view, shared by the color and the depth target.
   The library reads LEVELS, LAYERS, FORMAT and TARGET. It does not read level_count, and it wraps
   when the base layer and the count pass the texture depth; the host refuses that case. A refusal
   writes no part of *out. */
static int rt_view_apply(const NvnRtEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                         NvnRtViewResult* out) {
    const NvnTextureLayout* layout = &texture->layout;
    uint64_t address = texture->address;
    uint32_t depth_word = layout->depth;
    uint32_t target = texture->builder.target;
    if (!view || view->flags == 0u) {
        out->address = address;
        out->depth_word = depth_word;
        out->target_3d = target == NVN_TEXTURE_TARGET_3D;
        return 1;
    }
    if (view->flags & (uint8_t)~NVN_RT_VIEW_FLAGS) {
        env->fault(env->ctx, "rt view flags", view->flags);
        return 0;
    }
    if ((view->flags & NVN_VIEW_LEVELS) && view->base_level != 0u) {
        env->fault(env->ctx, "rt view levels", view->base_level);
        return 0;
    }
    if (view->flags & NVN_VIEW_LAYERS) {
        if (view->layer_count != 0u) {
            uint64_t layers = (uint64_t)view->base_layer + view->layer_count;
            if (layers > layout->depth) {
                env->fault(env->ctx, "rt view layers", layers);
                return 0;
            }
            depth_word = view->layer_count;
        } else if (view->base_layer != 0u) {
            if (view->base_layer > layout->depth) {
                env->fault(env->ctx, "rt view layers", view->base_layer);
                return 0;
            }
            depth_word = layout->depth - view->base_layer;
        }
        uint64_t offset = (uint64_t)view->base_layer * layout->layer_stride;
        if (offset > UINT64_MAX - address) {
            env->fault(env->ctx, "rt view address", address);
            return 0;
        }
        address += offset;
    }
    if ((view->flags & NVN_VIEW_FORMAT) && view->format != texture->builder.format) {
        env->fault(env->ctx, "rt view format", view->format);
        return 0;
    }
    if (view->flags & NVN_VIEW_TARGET) target = view->target;
    out->address = address;
    out->depth_word = depth_word;
    out->target_3d = target == NVN_TEXTURE_TARGET_3D;
    return 1;
}

/* One target: check it and compute its eight words. The fault callback reports the first bad part. */
static int rt_target_words(const NvnRtEnv* env, const NvnRtColor* color, uint32_t word[NVN_RT_WORDS_PER_TARGET]) {
    const NvnTexture* texture = color->texture;
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt linear", &format_word)) return 0;
    if (color->view && (color->view->flags & NVN_VIEW_TARGET) &&
        color->view->target > NVN_RT_VIEW_TARGET_LAST) {
        env->fault(env->ctx, "rt view target", color->view->target);
        return 0;
    }
    NvnRtViewResult view;
    if (!rt_view_apply(env, texture, color->view, &view)) return 0;
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    word[0] = (uint32_t)(view.address >> 32);
    word[1] = (uint32_t)view.address;
    word[2] = rt_align_up(width, factor * gob_blocks);
    word[3] = height;
    word[4] = format_word;
    word[5] = rt_tile_word(layout) | (view.target_3d ? NVN_RT_TILE_3D_BIT : 0u);
    word[6] = view.depth_word;
    word[7] = (uint32_t)(layout->layer_stride >> NVN_RT_LAYER_STRIDE_SHIFT);
    return 1;
}

/* The depth surface: check it and compute its words. The view rules are the color rules; the depth
   word is the low 16 bits and the 3D tile bit has no depth part. */
static int rt_depth_words(const NvnRtEnv* env, const NvnRtColor* depth, NvnRtDepth* out) {
    const NvnTexture* texture = depth->texture;
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt depth linear", &format_word)) return 0;
    NvnRtViewResult view;
    if (!rt_view_apply(env, texture, depth->view, &view)) return 0;
    if (view.depth_word > NVN_RT_DEPTH_WORD_MASK) {
        env->fault(env->ctx, "rt depth layers", view.depth_word);
        return 0;
    }
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    out->address = view.address;
    out->width = rt_align_up(width, factor * gob_blocks);
    out->height = height;
    out->depth = view.depth_word & NVN_RT_DEPTH_WORD_MASK;
    out->format = format_word;
    out->tile = rt_tile_word(layout);
    out->layer_stride_words = (uint32_t)(layout->layer_stride >> NVN_RT_LAYER_STRIDE_SHIFT);
    return 1;
}

/* The depth bind words, the 0/1 pair, then the depth size words (order of d855da0). The library writes 0 only when
   there is no depth target. The depth enable meaning is an inference from that call order. */
static void rt_depth_emit(const NvnRtEnv* env, const NvnRtDepth* depth) {
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 0u, (uint32_t)(depth->address >> 32));
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 1u, (uint32_t)depth->address);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 2u, depth->format);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 3u, depth->tile);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 4u, depth->layer_stride_words);
    env->method(env->ctx, NVN_RT_DEPTH_A, 0u);
    env->method(env->ctx, NVN_RT_DEPTH_A, NVN_RT_DEPTH_ENABLE);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 0u, depth->width);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 1u, depth->height);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 2u, depth->depth);
}

int nvn_rt_emit(const NvnRtEnv* env, const NvnRtColor* color, uint32_t count, const NvnRtColor* depth,
                uint32_t clip_limit) {
    if (!env || !env->method || !env->fault) return 0;
    if (count > NVN_RT_COLOR_MAX) {
        env->fault(env->ctx, "rt count", count);
        return 0;
    }
    if (count > 0u && !color) {
        env->fault(env->ctx, "rt no color array", count);
        return 0;
    }
    uint32_t words[NVN_RT_COLOR_MAX][NVN_RT_WORDS_PER_TARGET];
    uint8_t present[NVN_RT_COLOR_MAX] = {0};
    uint32_t min_width = NVN_RT_CLIP_MAX;
    uint32_t min_height = NVN_RT_CLIP_MAX;
    uint32_t samples = 0;
    int has_color = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!color[i].texture) continue;
        if (!rt_target_words(env, &color[i], words[i])) return 0;
        present[i] = 1;
        has_color = 1;
        const NvnTextureLayout* layout = &color[i].texture->layout;
        if (layout->width < min_width) min_width = layout->width;
        if (layout->height < min_height) min_height = layout->height;
        samples = layout->samples;
    }
    NvnRtDepth depth_words;
    int has_depth = depth && depth->texture;
    if (has_depth) {
        if (!rt_depth_words(env, depth, &depth_words)) return 0;
        const NvnTextureLayout* layout = &depth->texture->layout;
        if (layout->width < min_width) min_width = layout->width;
        if (layout->height < min_height) min_height = layout->height;
        if (!has_color) samples = layout->samples;
    }
    uint32_t clip_x = rt_clip_word(min_width, clip_limit);
    uint32_t clip_y = rt_clip_word(min_height, clip_limit);
    uint32_t mode = rt_sample_mode(samples);
    uint32_t control = NVN_RT_CONTROL_BASE | (count & NVN_RT_CONTROL_COUNT_MASK);
    env->method(env->ctx, NVN_RT_CONTROL_METHOD, control);
    for (uint32_t i = 0; i < count; i++) {
        if (!present[i]) {
            env->method(env->ctx, NVN_RT_COLOR_DISABLE_BASE + NVN_RT_COLOR_STRIDE * i, 0u);
            continue;
        }
        for (uint32_t k = 0; k < NVN_RT_WORDS_PER_TARGET; k++)
            env->method(env->ctx, NVN_RT_COLOR_BLOCK_BASE + NVN_RT_COLOR_STRIDE * i + k, words[i][k]);
    }
    for (uint32_t j = count; j < NVN_RT_COLOR_MAX; j++)
        env->method(env->ctx, NVN_RT_COLOR_DISABLE_BASE + NVN_RT_COLOR_STRIDE * j, 0u);
    /* The dependent block (0x1F8, 0x572) is not written: its words depend on compression and zcull,
       which the host does not model, and only a device mode accepts no block (explorer section 4).
       replay_host_render_targets logs the omission once. */
    if (has_depth) {
        rt_depth_emit(env, &depth_words);
    } else {
        env->method(env->ctx, NVN_RT_DEPTH_A, 0u);
        env->method(env->ctx, NVN_RT_DEPTH_B, 0u);
    }
    env->method(env->ctx, NVN_RT_CLIP_X, clip_x);
    env->method(env->ctx, NVN_RT_CLIP_Y, clip_y);
    env->method(env->ctx, NVN_RT_SAMPLE_MODE, mode);
    return 1;
}

uint32_t nvn_rt_clear_texture_words(const NvnRtEnv* env, const NvnTexture* texture,
                                    const NvnTextureView* view, const uint32_t region[6],
                                    const uint32_t color[4], uint32_t mask, uint32_t clip_limit,
                                    int* nothing, uint32_t* out) {
    if (nothing) *nothing = 0;
    if (!env || !env->fault || !out || !nothing || !texture || !region || !color) return 0;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt clear linear", &format_word)) return 0;
    uint32_t layers = region[5];
    uint32_t width_count = region[3];
    uint32_t rows = region[4];
    if (layers == 0u || width_count == 0u || rows == 0u) {
        *nothing = 1;
        return 0;
    }
    if (layers > NVN_RT_CLEAR_LAYERS_MAX) {
        env->fault(env->ctx, "clear layers", layers);
        return 0;
    }

    const NvnTextureLayout* layout = &texture->layout;
    uint32_t view_base = (view && (view->flags & NVN_VIEW_LAYERS)) ? view->base_layer : 0u;
    uint32_t base_level = (view && (view->flags & NVN_VIEW_LEVELS)) ? view->base_level : 0u;
    NvnTextureView layer_view = {0};
    layer_view.flags = NVN_VIEW_LEVELS | NVN_VIEW_LAYERS;
    layer_view.base_level = (uint8_t)base_level;
    layer_view.level_count = 1;
    layer_view.layer_count = 1;

    /* The addresses are made first, so that a late refusal writes no part of out. */
    uint32_t layer_arg1[NVN_RT_CLEAR_LAYERS_MAX];
    for (uint32_t i = 0; i < layers; i++) {
        uint64_t layer = (uint64_t)region[2] + view_base + i;
        if (layer > NVN_RT_CLEAR_LAYER_LIMIT) {
            env->fault(env->ctx, "clear layers", layer);
            return 0;
        }
        layer_view.base_layer = (uint16_t)layer;
        NvnRtViewResult result;
        if (!rt_view_apply(env, texture, &layer_view, &result)) return 0;
        layer_arg1[i] = (uint32_t)(result.address >> 8);
    }

    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    uint32_t aligned_width = rt_align_up(width, factor * gob_blocks);
    uint32_t surface = rt_tile_word(layout) | ((format_word & 0xFFu) << NVN_RT_CLEAR_FORMAT_SHIFT);
    uint32_t clip_x = rt_clear_clip_word(layout->sample_x * region[0], layout->sample_x * width_count,
                                         clip_limit);
    uint32_t clip_y = rt_clear_clip_word(layout->sample_y * region[1], layout->sample_y * rows, clip_limit);
    uint32_t mask_word = (mask & NVN_RT_CLEAR_MASK_NIBBLE) << NVN_RT_CLEAR_MASK_SHIFT;

    uint32_t at = 0;
    out[at++] = NVN_RT_CLEAR_OPEN;
    out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLEAR_COLOR_METHOD, NVN_RT_CLEAR_COLOR_WORDS);
    for (uint32_t i = 0; i < NVN_RT_CLEAR_COLOR_WORDS; i++) out[at++] = color[i];
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 2u);
    for (uint32_t i = 0; i < layers; i++) {
        out[at++] = NVN_RT_CLEAR_MACRO(NVN_RT_CLEAR_MACRO_METHOD, NVN_RT_CLEAR_MACRO_WORDS);
        out[at++] = layer_arg1[i];
        out[at++] = aligned_width;
        out[at++] = height;
        out[at++] = surface;
        out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLIP_X, 2u);
        out[at++] = clip_x;
        out[at++] = clip_y;
        out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLEAR_MASK_METHOD, 1u);
        out[at++] = mask_word;
    }
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 3u);
    out[at++] = NVN_RT_CLEAR_MACRO(NVN_RT_CLEAR_MACRO_METHOD, NVN_RT_CLEAR_MACRO_WORDS);
    out[at++] = layer_arg1[layers - 1u];
    out[at++] = aligned_width;
    out[at++] = height;
    out[at++] = surface;
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 1u);
    out[at++] = NVN_RT_CLEAR_CLOSE;
    return at;
}

/* The checks that the two copy surfaces share. The format word is read after the copy format change, so it is not
   part of these checks: rt_texture_checks reads the render target column +0x1C and the copy takes +0x18. */
static int rt_copy_checks(const NvnRtCopyEnv* env, const NvnTexture* texture) {
    const NvnTextureLayout* layout = &texture->layout;
    if (texture->builder.target != NVN_TEXTURE_TARGET_2D &&
        texture->builder.target != NVN_TEXTURE_TARGET_2D_ARRAY) {
        env->base.fault(env->base.ctx, "copy target", texture->builder.target);
        return 0;
    }
    if (texture->builder.levels > 1u) {
        env->base.fault(env->base.ctx, "copy levels", texture->builder.levels);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        env->base.fault(env->base.ctx, "copy linear", texture->builder.flags);
        return 0;
    }
    if (layout->block_width != 1u || layout->block_height != 1u) {
        env->base.fault(env->base.ctx, "copy block", texture->builder.format);
        return 0;
    }
    if (layout->bytes_per_block == 0u || layout->bytes_per_block > NVN_TEXTURE_GOB_WIDTH) {
        env->base.fault(env->base.ctx, "copy bytes per block", layout->bytes_per_block);
        return 0;
    }
    return 1;
}

/* The size and tile words of one copy surface. The width and height use the sample scale as in ClearTexture. */
static void rt_copy_surface(const NvnTexture* texture, NvnRtCopySurface* out) {
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    uint32_t tile_depth = 1u << (layout->tile_z & NVN_RT_SHIFT_MASK);
    out->aligned_width = rt_align_up(width, factor * gob_blocks);
    out->height = height;
    out->tile = rt_tile_word(layout);
    out->depth_word = layout->depth < tile_depth ? layout->depth : tile_depth;
}

/* (int32_t)(extent << 15) / divisor (SDIV). The INT32_MIN / -1 case is undefined in C and is refused here. */
static int rt_copy_ratio(const NvnRtCopyEnv* env, uint32_t extent, uint32_t divisor, int32_t* out) {
    int32_t value = (int32_t)(extent << NVN_RT_COPY_RATIO_SHIFT);
    int32_t div = (int32_t)divisor;
    if (value == INT32_MIN && div == -1) {
        env->base.fault(env->base.ctx, "copy ratio", divisor);
        return 0;
    }
    *out = value / div;
    return 1;
}

uint32_t nvn_rt_copy_texture_words(const NvnRtCopyEnv* env, const NvnTexture* src,
                                   const NvnTextureView* src_view, const NvnTexture* dst,
                                   const NvnTextureView* dst_view, const uint32_t src_region[6],
                                   const uint32_t dst_region[6], uint32_t flags, int* nothing,
                                   uint32_t* out) {
    if (nothing) *nothing = 0;
    if (!env || !env->base.fault || !env->format_word || !out || !nothing || !src || !dst ||
        !src_region || !dst_region) return 0;
    uint32_t layers = dst_region[5];
    /* The library returns before the start word when the destination region is empty (explorer section 7). */
    if (dst_region[3] == 0u || dst_region[4] == 0u || layers == 0u) {
        *nothing = 1;
        return 0;
    }
    if (!rt_copy_checks(env, src) || !rt_copy_checks(env, dst)) return 0;
    if (flags & ~NVN_RT_COPY_FLAGS_MASK) {
        env->base.fault(env->base.ctx, "copy flags", flags);
        return 0;
    }
    if (layers > NVN_RT_COPY_LAYERS_MAX) {
        env->base.fault(env->base.ctx, "copy layers", layers);
        return 0;
    }
    int32_t ratio_x = 0, ratio_y = 0;
    if (!rt_copy_ratio(env, src_region[3], dst_region[3], &ratio_x) ||
        !rt_copy_ratio(env, src_region[4], dst_region[4], &ratio_y)) return 0;

    /* The format change of item 5, then the +0x18 word of each index (explorer sections 2 and 5).
       The caller reads the format from the view when the view has one, else from the texture. */
    uint32_t src_format = (src_view && (src_view->flags & NVN_VIEW_FORMAT)) ? src_view->format
                                                                           : src->builder.format;
    uint32_t dst_format = (dst_view && (dst_view->flags & NVN_VIEW_FORMAT)) ? dst_view->format
                                                                           : dst->builder.format;
    if (src_format == dst_format &&
        (!(flags & NVN_RT_COPY_FLAGS_MASK) ||
         (ratio_x == NVN_RT_COPY_RATIO_UNIT && ratio_y == NVN_RT_COPY_RATIO_UNIT))) {
        if (src_format == NVN_RT_COPY_FORMAT_0X38) {
            src_format = NVN_RT_COPY_FORMAT_0X25;
            dst_format = NVN_RT_COPY_FORMAT_0X25;
        } else if (src_format == NVN_RT_COPY_FORMAT_0X37) {
            src_format = NVN_RT_COPY_FORMAT_0X6D;
            dst_format = NVN_RT_COPY_FORMAT_0X6D;
        }
    }
    uint32_t src_format_word = env->format_word(env->base.ctx, src_format);
    if (src_format_word == 0u) {
        env->base.fault(env->base.ctx, "copy format", src_format);
        return 0;
    }
    uint32_t dst_format_word = env->format_word(env->base.ctx, dst_format);
    if (dst_format_word == 0u) {
        env->base.fault(env->base.ctx, "copy format", dst_format);
        return 0;
    }

    /* The layer addresses are made first, so that a late refusal writes no part of out. */
    uint32_t src_high[NVN_RT_COPY_LAYERS_MAX], src_low[NVN_RT_COPY_LAYERS_MAX];
    uint32_t dst_high[NVN_RT_COPY_LAYERS_MAX], dst_low[NVN_RT_COPY_LAYERS_MAX];
    uint32_t src_view_base = (src_view && (src_view->flags & NVN_VIEW_LAYERS)) ? src_view->base_layer : 0u;
    uint32_t dst_view_base = (dst_view && (dst_view->flags & NVN_VIEW_LAYERS)) ? dst_view->base_layer : 0u;
    int32_t step = (int32_t)src_region[5] / (int32_t)layers;
    NvnTextureView src_layer = {0};
    NvnTextureView dst_layer = {0};
    src_layer.flags = dst_layer.flags = NVN_VIEW_LEVELS | NVN_VIEW_LAYERS;
    src_layer.level_count = dst_layer.level_count = 1u;
    src_layer.layer_count = dst_layer.layer_count = 1u;
    src_layer.base_level =
        (uint8_t)((src_view && (src_view->flags & NVN_VIEW_LEVELS)) ? src_view->base_level : 0u);
    dst_layer.base_level =
        (uint8_t)((dst_view && (dst_view->flags & NVN_VIEW_LEVELS)) ? dst_view->base_level : 0u);
    for (uint32_t i = 0; i < layers; i++) {
        uint64_t src_position =
            (uint64_t)src_region[2] + src_view_base + (uint64_t)((int64_t)step * (int64_t)i);
        uint64_t dst_position = (uint64_t)dst_region[2] + dst_view_base + i;
        if (src_position > NVN_RT_COPY_LAYER_LIMIT || dst_position > NVN_RT_COPY_LAYER_LIMIT) {
            env->base.fault(env->base.ctx, "copy layers",
                            src_position > NVN_RT_COPY_LAYER_LIMIT ? src_position : dst_position);
            return 0;
        }
        src_layer.base_layer = (uint16_t)src_position;
        dst_layer.base_layer = (uint16_t)dst_position;
        NvnRtViewResult src_result, dst_result;
        if (!rt_view_apply(&env->base, src, &src_layer, &src_result)) return 0;
        if (!rt_view_apply(&env->base, dst, &dst_layer, &dst_result)) return 0;
        src_high[i] = (uint32_t)(src_result.address >> 32);
        src_low[i] = (uint32_t)src_result.address;
        dst_high[i] = (uint32_t)(dst_result.address >> 32);
        dst_low[i] = (uint32_t)dst_result.address;
    }

    NvnRtCopySurface src_surface, dst_surface;
    rt_copy_surface(src, &src_surface);
    rt_copy_surface(dst, &dst_surface);
    uint32_t w5 = (uint32_t)(ratio_x >> NVN_RT_COPY_RATIO_INT_SHIFT) + (src_region[0] << NVN_RT_COPY_ORIGIN_SHIFT);
    uint32_t w6 = (uint32_t)(ratio_y >> NVN_RT_COPY_RATIO_INT_SHIFT) + (src_region[1] << NVN_RT_COPY_ORIGIN_SHIFT);

    uint32_t at = 0;
    out[at++] = NVN_RT_COPY_OPEN;
    for (uint32_t i = 0; i < layers; i++) {
        if (i == 0u) {
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_METHOD, NVN_RT_COPY_SRC_WORDS);
            out[at++] = src_format_word;
            out[at++] = 0u;
            out[at++] = src_surface.tile;
            out[at++] = src_surface.depth_word;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_surface.aligned_width;
            out[at++] = src_surface.height;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_high[i];
            out[at++] = src_low[i];
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_METHOD, NVN_RT_COPY_DST_WORDS);
            out[at++] = dst_format_word;
            out[at++] = 0u;
            out[at++] = dst_surface.tile;
            out[at++] = dst_surface.depth_word;
            out[at++] = 0u;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_surface.aligned_width;
            out[at++] = dst_surface.height;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_high[i];
            out[at++] = dst_low[i];
            out[at++] = NVN_RT_COPY_VALUE(NVN_RT_COPY_IMMD_METHOD, NVN_RT_COPY_IMMD_VALUE);
        } else {
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_high[i];
            out[at++] = src_low[i];
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_high[i];
            out[at++] = dst_low[i];
        }
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_FLAGS_METHOD, NVN_RT_COPY_FLAGS_WORDS);
        out[at++] = ((flags & NVN_RT_COPY_FLAGS_MASK) ? NVN_RT_COPY_FLAGS_VALUE : 0u) |
                    NVN_RT_COPY_FLAGS_BASE;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_RECT_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = dst_region[0];
        out[at++] = dst_region[1];
        out[at++] = dst_region[3];
        out[at++] = dst_region[4];
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SCALE_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = (uint32_t)ratio_x << NVN_RT_COPY_FRACTION_SHIFT;
        out[at++] = (uint32_t)(ratio_x >> NVN_RT_COPY_RATIO_SHIFT);
        out[at++] = (uint32_t)ratio_y << NVN_RT_COPY_FRACTION_SHIFT;
        out[at++] = (uint32_t)(ratio_y >> NVN_RT_COPY_RATIO_SHIFT);
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_OFFSET_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = w5 << NVN_RT_COPY_OFFSET_SHIFT;
        out[at++] = (uint32_t)((int32_t)w5 >> NVN_RT_COPY_OFFSET_RIGHT);
        out[at++] = w6 << NVN_RT_COPY_OFFSET_SHIFT;
        out[at++] = (uint32_t)((int32_t)w6 >> NVN_RT_COPY_OFFSET_RIGHT);
    }
    out[at++] = NVN_RT_COPY_CLOSE;
    return at;
}

/* ---- CopyBufferToTexture and CopyTextureToBuffer: the DMA route of d82b0f0 (nvn-unobserved.md, U3 item 1).
   A launch copies one row stack of one layer. MIRROR_Y makes one launch for each row and MIRROR_Z reverses the
   layers on the buffer side: dma.c keeps the pitch as u32, so the negative pitch of the library is not possible. */

#define NVN_DMA_METHOD_OFFSET     0x100u /* OffsetIn upper/lower and OffsetOut upper/lower */
#define NVN_DMA_METHOD_SRC_PITCH  0x104u /* PitchIn */
#define NVN_DMA_METHOD_DST_PITCH  0x105u /* PitchOut */
#define NVN_DMA_METHOD_LINE       0x106u /* LineLengthIn, LineCount */
#define NVN_DMA_METHOD_DST_PARAMS 0x1C3u /* block size, width, height, depth, layer, origin */
#define NVN_DMA_METHOD_SRC_PARAMS 0x1CAu
#define NVN_DMA_METHOD_LAUNCH     0x0C0u
#define NVN_DMA_OFFSET_WORDS      4u
#define NVN_DMA_PITCH_WORDS       1u
#define NVN_DMA_LINE_WORDS        2u
#define NVN_DMA_PARAMS_WORDS      6u
#define NVN_DMA_SUBCHANNEL        4u
#define NVN_DMA_COPY_TYPE         2u
#define NVN_DMA_FLUSH             0x4u
#define NVN_DMA_LAYOUT_PITCH      0x80u /* source bit 7 */
#define NVN_DMA_LAYOUT_BLOCK      0u
#define NVN_DMA_LAYOUT_DST_PITCH  0x100u /* destination bit 8 */
#define NVN_DMA_MULTI_LINE        0x200u /* bit 9: LineCount rows */
#define NVN_DMA_GOB_HEIGHT_FERMI  0x1000u /* block size bits 12-15: 1 = 8-row GOB, the library ORs it (d82b34c, d82b3d0) */
#define NVN_RT_SUBCHANNEL_SHIFT   13u    /* the method word header, the pushbuffer header's subchannel field */
#define NVN_RT_ORIGIN_MASK        0xFFFFu

typedef struct {
    const NvnTexture* texture;
    int is_3d;
    int linear;
    int multisample;
    uint32_t row_bytes;          /* region row bytes after whole blocks */
    uint32_t rows;               /* region rows in block rows */
    uint32_t layers;
    uint32_t x_bytes;            /* first byte of the row on the texture side */
    uint32_t y_first;            /* first block row of the level */
    uint32_t region_layer;       /* the region's first layer word */
    uint32_t layer_first;        /* first layer or slice, view base included */
    uint32_t surface_row_bytes;  /* the block linear surface row, padded */
    uint32_t surface_rows;
    uint32_t surface_depth;
    uint32_t block_size;
    uint32_t tile;
    uint32_t depth_word;
    uint32_t level_width;        /* level size in texels, before the sample scale */
    uint32_t level_height;
    uint32_t aligned_width;      /* level size for the 2D block */
    uint32_t aligned_height;
    uint64_t level_offset;
    uint64_t row_stride;         /* the buffer row stride */
    uint64_t image_stride;       /* the buffer image stride */
    uint64_t tex_pitch;          /* the texture side pitch, a linear texture */
} NvnRtCopyPlan;

static uint32_t rt_mip_dimension(uint32_t n, uint32_t level) {
    n >>= level & 31u;
    return n ? n : 1u;
}
static uint32_t rt_ceil_div(uint32_t n, uint32_t d) { return (n + d - 1u) / d; }
static int rt_add_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (a > UINT64_MAX - b) return 0;
    *out = a + b;
    return 1;
}
static int rt_mul_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (a && b > UINT64_MAX / a) return 0;
    *out = a * b;
    return 1;
}

/* The level geometry of a block linear texture, also for a multisample texture (its level is level 0). 0 after a
   fault: the texture has no block level at this base level. */
static int rt_copy_level(const NvnRtCopyEnv* env, const NvnTexture* texture, uint32_t base_level,
                         NvnTextureLevel* level) {
    const NvnTextureLayout* l = &texture->layout;
    if (l->sample_x == 1u && l->sample_y == 1u) {
        if (!nvn_texture_level(texture, base_level, level)) {
            env->base.fault(env->base.ctx, "copy level", base_level);
            return 0;
        }
        return 1;
    }
    memset(level, 0, sizeof *level);
    uint32_t width = rt_mip_dimension(l->width, base_level) * l->sample_x;
    uint32_t height = rt_mip_dimension(l->height, base_level) * l->sample_y;
    uint32_t blocks = rt_ceil_div(width, l->block_width);
    uint32_t row_bytes = blocks * l->bytes_per_block;
    uint32_t factor = rt_tile_factor(l, width, height, l->depth);
    level->gobs_x = rt_align_up(rt_ceil_div(row_bytes, NVN_TEXTURE_GOB_WIDTH), factor);
    level->rows = rt_ceil_div(height, l->block_height);
    level->depth = l->dim == 3u ? l->depth : 1u;
    level->block_height_log2 = l->tile_y & NVN_RT_SHIFT_MASK;
    level->depth_log2 = l->tile_z & NVN_RT_SHIFT_MASK;
    return 1;
}

/* The geometry and the checks of one texture copy. The buffer holds the region only: its rows are row_stride
   bytes apart and the first byte is the start. 0 after a fault; *nothing is 1 for an empty region. */
static int rt_copy_plan(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                        const uint32_t region[6], uint32_t row_stride, uint32_t image_stride,
                        NvnRtCopyPlan* p, int* nothing) {
    memset(p, 0, sizeof *p);
    *nothing = 0;
    const NvnTextureLayout* l = &texture->layout;
    uint32_t target = view && (view->flags & NVN_VIEW_TARGET) ? view->target : texture->builder.target;
    int one_d_array = target == NVN_TEXTURE_TARGET_1D_ARRAY;
    uint32_t x = region[0];
    uint32_t y = one_d_array ? 0u : region[1];
    uint32_t layer = one_d_array ? region[1] : region[2];
    uint32_t width = region[3];
    uint32_t height = one_d_array ? 1u : region[4];
    uint32_t layers = one_d_array ? region[4] : region[5];
    if (!width || !height || !layers) {
        *nothing = 1;
        return 0;
    }
    if (target == NVN_TEXTURE_TARGET_BUFFER) {
        env->base.fault(env->base.ctx, "copy target", target);
        return 0;
    }
    uint32_t base_level = view && (view->flags & NVN_VIEW_LEVELS) ? view->base_level : 0u;
    uint32_t base_layer = view && (view->flags & NVN_VIEW_LAYERS) ? view->base_layer : 0u;
    p->texture = texture;
    p->linear = (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) != 0u;
    p->multisample = l->sample_x != 1u || l->sample_y != 1u;

    p->level_width = rt_mip_dimension(l->width, base_level);
    p->level_height = rt_mip_dimension(l->height, base_level);
    if ((uint64_t)x + width > p->level_width || (uint64_t)y + height > p->level_height) {
        env->base.fault(env->base.ctx, "copy region", texture->address);
        return 0;
    }
    uint64_t max_layers;
    if (p->linear) {
        max_layers = l->depth ? l->depth : 1u;
    } else {
        NvnTextureLevel level;
        if (!rt_copy_level(env, texture, base_level, &level)) return 0;
        p->is_3d = l->dim == 3u;
        max_layers = p->is_3d ? level.depth : (l->depth ? l->depth : 1u);
        p->surface_row_bytes = level.gobs_x * NVN_TEXTURE_GOB_WIDTH;
        p->surface_rows = level.rows;
        p->surface_depth = p->is_3d ? level.depth : 1u;
        p->block_size = ((uint32_t)level.block_height_log2 << 4) |
                        ((uint32_t)level.depth_log2 << 8) | NVN_DMA_GOB_HEIGHT_FERMI;
        p->tile = rt_tile_word(l);
        uint32_t tile_depth = 1u << (l->tile_z & NVN_RT_SHIFT_MASK);
        p->depth_word = (l->depth ? l->depth : 1u) < tile_depth ? (l->depth ? l->depth : 1u) : tile_depth;
        uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / l->bytes_per_block;
        uint32_t factor = rt_tile_factor(l, p->level_width * l->sample_x, p->level_height * l->sample_y,
                                         l->depth);
        p->aligned_width = rt_align_up(p->level_width * l->sample_x, factor * gob_blocks);
        p->aligned_height = p->level_height * l->sample_y;
        uint64_t view_offset = 0;
        if (!nvn_texture_view_offset(texture, view, &view_offset)) {
            env->base.fault(env->base.ctx, "copy level", base_level);
            return 0;
        }
        p->level_offset = p->is_3d ? level.offset : view_offset;
    }
    if (p->linear) {
        p->level_offset = 0u;
        p->surface_row_bytes = l->linear_stride;
        p->surface_rows = p->level_height;
        p->surface_depth = 1u;
        p->tile = 0u;
        p->depth_word = 1u;
        p->aligned_width = p->level_width;
        p->aligned_height = p->level_height;
        p->tex_pitch = l->linear_stride ? l->linear_stride : (uint64_t)p->level_width * l->bytes_per_block;
    }
    if ((uint64_t)layer + layers > max_layers) {
        env->base.fault(env->base.ctx, "copy layers", layer);
        return 0;
    }
    p->layers = layers;
    p->region_layer = layer;
    p->layer_first = base_layer + layer;
    uint32_t bw = l->block_width ? l->block_width : 1u;
    uint32_t bh = l->block_height ? l->block_height : 1u;
    p->x_bytes = rt_ceil_div(x, bw) * l->bytes_per_block;
    p->row_bytes = rt_ceil_div(width, bw) * l->bytes_per_block;
    p->y_first = rt_ceil_div(y, bh);
    p->rows = rt_ceil_div(height, bh);
    if (p->linear) {
        p->x_bytes = x * l->bytes_per_block;
        p->row_bytes = width * l->bytes_per_block;
        p->y_first = y;
        p->rows = height;
    }
    /* The DMA origin holds 16 bits for x bytes and y rows: a larger start cannot be addressed. */
    if (p->x_bytes > NVN_RT_ORIGIN_MASK || p->y_first + p->rows - 1u > NVN_RT_ORIGIN_MASK) {
        env->base.fault(env->base.ctx, "copy origin", p->x_bytes);
        return 0;
    }
    uint64_t tight_image;
    if (row_stride && row_stride < p->row_bytes) {
        env->base.fault(env->base.ctx, "copy row stride", row_stride);
        return 0;
    }
    p->row_stride = row_stride ? row_stride : p->row_bytes;
    if (!rt_mul_u64(p->row_stride, p->rows, &tight_image)) {
        env->base.fault(env->base.ctx, "copy image stride", image_stride);
        return 0;
    }
    if (image_stride && image_stride < tight_image) {
        env->base.fault(env->base.ctx, "copy image stride", image_stride);
        return 0;
    }
    p->image_stride = image_stride ? image_stride : tight_image;
    return 1;
}

int nvn_rt_copy_dma_check(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                          uint64_t buffer, const uint32_t region[6], uint32_t row_stride, uint32_t image_stride,
                          int to_texture, int* nothing) {
    (void)to_texture;
    if (nothing) *nothing = 0;
    if (!env || !env->base.fault || !nothing || !texture || !region) return 0;
    NvnRtCopyPlan p;
    if (!rt_copy_plan(env, texture, view, region, row_stride, image_stride, &p, nothing)) return 0;
    /* The last byte of the buffer: (layers - 1) image strides, (rows - 1) row strides and one row. */
    uint64_t span = 0, part = 0, end = 0;
    if (!rt_mul_u64((uint64_t)(p.layers - 1u), p.image_stride, &span) ||
        !rt_mul_u64((uint64_t)(p.rows - 1u), p.row_stride, &part) ||
        !rt_add_u64(span, part, &span) || !rt_add_u64(span, p.row_bytes, &span) ||
        !rt_add_u64(buffer, span, &end)) {
        env->base.fault(env->base.ctx, "copy buffer range", buffer);
        return 0;
    }
    return 1;
}

/* The words of one DMA side: the block parameters of a block linear side, or the pitch word of a pitch side. */
static uint32_t dma_side_words(uint32_t* out, uint32_t at, uint32_t block_method, uint32_t pitch_method,
                               int is_block, uint32_t pitch, const uint32_t block_params[NVN_DMA_PARAMS_WORDS]) {
    if (is_block) {
        out[at++] = NVN_RT_HEADER_INCREASING | (NVN_DMA_PARAMS_WORDS << NVN_RT_HEADER_COUNT_SHIFT) |
                    (NVN_DMA_SUBCHANNEL << NVN_RT_SUBCHANNEL_SHIFT) | block_method;
        for (uint32_t i = 0; i < NVN_DMA_PARAMS_WORDS; i++) out[at++] = block_params[i];
    } else {
        out[at++] = NVN_RT_HEADER_INCREASING | (NVN_DMA_PITCH_WORDS << NVN_RT_HEADER_COUNT_SHIFT) |
                    (NVN_DMA_SUBCHANNEL << NVN_RT_SUBCHANNEL_SHIFT) | pitch_method;
        out[at++] = pitch;
    }
    return at;
}

uint32_t nvn_rt_copy_dma_words(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                               uint64_t buffer, const uint32_t region[6], uint32_t flags,
                               uint32_t row_stride, uint32_t image_stride, int to_texture,
                               uint32_t launch_index, uint32_t* launch_count, uint32_t* out, uint32_t cap) {
    if (launch_count) *launch_count = 0;
    if (!env || !env->base.fault || !launch_count || !out || !texture || !region) return 0;
    int nothing = 0;
    NvnRtCopyPlan p;
    if (!rt_copy_plan(env, texture, view, region, row_stride, image_stride, &p, &nothing)) return 0;
    if (nothing) return 0;
    int mirror_y = (flags & NVN_COPY_FLAG_MIRROR_Y) != 0;
    int mirror_z = (flags & NVN_COPY_FLAG_MIRROR_Z) != 0;
    uint32_t launches_per_layer = mirror_y ? p.rows : 1u;
    uint64_t total = (uint64_t)p.layers * launches_per_layer;
    if (total > UINT32_MAX) {
        env->base.fault(env->base.ctx, "copy launches", total);
        return 0;
    }
    *launch_count = (uint32_t)total;
    if (launch_index >= *launch_count || cap < NVN_RT_DMA_LAUNCH_WORDS) return 0;
    uint32_t layer_index = launch_index / launches_per_layer;
    uint32_t row_part = launch_index % launches_per_layer;
    uint32_t buffer_layer = mirror_z ? p.layers - 1u - layer_index : layer_index;
    uint32_t line_rows = mirror_y ? 1u : p.rows;
    uint32_t origin_y = p.y_first + (mirror_y ? row_part : 0u);
    uint64_t buffer_offset;
    if (!rt_mul_u64(buffer_layer, p.image_stride, &buffer_offset)) {
        env->base.fault(env->base.ctx, "copy buffer range", buffer);
        return 0;
    }
    uint64_t row_offset = mirror_y ? (uint64_t)(p.rows - 1u - row_part) * p.row_stride : 0u;
    if (!rt_add_u64(buffer_offset, row_offset, &buffer_offset) ||
        !rt_add_u64(buffer_offset, buffer, &buffer_offset)) {
        env->base.fault(env->base.ctx, "copy buffer range", buffer);
        return 0;
    }

    uint64_t texture_offset;
    uint32_t layer_field = 0;
    if (p.linear) {
        uint64_t layer_offset;
        if (!rt_mul_u64(p.layer_first + layer_index, texture->layout.layer_stride, &layer_offset) ||
            !rt_add_u64(texture->address, layer_offset, &layer_offset) ||
            !rt_mul_u64(origin_y, p.tex_pitch, &texture_offset) ||
            !rt_add_u64(layer_offset, texture_offset, &texture_offset) ||
            !rt_add_u64(texture_offset, p.x_bytes, &texture_offset)) {
            env->base.fault(env->base.ctx, "copy address", texture->address);
            return 0;
        }
    } else if (p.is_3d) {
        texture_offset = texture->address + p.level_offset;
        layer_field = p.layer_first + layer_index;
    } else {
        uint64_t layer_offset;
        if (!rt_mul_u64(p.region_layer + layer_index, texture->layout.layer_stride, &layer_offset) ||
            !rt_add_u64(texture->address + p.level_offset, layer_offset, &texture_offset)) {
            env->base.fault(env->base.ctx, "copy address", texture->address);
            return 0;
        }
    }

    uint64_t linear_addr = buffer_offset;
    uint64_t block_addr = texture_offset;
    uint64_t in_addr = to_texture ? linear_addr : block_addr;
    uint64_t out_addr = to_texture ? block_addr : linear_addr;
    uint32_t linear_pitch = (uint32_t)p.row_stride;
    uint32_t texture_pitch = p.linear ? (uint32_t)p.tex_pitch : (uint32_t)p.row_stride;
    uint32_t src_pitch = to_texture ? linear_pitch : texture_pitch;
    uint32_t dst_pitch = to_texture ? texture_pitch : linear_pitch;
    int src_block = to_texture ? 0 : !p.linear;
    int dst_block = to_texture ? !p.linear : 0;
    uint32_t launch = NVN_DMA_COPY_TYPE | NVN_DMA_FLUSH | NVN_DMA_MULTI_LINE |
                      (src_block ? NVN_DMA_LAYOUT_BLOCK : NVN_DMA_LAYOUT_PITCH) |
                      (dst_block ? NVN_DMA_LAYOUT_BLOCK : NVN_DMA_LAYOUT_DST_PITCH);
    uint32_t block_size = p.linear ? 0u : p.block_size;
    uint32_t surface_row_bytes = p.linear ? (uint32_t)p.tex_pitch : p.surface_row_bytes;
    uint32_t surface_rows = p.linear ? p.level_height : p.surface_rows;
    uint32_t surface_depth = p.linear ? 1u : p.surface_depth;
    uint32_t origin = (p.x_bytes & NVN_RT_ORIGIN_MASK) | ((origin_y & NVN_RT_ORIGIN_MASK) << 16);
    uint32_t block_params[NVN_DMA_PARAMS_WORDS] = {block_size, surface_row_bytes, surface_rows,
                                                   surface_depth, layer_field, origin};

    uint32_t at = 0;
    out[at++] = NVN_RT_HEADER_INCREASING | (NVN_DMA_OFFSET_WORDS << NVN_RT_HEADER_COUNT_SHIFT) |
                (NVN_DMA_SUBCHANNEL << NVN_RT_SUBCHANNEL_SHIFT) | NVN_DMA_METHOD_OFFSET;
    out[at++] = (uint32_t)(in_addr >> 32);
    out[at++] = (uint32_t)in_addr;
    out[at++] = (uint32_t)(out_addr >> 32);
    out[at++] = (uint32_t)out_addr;
    at = dma_side_words(out, at, NVN_DMA_METHOD_SRC_PARAMS, NVN_DMA_METHOD_SRC_PITCH, src_block, src_pitch,
                        block_params);
    at = dma_side_words(out, at, NVN_DMA_METHOD_DST_PARAMS, NVN_DMA_METHOD_DST_PITCH, dst_block, dst_pitch,
                        block_params);
    out[at++] = NVN_RT_HEADER_INCREASING | (NVN_DMA_LINE_WORDS << NVN_RT_HEADER_COUNT_SHIFT) |
                (NVN_DMA_SUBCHANNEL << NVN_RT_SUBCHANNEL_SHIFT) | NVN_DMA_METHOD_LINE;
    out[at++] = p.row_bytes;
    out[at++] = line_rows;
    out[at++] = NVN_RT_HEADER_INLINE | (launch << NVN_RT_HEADER_COUNT_SHIFT) |
                (NVN_DMA_SUBCHANNEL << NVN_RT_SUBCHANNEL_SHIFT) | NVN_DMA_METHOD_LAUNCH;
    return at;
}

/* ---- the 2D block of CopyBufferToTexture, Downsample and TiledDownsample (d82c5d0). Each side is one surface: a
   block linear level, or the buffer of a copy with the row stride as its pitch. */
typedef struct {
    uint32_t format_word, linear, tile, depth_word;
    uint32_t surface_width, surface_height; /* the size registers, aligned for a block surface */
    uint32_t level_width, level_height;     /* the sampled level size, before the tile alignment */
    uint32_t pitch;                         /* a pitch linear surface */
    uint64_t address;
} NvnRt2dSide;

#define NVN_RT_2D_OPEN_COPY        0x91010002u
#define NVN_RT_2D_OPEN_DOWNSAMPLE  0x91050002u
#define NVN_RT_2D_OPEN_TILED       0x91060002u
/* SAMPLE_MODE: bit 0 origin (1 corner), bit 4 filter (1 bilinear). The 2D route of CopyBufferToTexture is corner and
   point (d82ba04-d82ba3c), the downsamples are center and bilinear (d82ca3c-d82ca90, design item 2). */
#define NVN_RT_2D_SAMPLE_CORNER    0x1u
#define NVN_RT_2D_SAMPLE_BILINEAR  0x10u
#define NVN_RT_2D_UNIT             0x100000000ull
#define NVN_RT_2D_NEGATIVE_UNIT    0xFFFFFFFF00000000ull
#define NVN_RT_2D_SRC_PITCH_METHOD 0x6090u /* subchannel 3, methods 0x090 LAYER and 0x091 PITCH */
#define NVN_RT_2D_DST_WORDS        6u      /* format, linear, block size, depth, layer, pitch */
#define NVN_RT_2D_PITCH_WORDS      2u

static uint32_t rt_2d_emit(const NvnRt2dSide* src, const NvnRt2dSide* dst, int first, uint32_t open,
                           uint32_t sample_mode, const uint32_t rect[4], const uint32_t scale[4],
                           const uint32_t origin[4], uint32_t* out) {
    uint32_t at = 0;
    if (first) {
        out[at++] = open;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_METHOD, NVN_RT_COPY_SRC_WORDS);
        out[at++] = src->format_word;
        out[at++] = src->linear;
        out[at++] = src->tile;
        out[at++] = src->depth_word;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_2D_SRC_PITCH_METHOD, NVN_RT_2D_PITCH_WORDS);
        out[at++] = 0u;
        out[at++] = src->pitch;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = src->surface_width;
        out[at++] = src->surface_height;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = (uint32_t)(src->address >> 32);
        out[at++] = (uint32_t)src->address;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_METHOD, NVN_RT_2D_DST_WORDS);
        out[at++] = dst->format_word;
        out[at++] = dst->linear;
        out[at++] = dst->tile;
        out[at++] = dst->depth_word;
        out[at++] = 0u;
        out[at++] = dst->pitch;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = dst->surface_width;
        out[at++] = dst->surface_height;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = (uint32_t)(dst->address >> 32);
        out[at++] = (uint32_t)dst->address;
        out[at++] = NVN_RT_COPY_VALUE(NVN_RT_COPY_IMMD_METHOD, NVN_RT_COPY_IMMD_VALUE);
    } else {
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = (uint32_t)(src->address >> 32);
        out[at++] = (uint32_t)src->address;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
        out[at++] = (uint32_t)(dst->address >> 32);
        out[at++] = (uint32_t)dst->address;
    }
    out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_FLAGS_METHOD, NVN_RT_COPY_FLAGS_WORDS);
    out[at++] = sample_mode;
    out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_RECT_METHOD, NVN_RT_COPY_RECT_WORDS);
    out[at++] = rect[0];
    out[at++] = rect[1];
    out[at++] = rect[2];
    out[at++] = rect[3];
    out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SCALE_METHOD, NVN_RT_COPY_RECT_WORDS);
    for (uint32_t i = 0; i < NVN_RT_COPY_RECT_WORDS; i++) out[at++] = scale[i];
    out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_OFFSET_METHOD, NVN_RT_COPY_RECT_WORDS);
    for (uint32_t i = 0; i < NVN_RT_COPY_RECT_WORDS; i++) out[at++] = origin[i];
    return at;
}

/* The 2D surface of one texture level. A multisample texture is its level 0 with the sample scale. 0 after a fault. */
static int rt_2d_surface(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                         NvnRt2dSide* s) {
    memset(s, 0, sizeof *s);
    const NvnTextureLayout* l = &texture->layout;
    uint32_t target = view && (view->flags & NVN_VIEW_TARGET) ? view->target : texture->builder.target;
    if (target != NVN_TEXTURE_TARGET_2D && target != NVN_TEXTURE_TARGET_2D_ARRAY &&
        target != NVN_TEXTURE_TARGET_2D_MS && target != NVN_TEXTURE_TARGET_2D_MS_ARRAY &&
        target != NVN_TEXTURE_TARGET_RECTANGLE) {
        env->base.fault(env->base.ctx, "copy 2d target", target);
        return 0;
    }
    uint32_t format = view && (view->flags & NVN_VIEW_FORMAT) ? view->format : texture->builder.format;
    if (!env->format_word) {
        env->base.fault(env->base.ctx, "copy 2d format", format);
        return 0;
    }
    uint32_t word = env->format_word(env->base.ctx, format);
    if (!word) {
        env->base.fault(env->base.ctx, "copy 2d format", format);
        return 0;
    }
    s->format_word = word;
    if (l->block_width != 1u || l->block_height != 1u) {
        env->base.fault(env->base.ctx, "copy 2d block", format);
        return 0;
    }
    s->linear = (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) != 0u;
    s->tile = rt_tile_word(l);
    uint32_t tile_depth = 1u << (l->tile_z & NVN_RT_SHIFT_MASK);
    uint32_t depth = l->depth ? l->depth : 1u;
    s->depth_word = depth < tile_depth ? depth : tile_depth;
    uint32_t base_level = view && (view->flags & NVN_VIEW_LEVELS) ? view->base_level : 0u;
    uint32_t base_layer = view && (view->flags & NVN_VIEW_LAYERS) ? view->base_layer : 0u;
    uint64_t offset = 0;
    if (!nvn_texture_view_offset(texture, view, &offset)) {
        env->base.fault(env->base.ctx, "copy 2d level", base_level);
        return 0;
    }
    s->address = texture->address + offset;
    if (s->linear) {
        s->address = texture->address + (uint64_t)base_layer * l->layer_stride;
        s->surface_width = l->width;
        s->surface_height = l->height;
        s->level_width = l->width;
        s->level_height = l->height;
        s->pitch = l->linear_stride;
        return 1;
    }
    uint32_t width = rt_mip_dimension(l->width, base_level);
    uint32_t height = rt_mip_dimension(l->height, base_level);
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / l->bytes_per_block;
    if (l->sample_x != 1u || l->sample_y != 1u) {
        width *= l->sample_x;
        height *= l->sample_y;
    }
    uint32_t factor = rt_tile_factor(l, width, height, l->depth);
    s->level_width = width;
    s->level_height = height;
    s->surface_width = rt_align_up(width, factor * gob_blocks);
    s->surface_height = height;
    return 1;
}

uint32_t nvn_rt_copy_buffer_texture_2d_words(const NvnRtCopyEnv* env, const NvnTexture* texture,
                                             const NvnTextureView* view, uint64_t buffer,
                                             const uint32_t region[6], uint32_t flags,
                                             uint32_t row_stride, uint32_t image_stride, uint32_t* out) {
    if (!env || !env->base.fault || !env->format_word || !out || !texture || !region) return 0;
    int nothing = 0;
    NvnRtCopyPlan p;
    if (!rt_copy_plan(env, texture, view, region, row_stride, image_stride, &p, &nothing)) return 0;
    if (nothing) return 0;
    if (p.layers > NVN_RT_COPY_LAYERS_MAX) {
        env->base.fault(env->base.ctx, "copy layers", p.layers);
        return 0;
    }
    if (p.linear) {
        env->base.fault(env->base.ctx, "copy 2d linear", texture->address);
        return 0;
    }
    if (p.is_3d) {
        env->base.fault(env->base.ctx, "copy 2d target", NVN_TEXTURE_TARGET_3D);
        return 0;
    }
    NvnRt2dSide dst;
    if (!rt_2d_surface(env, texture, view, &dst)) return 0;
    uint32_t width = region[3];
    uint32_t one_d = (view && (view->flags & NVN_VIEW_TARGET) ? view->target : texture->builder.target) ==
                     NVN_TEXTURE_TARGET_1D_ARRAY;
    uint32_t height = one_d ? 1u : region[4];
    uint32_t rect[4] = {region[0], one_d ? 0u : region[1], width, height};
    uint64_t du = (flags & NVN_COPY_FLAG_MIRROR_X) ? NVN_RT_2D_NEGATIVE_UNIT : NVN_RT_2D_UNIT;
    uint64_t dv = (flags & NVN_COPY_FLAG_MIRROR_Y) ? NVN_RT_2D_NEGATIVE_UNIT : NVN_RT_2D_UNIT;
    uint32_t scale[4] = {(uint32_t)du, (uint32_t)(du >> 32), (uint32_t)dv, (uint32_t)(dv >> 32)};
    /* 32.32 fixed point, as the step: a mirrored side starts at its far edge. With the corner origin the library starts
       half a step in (d82b9a8-d82b9d4), so the engine samples the texel centers. */
    uint64_t sx = ((flags & NVN_COPY_FLAG_MIRROR_X) ? (uint64_t)width << 32 : 0u) + (uint64_t)((int64_t)du / 2);
    uint64_t sy = ((flags & NVN_COPY_FLAG_MIRROR_Y) ? (uint64_t)height << 32 : 0u) + (uint64_t)((int64_t)dv / 2);
    uint32_t origin[4] = {(uint32_t)sx, (uint32_t)(sx >> 32), (uint32_t)sy, (uint32_t)(sy >> 32)};
    NvnRt2dSide src = {0};
    src.format_word = dst.format_word;
    src.linear = 1u;
    src.depth_word = 1u; /* the buffer is one pitch surface */
    src.surface_width = width;
    src.surface_height = height;
    src.level_width = width;
    src.level_height = height;
    src.pitch = (uint32_t)p.row_stride;
    /* The addresses of all layers are made first, so that a late refusal writes no part of out. */
    uint64_t src_addr[NVN_RT_COPY_LAYERS_MAX], dst_addr[NVN_RT_COPY_LAYERS_MAX];
    for (uint32_t i = 0; i < p.layers; i++) {
        uint32_t buffer_layer = (flags & NVN_COPY_FLAG_MIRROR_Z) ? p.layers - 1u - i : i;
        if (!rt_mul_u64(buffer_layer, p.image_stride, &src_addr[i]) ||
            !rt_add_u64(buffer, src_addr[i], &src_addr[i]))
            return 0;
        if (!rt_mul_u64(p.region_layer + i, texture->layout.layer_stride, &dst_addr[i]) ||
            !rt_add_u64(texture->address + p.level_offset, dst_addr[i], &dst_addr[i]))
            return 0;
    }
    uint32_t at = 0;
    for (uint32_t i = 0; i < p.layers; i++) {
        src.address = src_addr[i];
        dst.address = dst_addr[i];
        at += rt_2d_emit(&src, &dst, i == 0u, NVN_RT_2D_OPEN_COPY, NVN_RT_2D_SAMPLE_CORNER, rect, scale, origin,
                         out + at);
    }
    if (at < NVN_RT_COPY_2D_WORDS_MAX) out[at++] = NVN_RT_COPY_CLOSE;
    return at;
}

/* ---- Downsample and TiledDownsample. The factors come from the sample count of the source texture: the library
   index is ror32(texture record +0x68, 1) - 1, and that word is the sample count of the record (d83d3c0). */
static const uint32_t downsample_factor_x[8] = {2u, 2u, 1u, 4u, 1u, 1u, 1u, 4u};
static const uint32_t downsample_factor_y[8] = {1u, 2u, 1u, 2u, 1u, 1u, 1u, 4u};

static void rt_downsample_factors(const NvnTexture* src, uint32_t* xf, uint32_t* yf) {
    uint32_t word = src->layout.samples ? src->layout.samples : 1u;
    uint32_t index = ((word >> 1) | ((word & 1u) << 31)) - 1u; /* ror32(word, 1) - 1 */
    *xf = 1u;
    *yf = 1u;
    if (index <= 7u) {
        *xf = downsample_factor_x[index];
        *yf = downsample_factor_y[index];
    }
}

uint32_t nvn_rt_downsample_words(const NvnRtCopyEnv* env, const NvnTexture* src, const NvnTextureView* src_view,
                                 const NvnTexture* dst, const NvnTextureView* dst_view, int tiled,
                                 int* ratio_not_two, uint32_t* out) {
    if (ratio_not_two) *ratio_not_two = 0;
    if (!env || !env->base.fault || !env->format_word || !out || !src || !dst) return 0;
    NvnRt2dSide s, d;
    if (!rt_2d_surface(env, src, src_view, &s) || !rt_2d_surface(env, dst, dst_view, &d)) return 0;
    if (!d.level_width || !d.level_height) {
        env->base.fault(env->base.ctx, "downsample size", dst->address);
        return 0;
    }
    uint32_t rect[4] = {0u, 0u, d.level_width, d.level_height};
    uint64_t du, dv;
    if (tiled) {
        if (!s.level_width || !s.level_height) {
            env->base.fault(env->base.ctx, "downsample size", src->address);
            return 0;
        }
        du = ((uint64_t)s.level_width << 32) / d.level_width;
        dv = ((uint64_t)s.level_height << 32) / d.level_height;
        if ((uint64_t)s.level_width != 2ull * d.level_width || (uint64_t)s.level_height != 2ull * d.level_height)
            if (ratio_not_two) *ratio_not_two = 1;
    } else {
        uint32_t xf, yf;
        rt_downsample_factors(src, &xf, &yf);
        du = (uint64_t)xf << 32;
        dv = (uint64_t)yf << 32;
    }
    uint32_t scale[4] = {(uint32_t)du, (uint32_t)(du >> 32), (uint32_t)dv, (uint32_t)(dv >> 32)};
    uint32_t origin[4] = {0u, 0u, 0u, 0u};
    uint32_t at = rt_2d_emit(&s, &d, 1, tiled ? NVN_RT_2D_OPEN_TILED : NVN_RT_2D_OPEN_DOWNSAMPLE,
                             NVN_RT_2D_SAMPLE_BILINEAR, rect, scale, origin,
                             out);
    out[at++] = NVN_RT_COPY_CLOSE;
    return at;
}
