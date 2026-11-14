/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_RT_H
#define SNX_NVN_RT_H
#include "nx/gpu/nvn/nvn_params.h"
#include <stdint.h>

#define NVN_RT_COLOR_MAX 8u

typedef struct {
    const NvnTexture* texture;     /* NULL: the slot has no target */
    const NvnTextureView* view;    /* NULL: no view. A view with flags 0 is the same as no view. */
} NvnRtColor;

typedef struct NvnRtEnv {
    void* ctx;
    void (*method)(void* ctx, uint32_t m, uint32_t v);          /* one method write */
    void (*fault)(void* ctx, const char* what, uint64_t value);  /* one call for a refused request */
} NvnRtEnv;

/* 1 when all words are written. 0 when the request is refused: then NOTHING is written and fault was called once.
   depth is NULL or one target with a texture and its view. clip_limit is the device word at +0x18. */
int nvn_rt_emit(const NvnRtEnv* env, const NvnRtColor* color, uint32_t count, const NvnRtColor* depth,
                uint32_t clip_limit);

#define NVN_RT_CLEAR_LAYERS_MAX 64u
#define NVN_RT_CLEAR_WORDS_MAX  (15u + 10u * NVN_RT_CLEAR_LAYERS_MAX)

/* The words of nvnCommandBufferClearTexture (d857610). Returns the word count. A refusal returns 0, calls fault once and
   writes no part of out. An empty region writes no word in the library: then 0 is returned with *nothing set to 1 and
   fault is not called. view is the view of the call, NULL when there is none. */
uint32_t nvn_rt_clear_texture_words(const NvnRtEnv* env, const NvnTexture* texture,
                                    const NvnTextureView* view, const uint32_t region[6],
                                    const uint32_t color[4], uint32_t mask, uint32_t clip_limit,
                                    int* nothing, uint32_t* out);

/* 43 words for one layer (start, first pass, tail, end) and 23 more for each later layer. */
#define NVN_RT_COPY_LAYERS_MAX 64u
#define NVN_RT_COPY_WORDS_MAX  (43u + 23u * (NVN_RT_COPY_LAYERS_MAX - 1u))

/* The copy environment adds the game's format table column +0x18 to the render target callbacks. */
typedef struct {
    NvnRtEnv base;
    uint32_t (*format_word)(void* ctx, uint32_t format); /* 0: unreadable or invalid format */
} NvnRtCopyEnv;

/* The words of nvnCommandBufferCopyTextureToTexture (d82bfd0). Returns the word count. A refusal returns 0, calls fault
   once and writes no part of out. An empty destination region writes no word in the library: then 0 is returned with
   *nothing set to 1 and fault is not called. src_view and dst_view are the views of the call, NULL when there is none. */
uint32_t nvn_rt_copy_texture_words(const NvnRtCopyEnv* env, const NvnTexture* src,
                                   const NvnTextureView* src_view, const NvnTexture* dst,
                                   const NvnTextureView* dst_view, const uint32_t src_region[6],
                                   const uint32_t dst_region[6], uint32_t flags, int* nothing,
                                   uint32_t* out);

/* The copy flags of NVN (public NVNcopyFlags). Bits 1 to 4 select the route and the flips; bit 0 is not read. */
#define NVN_COPY_FLAG_LINEAR_FILTER 0x1u
#define NVN_COPY_FLAG_ENGINE_2D     0x2u
#define NVN_COPY_FLAG_MIRROR_X      0x4u
#define NVN_COPY_FLAG_MIRROR_Y      0x8u
#define NVN_COPY_FLAG_MIRROR_Z      0x10u

/* The DMA route of CopyBufferToTexture and CopyTextureToBuffer (the library block d82b0f0). One launch copies one
   region row stack or one layer. The builder writes the words of launch index; *launch_count is the number of
   launches. A refusal returns 0, calls fault once and writes no part of out. An empty region returns 0 with
   *launch_count 0 and no fault. */
/* The largest launch: 5 offset words, 7 block parameters, 2 pitch, 3 line and 1 launch word (18). */
#define NVN_RT_DMA_LAUNCH_WORDS 18u
uint32_t nvn_rt_copy_dma_words(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                               uint64_t buffer, const uint32_t region[6], uint32_t flags,
                               uint32_t row_stride, uint32_t image_stride, int to_texture,
                               uint32_t launch_index, uint32_t* launch_count, uint32_t* out, uint32_t cap);
/* The record-time check of the same route: the region against the level and the strides. 1 when the copy can be
   recorded. *nothing is 1 for an empty region (the library writes no copy). */
int nvn_rt_copy_dma_check(const NvnRtCopyEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                          uint64_t buffer, const uint32_t region[6], uint32_t row_stride, uint32_t image_stride,
                          int to_texture, int* nothing);

/* The 2D route of CopyBufferToTexture (B2T with ENGINE_2D): one 2D block for each layer, the source is the
   buffer with the row stride as its pitch. Returns the word count; a refusal returns 0 and writes no part of out. */
#define NVN_RT_COPY_2D_WORDS_MAX (47u + 23u * (NVN_RT_COPY_LAYERS_MAX - 1u))
uint32_t nvn_rt_copy_buffer_texture_2d_words(const NvnRtCopyEnv* env, const NvnTexture* texture,
                                             const NvnTextureView* view, uint64_t buffer,
                                             const uint32_t region[6], uint32_t flags,
                                             uint32_t row_stride, uint32_t image_stride, uint32_t* out);

/* Downsample and TiledDownsample: one 2D block from the source level to the destination level. tiled selects the
   whole-level ratio (the tiled form); otherwise the source sample count gives the factors. *ratio_not_two is set
   for a tiled size ratio that is not 2. Returns the word count; a refusal returns 0 and writes no part of out. */
#define NVN_RT_DOWNSAMPLE_WORDS_MAX 47u
uint32_t nvn_rt_downsample_words(const NvnRtCopyEnv* env, const NvnTexture* src, const NvnTextureView* src_view,
                                 const NvnTexture* dst, const NvnTextureView* dst_view, int tiled,
                                 int* ratio_not_two, uint32_t* out);

#endif
