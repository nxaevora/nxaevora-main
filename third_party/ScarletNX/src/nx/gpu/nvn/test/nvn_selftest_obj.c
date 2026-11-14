/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The builder and state object cases of the NVN selftest (nvn-unobserved.md, "The selftest"). One table holds the
   setters, the value sets and the getters; one runner sends the same script to the library and to the host handler.
   The object bytes are not compared, because the host keeps the values in its own records: the getters are the check
   (kind R), and a getter with a pointer output is compared as a blob with guard bytes (kind M). */
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define NVN_ST_OBJ_BYTES 0x200u  /* more than the largest builder or state object */
#define NVN_ST_OUT_BYTES 64u     /* 16 guard bytes, 32 payload bytes and 16 guard bytes */
#define NVN_ST_OUT_GUARD 16u
#define NVN_ST_OUT_FILL 0xA5
#define NVN_ST_MAX_GETS 32u
#define NVN_ST_MAX_PRE 4u
#define NVN_ST_DATA_WORDS 8u

/* The argument markers: the runner replaces them before the call. */
#define NVN_ST_DEVICE UINT64_MAX
#define NVN_ST_PTR (UINT64_MAX - 1u)

/* One object block for each kind. The host record is keyed by the address and the kind, so a kind never shares a block. */
enum {
    SLOT_DEVICE_BUILDER,
    SLOT_QUEUE_BUILDER,
    SLOT_WINDOW_BUILDER,
    SLOT_EVENT_BUILDER,
    SLOT_BUFFER_BUILDER,
    SLOT_TEXTURE_BUILDER,
    SLOT_SAMPLER_BUILDER,
    SLOT_BLEND_STATE,
    SLOT_COLOR_STATE,
    SLOT_CHANNEL_MASK_STATE,
    SLOT_DEPTH_STENCIL_STATE,
    SLOT_MULTISAMPLE_STATE,
    SLOT_POLYGON_STATE,
    SLOT_VERTEX_ATTRIB_STATE,
    SLOT_VERTEX_STREAM_STATE,
    SLOT_TEXTURE_VIEW,
    SLOT_SAMPLER_A,
    SLOT_SAMPLER_B,
    SLOT_TEXTURE_VIEW_B,
    SLOT_COUNT
};

/* ret: 0 for a void result, 1 for x0, 2 for a float in s0. out is the mask of the output registers x1..x5. */
typedef struct {
    const char* field;
    const char* name;
    uint64_t x1;
    unsigned out;
    unsigned ret;
} NvnStGet;

typedef struct {
    const char* name;
    uint64_t x[6];
} NvnStPre;

typedef struct {
    const char* setter;
    const char* args;
    uint64_t x[6];
    uint32_t s[4];
    unsigned n_s;
    unsigned n_data;
    uint32_t data[NVN_ST_DATA_WORDS];
    const NvnStPre* pre;
    unsigned n_pre;
    const NvnStGet* gets;
    unsigned n_gets;
} NvnStTest;

typedef struct {
    const char* tag;
    unsigned slot;
    const char* defaults;
    const char* device;
    const NvnStTest* tests;
    unsigned n_tests;
} NvnStKind;

/* a = observed in analysis/enum-run2.txt, b = default of the library, c = analyst table or the accepted range. */

/* DeviceBuilder: a = 0. Flags are store only. */
static const NvnStGet device_all[] = {
    {"flags", "nvnDeviceBuilderGetFlags", 0, 0, 1},
};
static const NvnStTest device_tests[] = {
    {.setter = NULL, .args = "device builder", .gets = device_all, .n_gets = ARRAY_LEN(device_all)},
    {.setter = "nvnDeviceBuilderSetFlags", .args = "flags=0", .x = {0, 0}, .gets = device_all, .n_gets = ARRAY_LEN(device_all)},
    {.setter = "nvnDeviceBuilderSetFlags", .args = "flags=1", .x = {0, 1}, .gets = device_all, .n_gets = ARRAY_LEN(device_all)},
};

/* QueueBuilder: a = flags 0, priority 1000; b = priority 100 and the default sizes; c = a larger size. */
static const NvnStGet queue_all[] = {
    {"device", "nvnQueueBuilderGetDevice", 0, 0, 1},
    {"flags", "nvnQueueBuilderGetFlags", 0, 0, 1},
    {"priority", "nvnQueueBuilderGetQueuePriority", 0, 0, 1},
    {"commandMemorySize", "nvnQueueBuilderGetCommandMemorySize", 0, 0x2u, 1},
    {"computeMemorySize", "nvnQueueBuilderGetComputeMemorySize", 0, 0x2u, 1},
    {"controlMemorySize", "nvnQueueBuilderGetControlMemorySize", 0, 0x2u, 1},
    {"commandFlushThreshold", "nvnQueueBuilderGetCommandFlushThreshold", 0, 0x2u, 1},
    {"memory", "nvnQueueBuilderGetMemory", 0, 0, 1},
    {"memorySize", "nvnQueueBuilderGetMemorySize", 0, 0, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStGet queue_device[] = {{"device", "nvnQueueBuilderGetDevice", 0, 0, 1}};
static const NvnStGet queue_flags[] = {
    {"flags", "nvnQueueBuilderGetFlags", 0, 0, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStGet queue_priority[] = {{"priority", "nvnQueueBuilderGetQueuePriority", 0, 0, 1}};
static const NvnStGet queue_command[] = {
    {"commandMemorySize", "nvnQueueBuilderGetCommandMemorySize", 0, 0x2u, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStGet queue_compute[] = {
    {"computeMemorySize", "nvnQueueBuilderGetComputeMemorySize", 0, 0x2u, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStGet queue_control[] = {
    {"controlMemorySize", "nvnQueueBuilderGetControlMemorySize", 0, 0x2u, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStGet queue_flush[] = {
    {"commandFlushThreshold", "nvnQueueBuilderGetCommandFlushThreshold", 0, 0x2u, 1},
    {"queueMemorySize", "nvnQueueBuilderGetQueueMemorySize", 0, 0, 1},
};
static const NvnStTest queue_tests[] = {
    {.setter = NULL, .args = "queue builder", .gets = queue_all, .n_gets = ARRAY_LEN(queue_all)},
    {.setter = "nvnQueueBuilderSetDevice", .args = "device", .x = {0, NVN_ST_DEVICE}, .gets = queue_device, .n_gets = 1},
    {.setter = "nvnQueueBuilderSetFlags", .args = "flags=0", .x = {0, 0}, .gets = queue_flags, .n_gets = ARRAY_LEN(queue_flags)},
    {.setter = "nvnQueueBuilderSetFlags", .args = "flags=1", .x = {0, 1}, .gets = queue_flags, .n_gets = ARRAY_LEN(queue_flags)},
    {.setter = "nvnQueueBuilderSetQueuePriority", .args = "100", .x = {0, 100}, .gets = queue_priority, .n_gets = 1},
    {.setter = "nvnQueueBuilderSetQueuePriority", .args = "1000", .x = {0, 1000}, .gets = queue_priority, .n_gets = 1},
    {.setter = "nvnQueueBuilderSetCommandMemorySize", .args = "0x10000", .x = {0, 0x10000}, .gets = queue_command, .n_gets = ARRAY_LEN(queue_command)},
    {.setter = "nvnQueueBuilderSetCommandMemorySize", .args = "0x20000", .x = {0, 0x20000}, .gets = queue_command, .n_gets = ARRAY_LEN(queue_command)},
    {.setter = "nvnQueueBuilderSetComputeMemorySize", .args = "0x40000", .x = {0, 0x40000}, .gets = queue_compute, .n_gets = ARRAY_LEN(queue_compute)},
    {.setter = "nvnQueueBuilderSetComputeMemorySize", .args = "0x20000", .x = {0, 0x20000}, .gets = queue_compute, .n_gets = ARRAY_LEN(queue_compute)},
    {.setter = "nvnQueueBuilderSetControlMemorySize", .args = "0x4000", .x = {0, 0x4000}, .gets = queue_control, .n_gets = ARRAY_LEN(queue_control)},
    {.setter = "nvnQueueBuilderSetControlMemorySize", .args = "0x8000", .x = {0, 0x8000}, .gets = queue_control, .n_gets = ARRAY_LEN(queue_control)},
    {.setter = "nvnQueueBuilderSetCommandFlushThreshold", .args = "0x1000", .x = {0, 0x1000}, .gets = queue_flush, .n_gets = ARRAY_LEN(queue_flush)},
    {.setter = "nvnQueueBuilderSetCommandFlushThreshold", .args = "0x2000", .x = {0, 0x2000}, .gets = queue_flush, .n_gets = ARRAY_LEN(queue_flush)},
};

/* WindowBuilder: a = interval 2; b = interval 1 and the zero defaults; c = an active texture count in range. */
static const NvnStGet window_all[] = {
    {"device", "nvnWindowBuilderGetDevice", 0, 0, 1},
    {"nativeWindow", "nvnWindowBuilderGetNativeWindow", 0, 0, 1},
    {"activeTextures", "nvnWindowBuilderGetNumActiveTextures", 0, 0, 1},
    {"textures", "nvnWindowBuilderGetNumTextures", 0, 0, 1},
    {"presentInterval", "nvnWindowBuilderGetPresentInterval", 0, 0, 1},
};
static const NvnStGet window_device[] = {{"device", "nvnWindowBuilderGetDevice", 0, 0, 1}};
static const NvnStGet window_native[] = {{"nativeWindow", "nvnWindowBuilderGetNativeWindow", 0, 0, 1}};
static const NvnStGet window_interval[] = {{"presentInterval", "nvnWindowBuilderGetPresentInterval", 0, 0, 1}};
static const NvnStGet window_active[] = {{"activeTextures", "nvnWindowBuilderGetNumActiveTextures", 0, 0, 1}};
static const NvnStTest window_tests[] = {
    {.setter = NULL, .args = "window builder", .gets = window_all, .n_gets = ARRAY_LEN(window_all)},
    {.setter = "nvnWindowBuilderSetDevice", .args = "device", .x = {0, NVN_ST_DEVICE}, .gets = window_device, .n_gets = 1},
    {.setter = "nvnWindowBuilderSetNativeWindow", .args = "0x1000", .x = {0, 0x1000}, .gets = window_native, .n_gets = 1},
    {.setter = "nvnWindowBuilderSetPresentInterval", .args = "1", .x = {0, 1}, .gets = window_interval, .n_gets = 1},
    {.setter = "nvnWindowBuilderSetPresentInterval", .args = "2", .x = {0, 2}, .gets = window_interval, .n_gets = 1},
    {.setter = "nvnWindowBuilderSetNumActiveTextures", .args = "1", .x = {0, 1}, .gets = window_active, .n_gets = 1},
    {.setter = "nvnWindowBuilderSetNumActiveTextures", .args = "2", .x = {0, 2}, .gets = window_active, .n_gets = 1},
};

/* EventBuilder: b = zero pool and offset (SetStorage takes a pool and is out of scope). */
static const NvnStGet event_all[] = {
    {"memoryPool", "nvnEventBuilderGetMemoryPool", 0, 0, 1},
    {"memoryOffset", "nvnEventBuilderGetMemoryOffset", 0, 0, 1},
    {"storage", "nvnEventBuilderGetStorage", 0, 0x2u, 1},
};
static const NvnStTest event_tests[] = {
    {.setter = NULL, .args = "event builder", .gets = event_all, .n_gets = ARRAY_LEN(event_all)},
};

/* BufferBuilder: b = zero size, pool and offset (SetStorage takes a pool and is out of scope). */
static const NvnStGet buffer_all[] = {
    {"size", "nvnBufferBuilderGetSize", 0, 0, 1},
    {"device", "nvnBufferBuilderGetDevice", 0, 0, 1},
    {"memoryPool", "nvnBufferBuilderGetMemoryPool", 0, 0, 1},
    {"memoryOffset", "nvnBufferBuilderGetMemoryOffset", 0, 0, 1},
};
static const NvnStGet buffer_device[] = {{"device", "nvnBufferBuilderGetDevice", 0, 0, 1}};
static const NvnStTest buffer_tests[] = {
    {.setter = NULL, .args = "buffer builder", .gets = buffer_all, .n_gets = ARRAY_LEN(buffer_all)},
    {.setter = "nvnBufferBuilderSetDevice", .args = "device", .x = {0, NVN_ST_DEVICE}, .gets = buffer_device, .n_gets = 1},
};

/* TextureBuilder: a = flags 0/9, targets 1/2, formats 0x25/0x26, levels 1/4, sizes from the old case; b = the
   default target 1, levels 1 and swizzle 2,3,4,5; c = valid formats and sizes for the derived getters. */
static const NvnStGet tex_all[] = {
    {"device", "nvnTextureBuilderGetDevice", 0, 0, 1},
    {"flags", "nvnTextureBuilderGetFlags", 0, 0, 1},
    {"target", "nvnTextureBuilderGetTarget", 0, 0, 1},
    {"width", "nvnTextureBuilderGetWidth", 0, 0, 1},
    {"height", "nvnTextureBuilderGetHeight", 0, 0, 1},
    {"depth", "nvnTextureBuilderGetDepth", 0, 0, 1},
    {"levels", "nvnTextureBuilderGetLevels", 0, 0, 1},
    {"format", "nvnTextureBuilderGetFormat", 0, 0, 1},
    {"samples", "nvnTextureBuilderGetSamples", 0, 0, 1},
    {"swizzle", "nvnTextureBuilderGetSwizzle", 0, 0x1Eu, 0},
    {"depthStencilMode", "nvnTextureBuilderGetDepthStencilMode", 0, 0, 1},
    {"stride", "nvnTextureBuilderGetStride", 0, 0, 1},
    {"memoryOffset", "nvnTextureBuilderGetMemoryOffset", 0, 0, 1},
    {"memoryPool", "nvnTextureBuilderGetMemoryPool", 0, 0, 1},
    {"glTextureName", "nvnTextureBuilderGetGLTextureName", 0, 0, 1},
    {"packagedTextureData", "nvnTextureBuilderGetPackagedTextureData", 0, 0, 1},
    {"packagedTextureLayout", "nvnTextureBuilderGetPackagedTextureLayout", 0, 0x2u, 1},
    {"storageAlignment", "nvnTextureBuilderGetStorageAlignment", 0, 0, 1},
    {"storageSize", "nvnTextureBuilderGetStorageSize", 0, 0, 1},
    {"storageClass", "nvnTextureBuilderGetStorageClass", 0, 0, 1},
    {"rawStorageClass", "nvnTextureBuilderGetRawStorageClass", 0, 0, 1},
    {"zcullStorageSize", "nvnTextureBuilderGetZCullStorageSize", 0, 0, 1},
    {"sparseTileLayout", "nvnTextureBuilderGetSparseTileLayout", 0, 0x1Eu, 0},
};
static const NvnStGet tex_device[] = {{"device", "nvnTextureBuilderGetDevice", 0, 0, 1}};
static const NvnStGet tex_flags[] = {{"flags", "nvnTextureBuilderGetFlags", 0, 0, 1}};
static const NvnStGet tex_target[] = {{"target", "nvnTextureBuilderGetTarget", 0, 0, 1}};
static const NvnStGet tex_size[] = {
    {"width", "nvnTextureBuilderGetWidth", 0, 0, 1},
    {"height", "nvnTextureBuilderGetHeight", 0, 0, 1},
    {"depth", "nvnTextureBuilderGetDepth", 0, 0, 1},
};
static const NvnStGet tex_width[] = {{"width", "nvnTextureBuilderGetWidth", 0, 0, 1}};
static const NvnStGet tex_height[] = {{"height", "nvnTextureBuilderGetHeight", 0, 0, 1}};
static const NvnStGet tex_depth[] = {{"depth", "nvnTextureBuilderGetDepth", 0, 0, 1}};
static const NvnStGet tex_levels[] = {{"levels", "nvnTextureBuilderGetLevels", 0, 0, 1}};
static const NvnStGet tex_samples[] = {{"samples", "nvnTextureBuilderGetSamples", 0, 0, 1}};
static const NvnStGet tex_swizzle[] = {{"swizzle", "nvnTextureBuilderGetSwizzle", 0, 0x1Eu, 0}};
static const NvnStGet tex_dsm[] = {{"depthStencilMode", "nvnTextureBuilderGetDepthStencilMode", 0, 0, 1}};
static const NvnStGet tex_stride[] = {{"stride", "nvnTextureBuilderGetStride", 0, 0, 1}};
static const NvnStGet tex_glname[] = {{"glTextureName", "nvnTextureBuilderGetGLTextureName", 0, 0, 1}};
static const NvnStGet tex_format[] = {
    {"format", "nvnTextureBuilderGetFormat", 0, 0, 1},
    {"storageAlignment", "nvnTextureBuilderGetStorageAlignment", 0, 0, 1},
    {"storageSize", "nvnTextureBuilderGetStorageSize", 0, 0, 1},
    {"storageClass", "nvnTextureBuilderGetStorageClass", 0, 0, 1},
    {"rawStorageClass", "nvnTextureBuilderGetRawStorageClass", 0, 0, 1},
    {"zcullStorageSize", "nvnTextureBuilderGetZCullStorageSize", 0, 0, 1},
    {"sparseTileLayout", "nvnTextureBuilderGetSparseTileLayout", 0, 0x1Eu, 0},
};
static const NvnStPre tex_format_pre[] = {
    {"nvnTextureBuilderSetFlags", {0, 9}},
    {"nvnTextureBuilderSetTarget", {0, 1}},
};
static const NvnStTest texture_tests[] = {
    {.setter = NULL, .args = "texture builder", .gets = tex_all, .n_gets = ARRAY_LEN(tex_all)},
    {.setter = "nvnTextureBuilderSetDevice", .args = "device", .x = {0, NVN_ST_DEVICE}, .gets = tex_device, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetFlags", .args = "flags=0", .x = {0, 0}, .gets = tex_flags, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetFlags", .args = "flags=9", .x = {0, 9}, .gets = tex_flags, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetTarget", .args = "target=1", .x = {0, 1}, .gets = tex_target, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetTarget", .args = "target=2", .x = {0, 2}, .gets = tex_target, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetSize2D", .args = "64x64", .x = {0, 64, 64}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetSize2D", .args = "1280x720", .x = {0, 1280, 720}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetSize3D", .args = "32x32x8", .x = {0, 32, 32, 8}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetSize3D", .args = "64x16x4", .x = {0, 64, 16, 4}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetSize1D", .args = "128", .x = {0, 128}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetSize1D", .args = "1024", .x = {0, 1024}, .gets = tex_size, .n_gets = ARRAY_LEN(tex_size)},
    {.setter = "nvnTextureBuilderSetWidth", .args = "64", .x = {0, 64}, .gets = tex_width, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetWidth", .args = "1920", .x = {0, 1920}, .gets = tex_width, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetHeight", .args = "64", .x = {0, 64}, .gets = tex_height, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetHeight", .args = "1080", .x = {0, 1080}, .gets = tex_height, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetDepth", .args = "1", .x = {0, 1}, .gets = tex_depth, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetDepth", .args = "6", .x = {0, 6}, .gets = tex_depth, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetLevels", .args = "levels=1", .x = {0, 1}, .gets = tex_levels, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetLevels", .args = "levels=4", .x = {0, 4}, .gets = tex_levels, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetFormat", .args = "fmt=0x25", .x = {0, 0x25}, .pre = tex_format_pre, .n_pre = ARRAY_LEN(tex_format_pre), .gets = tex_format, .n_gets = ARRAY_LEN(tex_format)},
    {.setter = "nvnTextureBuilderSetFormat", .args = "fmt=0x26", .x = {0, 0x26}, .pre = tex_format_pre, .n_pre = ARRAY_LEN(tex_format_pre), .gets = tex_format, .n_gets = ARRAY_LEN(tex_format)},
    {.setter = "nvnTextureBuilderSetSwizzle", .args = "2,3,4,5", .x = {0, 2, 3, 4, 5}, .gets = tex_swizzle, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetSwizzle", .args = "0,1,2,3", .x = {0, 0, 1, 2, 3}, .gets = tex_swizzle, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetDepthStencilMode", .args = "0", .x = {0, 0}, .gets = tex_dsm, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetDepthStencilMode", .args = "1", .x = {0, 1}, .gets = tex_dsm, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetStride", .args = "0", .x = {0, 0}, .gets = tex_stride, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetStride", .args = "256", .x = {0, 256}, .gets = tex_stride, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetSamples", .args = "0", .x = {0, 0}, .gets = tex_samples, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetSamples", .args = "2", .x = {0, 2}, .gets = tex_samples, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetGLTextureName", .args = "0", .x = {0, 0}, .gets = tex_glname, .n_gets = 1},
    {.setter = "nvnTextureBuilderSetGLTextureName", .args = "0x1234", .x = {0, 0x1234}, .gets = tex_glname, .n_gets = 1},
};

/* SamplerBuilder: a = filters 1/2/5, wraps 1/3/5/6/7; b = the defaults (filter 1, wrap 1, max lod 1000, compare 2,
   anisotropy 1); c = valid float values and integer enum values. */
static const NvnStGet sampler_all[] = {
    {"device", "nvnSamplerBuilderGetDevice", 0, 0, 1},
    {"lodBias", "nvnSamplerBuilderGetLodBias", 0, 0, 2},
    {"maxAnisotropy", "nvnSamplerBuilderGetMaxAnisotropy", 0, 0, 2},
    {"reductionFilter", "nvnSamplerBuilderGetReductionFilter", 0, 0, 1},
    {"lodSnap", "nvnSamplerBuilderGetLodSnap", 0, 0, 2},
    {"minMagFilter", "nvnSamplerBuilderGetMinMagFilter", 0, 0x6u, 0},
    {"wrapMode", "nvnSamplerBuilderGetWrapMode", 0, 0xEu, 0},
    {"lodClamp", "nvnSamplerBuilderGetLodClamp", 0, 0x6u, 0},
    {"compare", "nvnSamplerBuilderGetCompare", 0, 0x6u, 0},
    {"borderColor", "nvnSamplerBuilderGetBorderColor", 0, 0x1Eu, 0},
};
static const NvnStGet sampler_device[] = {{"device", "nvnSamplerBuilderGetDevice", 0, 0, 1}};
static const NvnStGet sampler_filter[] = {{"minMagFilter", "nvnSamplerBuilderGetMinMagFilter", 0, 0x6u, 0}};
static const NvnStGet sampler_wrap[] = {{"wrapMode", "nvnSamplerBuilderGetWrapMode", 0, 0xEu, 0}};
static const NvnStGet sampler_lodclamp[] = {{"lodClamp", "nvnSamplerBuilderGetLodClamp", 0, 0x6u, 0}};
static const NvnStGet sampler_lodbias[] = {{"lodBias", "nvnSamplerBuilderGetLodBias", 0, 0, 2}};
static const NvnStGet sampler_compare[] = {{"compare", "nvnSamplerBuilderGetCompare", 0, 0x6u, 0}};
static const NvnStGet sampler_border[] = {{"borderColor", "nvnSamplerBuilderGetBorderColor", 0, 0x1Eu, 0}};
static const NvnStGet sampler_borderi[] = {{"borderColor", "nvnSamplerBuilderGetBorderColori", 0, 0x1Eu, 0}};
static const NvnStGet sampler_borderui[] = {{"borderColor", "nvnSamplerBuilderGetBorderColorui", 0, 0x1Eu, 0}};
static const NvnStGet sampler_aniso[] = {{"maxAnisotropy", "nvnSamplerBuilderGetMaxAnisotropy", 0, 0, 2}};
static const NvnStGet sampler_reduction[] = {{"reductionFilter", "nvnSamplerBuilderGetReductionFilter", 0, 0, 1}};
static const NvnStGet sampler_snap[] = {{"lodSnap", "nvnSamplerBuilderGetLodSnap", 0, 0, 2}};
static const NvnStTest sampler_tests[] = {
    {.setter = NULL, .args = "sampler builder", .gets = sampler_all, .n_gets = ARRAY_LEN(sampler_all)},
    {.setter = "nvnSamplerBuilderSetDevice", .args = "device", .x = {0, NVN_ST_DEVICE}, .gets = sampler_device, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetMinMagFilter", .args = "2,1", .x = {0, 2, 1}, .gets = sampler_filter, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetMinMagFilter", .args = "5,0", .x = {0, 5, 0}, .gets = sampler_filter, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetWrapMode", .args = "1,1,1", .x = {0, 1, 1, 1}, .gets = sampler_wrap, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetWrapMode", .args = "5,6,7", .x = {0, 5, 6, 7}, .gets = sampler_wrap, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodClamp", .args = "0,1000", .x = {0}, .s = {0x00000000u, 0x447A0000u}, .n_s = 2, .gets = sampler_lodclamp, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodClamp", .args = "1.5,10", .x = {0}, .s = {0x3FC00000u, 0x41200000u}, .n_s = 2, .gets = sampler_lodclamp, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodBias", .args = "0", .x = {0}, .s = {0x00000000u}, .n_s = 1, .gets = sampler_lodbias, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodBias", .args = "0.5", .x = {0}, .s = {0x3F000000u}, .n_s = 1, .gets = sampler_lodbias, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetCompare", .args = "0,2", .x = {0, 0, 2}, .gets = sampler_compare, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetCompare", .args = "1,3", .x = {0, 1, 3}, .gets = sampler_compare, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetBorderColor", .args = "0,0,0,0", .x = {0, NVN_ST_PTR}, .n_data = 4, .data = {0, 0, 0, 0}, .gets = sampler_border, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetBorderColor", .args = "1,0,0,1", .x = {0, NVN_ST_PTR}, .n_data = 4, .data = {0x3F800000u, 0, 0, 0x3F800000u}, .gets = sampler_border, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetBorderColori", .args = "1,2,3,4", .x = {0, NVN_ST_PTR}, .n_data = 4, .data = {1, 2, 3, 4}, .gets = sampler_borderi, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetBorderColorui", .args = "0xee,0xdd,0xcc,0xbb", .x = {0, NVN_ST_PTR}, .n_data = 4, .data = {0xEEu, 0xDDu, 0xCCu, 0xBBu}, .gets = sampler_borderui, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetMaxAnisotropy", .args = "1", .x = {0}, .s = {0x3F800000u}, .n_s = 1, .gets = sampler_aniso, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetMaxAnisotropy", .args = "8", .x = {0}, .s = {0x41000000u}, .n_s = 1, .gets = sampler_aniso, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetReductionFilter", .args = "0", .x = {0, 0}, .gets = sampler_reduction, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetReductionFilter", .args = "1", .x = {0, 1}, .gets = sampler_reduction, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodSnap", .args = "0", .x = {0}, .s = {0x00000000u}, .n_s = 1, .gets = sampler_snap, .n_gets = 1},
    {.setter = "nvnSamplerBuilderSetLodSnap", .args = "0.5", .x = {0}, .s = {0x3F000000u}, .n_s = 1, .gets = sampler_snap, .n_gets = 1},
};

/* State objects: a = the observed values in enum-run2.txt; b = the library default; c = a valid enum value. */
static const NvnStGet blend_all[] = {
    {"blendTarget", "nvnBlendStateGetBlendTarget", 0, 0, 1},
    {"blendFunc", "nvnBlendStateGetBlendFunc", 0, 0x1Eu, 0},
    {"blendEquation", "nvnBlendStateGetBlendEquation", 0, 0x6u, 0},
    {"advancedMode", "nvnBlendStateGetAdvancedMode", 0, 0, 1},
    {"advancedOverlap", "nvnBlendStateGetAdvancedOverlap", 0, 0, 1},
    {"advancedPremultipliedSrc", "nvnBlendStateGetAdvancedPremultipliedSrc", 0, 0, 1},
    {"advancedNormalizedDst", "nvnBlendStateGetAdvancedNormalizedDst", 0, 0, 1},
};
static const NvnStGet blend_target[] = {{"blendTarget", "nvnBlendStateGetBlendTarget", 0, 0, 1}};
static const NvnStGet blend_func[] = {{"blendFunc", "nvnBlendStateGetBlendFunc", 0, 0x1Eu, 0}};
static const NvnStGet blend_equation[] = {{"blendEquation", "nvnBlendStateGetBlendEquation", 0, 0x6u, 0}};
static const NvnStGet blend_mode[] = {{"advancedMode", "nvnBlendStateGetAdvancedMode", 0, 0, 1}};
static const NvnStGet blend_overlap[] = {{"advancedOverlap", "nvnBlendStateGetAdvancedOverlap", 0, 0, 1}};
static const NvnStGet blend_premul[] = {{"advancedPremultipliedSrc", "nvnBlendStateGetAdvancedPremultipliedSrc", 0, 0, 1}};
static const NvnStGet blend_norm[] = {{"advancedNormalizedDst", "nvnBlendStateGetAdvancedNormalizedDst", 0, 0, 1}};
static const NvnStTest blend_tests[] = {
    {.setter = NULL, .args = "blend state", .gets = blend_all, .n_gets = ARRAY_LEN(blend_all)},
    {.setter = "nvnBlendStateSetBlendTarget", .args = "0", .x = {0, 0}, .gets = blend_target, .n_gets = 1},
    {.setter = "nvnBlendStateSetBlendTarget", .args = "1", .x = {0, 1}, .gets = blend_target, .n_gets = 1},
    {.setter = "nvnBlendStateSetBlendFunc", .args = "1,1,1,1", .x = {0, 1, 1, 1, 1}, .gets = blend_func, .n_gets = 1},
    {.setter = "nvnBlendStateSetBlendFunc", .args = "8,4,10,6", .x = {0, 8, 4, 10, 6}, .gets = blend_func, .n_gets = 1},
    {.setter = "nvnBlendStateSetBlendEquation", .args = "1,1", .x = {0, 1, 1}, .gets = blend_equation, .n_gets = 1},
    {.setter = "nvnBlendStateSetBlendEquation", .args = "3,5", .x = {0, 3, 5}, .gets = blend_equation, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedMode", .args = "0", .x = {0, 0}, .gets = blend_mode, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedMode", .args = "2", .x = {0, 2}, .gets = blend_mode, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedOverlap", .args = "0", .x = {0, 0}, .gets = blend_overlap, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedOverlap", .args = "1", .x = {0, 1}, .gets = blend_overlap, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedPremultipliedSrc", .args = "0", .x = {0, 0}, .gets = blend_premul, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedPremultipliedSrc", .args = "1", .x = {0, 1}, .gets = blend_premul, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedNormalizedDst", .args = "0", .x = {0, 0}, .gets = blend_norm, .n_gets = 1},
    {.setter = "nvnBlendStateSetAdvancedNormalizedDst", .args = "1", .x = {0, 1}, .gets = blend_norm, .n_gets = 1},
};

static const NvnStGet color_all[] = {
    {"blendEnable", "nvnColorStateGetBlendEnable", 0, 0, 1},
    {"logicOp", "nvnColorStateGetLogicOp", 0, 0, 1},
    {"alphaTest", "nvnColorStateGetAlphaTest", 0, 0, 1},
};
static const NvnStGet color_blend0[] = {{"blendEnable", "nvnColorStateGetBlendEnable", 0, 0, 1}};
static const NvnStGet color_blend7[] = {{"blendEnable", "nvnColorStateGetBlendEnable", 7, 0, 1}};
static const NvnStGet color_logic[] = {{"logicOp", "nvnColorStateGetLogicOp", 0, 0, 1}};
static const NvnStGet color_alpha[] = {{"alphaTest", "nvnColorStateGetAlphaTest", 0, 0, 1}};
static const NvnStTest color_tests[] = {
    {.setter = NULL, .args = "color state", .gets = color_all, .n_gets = ARRAY_LEN(color_all)},
    {.setter = "nvnColorStateSetBlendEnable", .args = "target=0 on=1", .x = {0, 0, 1}, .gets = color_blend0, .n_gets = 1},
    {.setter = "nvnColorStateSetBlendEnable", .args = "target=7 on=1", .x = {0, 7, 1}, .gets = color_blend7, .n_gets = 1},
    {.setter = "nvnColorStateSetLogicOp", .args = "3", .x = {0, 3}, .gets = color_logic, .n_gets = 1},
    {.setter = "nvnColorStateSetLogicOp", .args = "5", .x = {0, 5}, .gets = color_logic, .n_gets = 1},
    {.setter = "nvnColorStateSetAlphaTest", .args = "5", .x = {0, 5}, .gets = color_alpha, .n_gets = 1},
    {.setter = "nvnColorStateSetAlphaTest", .args = "8", .x = {0, 8}, .gets = color_alpha, .n_gets = 1},
};

static const NvnStGet channel_all[] = {
    {"channelMask", "nvnChannelMaskStateGetChannelMask", 0, 0x3Cu, 0},
};
static const NvnStGet channel_mask[] = {{"channelMask", "nvnChannelMaskStateGetChannelMask", 0, 0x3Cu, 0}};
static const NvnStGet channel_mask3[] = {{"channelMask", "nvnChannelMaskStateGetChannelMask", 3, 0x3Cu, 0}};
static const NvnStTest channel_tests[] = {
    {.setter = NULL, .args = "channel mask state", .gets = channel_all, .n_gets = ARRAY_LEN(channel_all)},
    {.setter = "nvnChannelMaskStateSetChannelMask", .args = "target=0 1,1,1,1", .x = {0, 0, 1, 1, 1, 1}, .gets = channel_mask, .n_gets = 1},
    {.setter = "nvnChannelMaskStateSetChannelMask", .args = "target=3 0,1,0,1", .x = {0, 3, 0, 1, 0, 1}, .gets = channel_mask3, .n_gets = 1},
};

static const NvnStGet depth_all[] = {
    {"depthTestEnable", "nvnDepthStencilStateGetDepthTestEnable", 0, 0, 1},
    {"depthWriteEnable", "nvnDepthStencilStateGetDepthWriteEnable", 0, 0, 1},
    {"depthFunc", "nvnDepthStencilStateGetDepthFunc", 0, 0, 1},
    {"stencilTestEnable", "nvnDepthStencilStateGetStencilTestEnable", 0, 0, 1},
    {"stencilFuncFront", "nvnDepthStencilStateGetStencilFunc", 1, 0, 1},
    {"stencilFuncBack", "nvnDepthStencilStateGetStencilFunc", 2, 0, 1},
    {"stencilOpFront", "nvnDepthStencilStateGetStencilOp", 1, 0x1Cu, 0},
    {"stencilOpBack", "nvnDepthStencilStateGetStencilOp", 2, 0x1Cu, 0},
};
static const NvnStGet depth_test[] = {{"depthTestEnable", "nvnDepthStencilStateGetDepthTestEnable", 0, 0, 1}};
static const NvnStGet depth_write[] = {{"depthWriteEnable", "nvnDepthStencilStateGetDepthWriteEnable", 0, 0, 1}};
static const NvnStGet depth_stencil_test[] = {{"stencilTestEnable", "nvnDepthStencilStateGetStencilTestEnable", 0, 0, 1}};
static const NvnStGet depth_func[] = {{"depthFunc", "nvnDepthStencilStateGetDepthFunc", 0, 0, 1}};
static const NvnStGet stencil_func1[] = {{"stencilFuncFront", "nvnDepthStencilStateGetStencilFunc", 1, 0, 1}};
static const NvnStGet stencil_func2[] = {{"stencilFuncBack", "nvnDepthStencilStateGetStencilFunc", 2, 0, 1}};
static const NvnStGet stencil_op1[] = {{"stencilOpFront", "nvnDepthStencilStateGetStencilOp", 1, 0x1Cu, 0}};
static const NvnStGet stencil_op2[] = {{"stencilOpBack", "nvnDepthStencilStateGetStencilOp", 2, 0x1Cu, 0}};
static const NvnStTest depth_tests[] = {
    {.setter = NULL, .args = "depth stencil state", .gets = depth_all, .n_gets = ARRAY_LEN(depth_all)},
    {.setter = "nvnDepthStencilStateSetDepthTestEnable", .args = "0", .x = {0, 0}, .gets = depth_test, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetDepthTestEnable", .args = "1", .x = {0, 1}, .gets = depth_test, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetDepthWriteEnable", .args = "0", .x = {0, 0}, .gets = depth_write, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetDepthWriteEnable", .args = "1", .x = {0, 1}, .gets = depth_write, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilTestEnable", .args = "0", .x = {0, 0}, .gets = depth_stencil_test, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilTestEnable", .args = "1", .x = {0, 1}, .gets = depth_stencil_test, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetDepthFunc", .args = "2", .x = {0, 2}, .gets = depth_func, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetDepthFunc", .args = "6", .x = {0, 6}, .gets = depth_func, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilFunc", .args = "face=1 func=6", .x = {0, 1, 6}, .gets = stencil_func1, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilFunc", .args = "face=2 func=3", .x = {0, 2, 3}, .gets = stencil_func2, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilOp", .args = "face=1 1,1,1", .x = {0, 1, 1, 1, 1}, .gets = stencil_op1, .n_gets = 1},
    {.setter = "nvnDepthStencilStateSetStencilOp", .args = "face=2 7,4,3", .x = {0, 2, 7, 4, 3}, .gets = stencil_op2, .n_gets = 1},
};

static const NvnStGet multisample_all[] = {
    {"multisampleEnable", "nvnMultisampleStateGetMultisampleEnable", 0, 0, 1},
    {"samples", "nvnMultisampleStateGetSamples", 0, 0, 1},
    {"alphaToCoverageEnable", "nvnMultisampleStateGetAlphaToCoverageEnable", 0, 0, 1},
    {"alphaToCoverageDither", "nvnMultisampleStateGetAlphaToCoverageDither", 0, 0, 1},
    {"coverageModulationMode", "nvnMultisampleStateGetCoverageModulationMode", 0, 0, 1},
    {"rasterSamples", "nvnMultisampleStateGetRasterSamples", 0, 0, 1},
    {"coverageToColorEnable", "nvnMultisampleStateGetCoverageToColorEnable", 0, 0, 1},
    {"coverageToColorOutput", "nvnMultisampleStateGetCoverageToColorOutput", 0, 0, 1},
    {"sampleLocationsEnable", "nvnMultisampleStateGetSampleLocationsEnable", 0, 0, 1},
    {"sampleLocationsGridEnable", "nvnMultisampleStateGetSampleLocationsGridEnable", 0, 0, 1},
    {"sampleLocationsGrid", "nvnMultisampleStateGetSampleLocationsGrid", 0, 0x6u, 0},
};
static const NvnStGet ms_multisample[] = {{"multisampleEnable", "nvnMultisampleStateGetMultisampleEnable", 0, 0, 1}};
static const NvnStGet ms_a2c[] = {{"alphaToCoverageEnable", "nvnMultisampleStateGetAlphaToCoverageEnable", 0, 0, 1}};
static const NvnStGet ms_dither[] = {{"alphaToCoverageDither", "nvnMultisampleStateGetAlphaToCoverageDither", 0, 0, 1}};
static const NvnStGet ms_samples[] = {{"samples", "nvnMultisampleStateGetSamples", 0, 0, 1}};
static const NvnStGet ms_raster[] = {{"rasterSamples", "nvnMultisampleStateGetRasterSamples", 0, 0, 1}};
static const NvnStGet ms_modulation[] = {{"coverageModulationMode", "nvnMultisampleStateGetCoverageModulationMode", 0, 0, 1}};
static const NvnStGet ms_c2c[] = {{"coverageToColorEnable", "nvnMultisampleStateGetCoverageToColorEnable", 0, 0, 1}};
static const NvnStGet ms_c2c_out[] = {{"coverageToColorOutput", "nvnMultisampleStateGetCoverageToColorOutput", 0, 0, 1}};
static const NvnStGet ms_loc_enable[] = {{"sampleLocationsEnable", "nvnMultisampleStateGetSampleLocationsEnable", 0, 0, 1}};
static const NvnStGet ms_grid_enable[] = {{"sampleLocationsGridEnable", "nvnMultisampleStateGetSampleLocationsGridEnable", 0, 0, 1}};
static const NvnStTest multisample_tests[] = {
    {.setter = NULL, .args = "multisample state", .gets = multisample_all, .n_gets = ARRAY_LEN(multisample_all)},
    {.setter = "nvnMultisampleStateSetMultisampleEnable", .args = "0", .x = {0, 0}, .gets = ms_multisample, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetMultisampleEnable", .args = "1", .x = {0, 1}, .gets = ms_multisample, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetAlphaToCoverageEnable", .args = "0", .x = {0, 0}, .gets = ms_a2c, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetAlphaToCoverageEnable", .args = "1", .x = {0, 1}, .gets = ms_a2c, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetAlphaToCoverageDither", .args = "0", .x = {0, 0}, .gets = ms_dither, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetAlphaToCoverageDither", .args = "1", .x = {0, 1}, .gets = ms_dither, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSamples", .args = "1", .x = {0, 1}, .gets = ms_samples, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSamples", .args = "4", .x = {0, 4}, .gets = ms_samples, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetRasterSamples", .args = "1", .x = {0, 1}, .gets = ms_raster, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetRasterSamples", .args = "4", .x = {0, 4}, .gets = ms_raster, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageModulationMode", .args = "0", .x = {0, 0}, .gets = ms_modulation, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageModulationMode", .args = "2", .x = {0, 2}, .gets = ms_modulation, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageToColorEnable", .args = "0", .x = {0, 0}, .gets = ms_c2c, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageToColorEnable", .args = "1", .x = {0, 1}, .gets = ms_c2c, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageToColorOutput", .args = "0", .x = {0, 0}, .gets = ms_c2c_out, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetCoverageToColorOutput", .args = "3", .x = {0, 3}, .gets = ms_c2c_out, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSampleLocationsEnable", .args = "0", .x = {0, 0}, .gets = ms_loc_enable, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSampleLocationsEnable", .args = "1", .x = {0, 1}, .gets = ms_loc_enable, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSampleLocationsGridEnable", .args = "0", .x = {0, 0}, .gets = ms_grid_enable, .n_gets = 1},
    {.setter = "nvnMultisampleStateSetSampleLocationsGridEnable", .args = "1", .x = {0, 1}, .gets = ms_grid_enable, .n_gets = 1},
};

static const NvnStGet polygon_all[] = {
    {"cullFace", "nvnPolygonStateGetCullFace", 0, 0, 1},
    {"frontFace", "nvnPolygonStateGetFrontFace", 0, 0, 1},
    {"polygonMode", "nvnPolygonStateGetPolygonMode", 0, 0, 1},
    {"polygonOffsetEnables", "nvnPolygonStateGetPolygonOffsetEnables", 0, 0, 1},
};
static const NvnStGet polygon_cull[] = {{"cullFace", "nvnPolygonStateGetCullFace", 0, 0, 1}};
static const NvnStGet polygon_front[] = {{"frontFace", "nvnPolygonStateGetFrontFace", 0, 0, 1}};
static const NvnStGet polygon_mode[] = {{"polygonMode", "nvnPolygonStateGetPolygonMode", 0, 0, 1}};
static const NvnStGet polygon_offset[] = {{"polygonOffsetEnables", "nvnPolygonStateGetPolygonOffsetEnables", 0, 0, 1}};
static const NvnStTest polygon_tests[] = {
    {.setter = NULL, .args = "polygon state", .gets = polygon_all, .n_gets = ARRAY_LEN(polygon_all)},
    {.setter = "nvnPolygonStateSetCullFace", .args = "0", .x = {0, 0}, .gets = polygon_cull, .n_gets = 1},
    {.setter = "nvnPolygonStateSetCullFace", .args = "2", .x = {0, 2}, .gets = polygon_cull, .n_gets = 1},
    {.setter = "nvnPolygonStateSetFrontFace", .args = "0", .x = {0, 0}, .gets = polygon_front, .n_gets = 1},
    {.setter = "nvnPolygonStateSetFrontFace", .args = "1", .x = {0, 1}, .gets = polygon_front, .n_gets = 1},
    {.setter = "nvnPolygonStateSetPolygonMode", .args = "1", .x = {0, 1}, .gets = polygon_mode, .n_gets = 1},
    {.setter = "nvnPolygonStateSetPolygonMode", .args = "2", .x = {0, 2}, .gets = polygon_mode, .n_gets = 1},
    {.setter = "nvnPolygonStateSetPolygonOffsetEnables", .args = "0", .x = {0, 0}, .gets = polygon_offset, .n_gets = 1},
    {.setter = "nvnPolygonStateSetPolygonOffsetEnables", .args = "5", .x = {0, 5}, .gets = polygon_offset, .n_gets = 1},
};

static const NvnStGet vertex_attrib_all[] = {
    {"format", "nvnVertexAttribStateGetFormat", 0, 0x6u, 0},
    {"streamIndex", "nvnVertexAttribStateGetStreamIndex", 0, 0, 1},
};
static const NvnStGet vertex_attrib_format[] = {{"format", "nvnVertexAttribStateGetFormat", 0, 0x6u, 0}};
static const NvnStGet vertex_attrib_stream[] = {{"streamIndex", "nvnVertexAttribStateGetStreamIndex", 0, 0, 1}};
static const NvnStTest vertex_attrib_tests[] = {
    {.setter = NULL, .args = "vertex attrib state", .gets = vertex_attrib_all, .n_gets = ARRAY_LEN(vertex_attrib_all)},
    {.setter = "nvnVertexAttribStateSetFormat", .args = "fmt=0x22 off=0", .x = {0, 0x22, 0}, .gets = vertex_attrib_format, .n_gets = 1},
    {.setter = "nvnVertexAttribStateSetFormat", .args = "fmt=0x25 off=12", .x = {0, 0x25, 12}, .gets = vertex_attrib_format, .n_gets = 1},
    {.setter = "nvnVertexAttribStateSetStreamIndex", .args = "0", .x = {0, 0}, .gets = vertex_attrib_stream, .n_gets = 1},
    {.setter = "nvnVertexAttribStateSetStreamIndex", .args = "3", .x = {0, 3}, .gets = vertex_attrib_stream, .n_gets = 1},
};

static const NvnStGet vertex_stream_all[] = {
    {"stride", "nvnVertexStreamStateGetStride", 0, 0, 1},
    {"divisor", "nvnVertexStreamStateGetDivisor", 0, 0, 1},
};
static const NvnStGet vertex_stream_stride[] = {{"stride", "nvnVertexStreamStateGetStride", 0, 0, 1}};
static const NvnStGet vertex_stream_divisor[] = {{"divisor", "nvnVertexStreamStateGetDivisor", 0, 0, 1}};
static const NvnStTest vertex_stream_tests[] = {
    {.setter = NULL, .args = "vertex stream state", .gets = vertex_stream_all, .n_gets = ARRAY_LEN(vertex_stream_all)},
    {.setter = "nvnVertexStreamStateSetStride", .args = "12", .x = {0, 12}, .gets = vertex_stream_stride, .n_gets = 1},
    {.setter = "nvnVertexStreamStateSetStride", .args = "32", .x = {0, 32}, .gets = vertex_stream_stride, .n_gets = 1},
    {.setter = "nvnVertexStreamStateSetDivisor", .args = "0", .x = {0, 0}, .gets = vertex_stream_divisor, .n_gets = 1},
    {.setter = "nvnVertexStreamStateSetDivisor", .args = "1", .x = {0, 1}, .gets = vertex_stream_divisor, .n_gets = 1},
};

static const NvnStGet view_all[] = {
    {"levels", "nvnTextureViewGetLevels", 0, 0x6u, 1},
    {"layers", "nvnTextureViewGetLayers", 0, 0x6u, 1},
    {"format", "nvnTextureViewGetFormat", 0, 0x2u, 1},
    {"swizzle", "nvnTextureViewGetSwizzle", 0, 0x1Eu, 1},
    {"depthStencilMode", "nvnTextureViewGetDepthStencilMode", 0, 0x2u, 1},
    {"target", "nvnTextureViewGetTarget", 0, 0x2u, 1},
};
static const NvnStGet view_levels[] = {{"levels", "nvnTextureViewGetLevels", 0, 0x6u, 1}};
static const NvnStGet view_layers[] = {{"layers", "nvnTextureViewGetLayers", 0, 0x6u, 1}};
static const NvnStGet view_format[] = {{"format", "nvnTextureViewGetFormat", 0, 0x2u, 1}};
static const NvnStGet view_swizzle[] = {{"swizzle", "nvnTextureViewGetSwizzle", 0, 0x1Eu, 1}};
static const NvnStGet view_dsm[] = {{"depthStencilMode", "nvnTextureViewGetDepthStencilMode", 0, 0x2u, 1}};
static const NvnStGet view_target[] = {{"target", "nvnTextureViewGetTarget", 0, 0x2u, 1}};
static const NvnStTest view_tests[] = {
    {.setter = NULL, .args = "texture view", .gets = view_all, .n_gets = ARRAY_LEN(view_all)},
    {.setter = "nvnTextureViewSetLevels", .args = "0,1", .x = {0, 0, 1}, .gets = view_levels, .n_gets = 1},
    {.setter = "nvnTextureViewSetLevels", .args = "1,4", .x = {0, 1, 4}, .gets = view_levels, .n_gets = 1},
    {.setter = "nvnTextureViewSetLayers", .args = "0,1", .x = {0, 0, 1}, .gets = view_layers, .n_gets = 1},
    {.setter = "nvnTextureViewSetLayers", .args = "2,6", .x = {0, 2, 6}, .gets = view_layers, .n_gets = 1},
    {.setter = "nvnTextureViewSetFormat", .args = "fmt=0x25", .x = {0, 0x25}, .gets = view_format, .n_gets = 1},
    {.setter = "nvnTextureViewSetFormat", .args = "fmt=0x29", .x = {0, 0x29}, .gets = view_format, .n_gets = 1},
    {.setter = "nvnTextureViewSetSwizzle", .args = "2,3,4,5", .x = {0, 2, 3, 4, 5}, .gets = view_swizzle, .n_gets = 1},
    {.setter = "nvnTextureViewSetSwizzle", .args = "0,1,2,3", .x = {0, 0, 1, 2, 3}, .gets = view_swizzle, .n_gets = 1},
    {.setter = "nvnTextureViewSetDepthStencilMode", .args = "0", .x = {0, 0}, .gets = view_dsm, .n_gets = 1},
    {.setter = "nvnTextureViewSetDepthStencilMode", .args = "1", .x = {0, 1}, .gets = view_dsm, .n_gets = 1},
    {.setter = "nvnTextureViewSetTarget", .args = "1", .x = {0, 1}, .gets = view_target, .n_gets = 1},
    {.setter = "nvnTextureViewSetTarget", .args = "8", .x = {0, 8}, .gets = view_target, .n_gets = 1},
};

static const NvnStKind kinds[] = {
    {.tag = "device builder", .slot = SLOT_DEVICE_BUILDER, .defaults = "nvnDeviceBuilderSetDefaults", .tests = device_tests, .n_tests = ARRAY_LEN(device_tests)},
    {.tag = "queue builder", .slot = SLOT_QUEUE_BUILDER, .defaults = "nvnQueueBuilderSetDefaults", .device = "nvnQueueBuilderSetDevice", .tests = queue_tests, .n_tests = ARRAY_LEN(queue_tests)},
    {.tag = "window builder", .slot = SLOT_WINDOW_BUILDER, .defaults = "nvnWindowBuilderSetDefaults", .device = "nvnWindowBuilderSetDevice", .tests = window_tests, .n_tests = ARRAY_LEN(window_tests)},
    {.tag = "event builder", .slot = SLOT_EVENT_BUILDER, .defaults = "nvnEventBuilderSetDefaults", .tests = event_tests, .n_tests = ARRAY_LEN(event_tests)},
    {.tag = "buffer builder", .slot = SLOT_BUFFER_BUILDER, .defaults = "nvnBufferBuilderSetDefaults", .device = "nvnBufferBuilderSetDevice", .tests = buffer_tests, .n_tests = ARRAY_LEN(buffer_tests)},
    {.tag = "texture builder", .slot = SLOT_TEXTURE_BUILDER, .defaults = "nvnTextureBuilderSetDefaults", .device = "nvnTextureBuilderSetDevice", .tests = texture_tests, .n_tests = ARRAY_LEN(texture_tests)},
    {.tag = "sampler builder", .slot = SLOT_SAMPLER_BUILDER, .defaults = "nvnSamplerBuilderSetDefaults", .device = "nvnSamplerBuilderSetDevice", .tests = sampler_tests, .n_tests = ARRAY_LEN(sampler_tests)},
    {.tag = "blend state", .slot = SLOT_BLEND_STATE, .defaults = "nvnBlendStateSetDefaults", .tests = blend_tests, .n_tests = ARRAY_LEN(blend_tests)},
    {.tag = "color state", .slot = SLOT_COLOR_STATE, .defaults = "nvnColorStateSetDefaults", .tests = color_tests, .n_tests = ARRAY_LEN(color_tests)},
    {.tag = "channel mask state", .slot = SLOT_CHANNEL_MASK_STATE, .defaults = "nvnChannelMaskStateSetDefaults", .tests = channel_tests, .n_tests = ARRAY_LEN(channel_tests)},
    {.tag = "depth stencil state", .slot = SLOT_DEPTH_STENCIL_STATE, .defaults = "nvnDepthStencilStateSetDefaults", .tests = depth_tests, .n_tests = ARRAY_LEN(depth_tests)},
    {.tag = "multisample state", .slot = SLOT_MULTISAMPLE_STATE, .defaults = "nvnMultisampleStateSetDefaults", .tests = multisample_tests, .n_tests = ARRAY_LEN(multisample_tests)},
    {.tag = "polygon state", .slot = SLOT_POLYGON_STATE, .defaults = "nvnPolygonStateSetDefaults", .tests = polygon_tests, .n_tests = ARRAY_LEN(polygon_tests)},
    {.tag = "vertex attrib state", .slot = SLOT_VERTEX_ATTRIB_STATE, .defaults = "nvnVertexAttribStateSetDefaults", .tests = vertex_attrib_tests, .n_tests = ARRAY_LEN(vertex_attrib_tests)},
    {.tag = "vertex stream state", .slot = SLOT_VERTEX_STREAM_STATE, .defaults = "nvnVertexStreamStateSetDefaults", .tests = vertex_stream_tests, .n_tests = ARRAY_LEN(vertex_stream_tests)},
    {.tag = "texture view", .slot = SLOT_TEXTURE_VIEW, .defaults = "nvnTextureViewSetDefaults", .tests = view_tests, .n_tests = ARRAY_LEN(view_tests)},
};

static uint64_t test_arg(uint64_t v, uint64_t data) {
    if (v == NVN_ST_DEVICE) return nvn_st_device();
    if (v == NVN_ST_PTR) return data;
    return v;
}

/* The sampler builder cases that the sampler object cases reuse (u3-2, decision 7). Each index is one entry of
   sampler_tests, so the values stay in that table. Entry 3 is the filter that makes variant 2 different. */
static const unsigned sampler_object_cases[] = {2, 5, 7, 9, 11, 13, 17, 19, 21};
static const unsigned sampler_object_other = 3;

/* Applies one sampler builder case of sampler_tests to builder. 1 when its setter was found. */
static int sampler_apply_case(CPU* c, uint64_t builder, uint64_t data, int host, unsigned index) {
    const NvnStTest* t = &sampler_tests[index];
    if (!t->setter) return 1;
    NvnStProc setter = {0};
    if (!nvn_st_proc_find(&setter, t->setter)) {
        LOG("selftest: %s skipped (the game did not resolve it, or the host has no handler)\n", t->setter);
        return 0;
    }
    memset(GP(data), 0, NVN_ST_DATA_WORDS * sizeof(uint32_t));
    for (unsigned i = 0; i < t->n_data && i < NVN_ST_DATA_WORDS; i++) gw32(data + (uint64_t)i * 4u, t->data[i]);
    NvnStCall a = {.x = {builder}};
    for (unsigned r = 1; r < 6; r++) a.x[r] = test_arg(t->x[r], data);
    a.n_s = t->n_s;
    for (unsigned i = 0; i < t->n_s; i++) a.s[i] = t->s[i];
    nvn_st_call(c, host ? &setter.host : &setter.lib, &a);
    return 1;
}

int nvn_st_sampler_build(CPU* c, uint64_t builder, uint64_t sampler, uint64_t data, int host, unsigned variant) {
    NvnStProc set_defaults = {0}, set_device = {0}, initialize = {0};
    if (!nvn_st_proc_find(&set_defaults, "nvnSamplerBuilderSetDefaults") ||
        !nvn_st_proc_find(&set_device, "nvnSamplerBuilderSetDevice") ||
        !nvn_st_proc_find(&initialize, "nvnSamplerInitialize")) {
        LOG("selftest: nvnSamplerBuilderSetDefaults, SetDevice or nvnSamplerInitialize skipped (not resolved, or no "
            "handler)\n");
        return 0;
    }
    memset(GP(builder), 0, NVN_ST_OBJ_BYTES);
    nvn_st_call(c, host ? &set_defaults.host : &set_defaults.lib, &(NvnStCall){.x = {builder}});
    nvn_st_call(c, host ? &set_device.host : &set_device.lib, &(NvnStCall){.x = {builder, nvn_st_device()}});
    for (unsigned i = 0; i < ARRAY_LEN(sampler_object_cases); i++)
        if (!sampler_apply_case(c, builder, data, host, sampler_object_cases[i])) return 0;
    if (variant == 2 && !sampler_apply_case(c, builder, data, host, sampler_object_other)) return 0;
    return (uint32_t)nvn_st_call(c, host ? &initialize.host : &initialize.lib,
                                 &(NvnStCall){.x = {sampler, builder}}) != 0;
}

/* The state objects of the command buffer Bind cases: the default and one setter that the object cases use. The first
   value after the object is the one that a mutation changes. */
typedef struct {
    const char* defaults;
    const char* setter;
    uint64_t x[6];
} NvnStStateSpec;

static const NvnStStateSpec state_specs[NVN_ST_STATE_COUNT] = {
    [NVN_ST_STATE_BLEND] = {"nvnBlendStateSetDefaults", "nvnBlendStateSetBlendFunc", {0, 8, 4, 10, 6}},
    [NVN_ST_STATE_COLOR] = {"nvnColorStateSetDefaults", "nvnColorStateSetLogicOp", {0, 3}},
    [NVN_ST_STATE_CHANNEL_MASK] = {"nvnChannelMaskStateSetDefaults", "nvnChannelMaskStateSetChannelMask",
                                   {0, 3, 0, 1, 0, 1}},
    [NVN_ST_STATE_DEPTH_STENCIL] = {"nvnDepthStencilStateSetDefaults", "nvnDepthStencilStateSetDepthFunc", {0, 2}},
    [NVN_ST_STATE_MULTISAMPLE] = {"nvnMultisampleStateSetDefaults", "nvnMultisampleStateSetSamples", {0, 4}},
    [NVN_ST_STATE_POLYGON] = {"nvnPolygonStateSetDefaults", "nvnPolygonStateSetCullFace", {0, 2}},
};

int nvn_st_state_build(CPU* c, unsigned kind, int host, uint64_t obj, uint32_t xor_value) {
    if (kind >= NVN_ST_STATE_COUNT) return 0;
    const NvnStStateSpec* s = &state_specs[kind];
    NvnStProc defaults = {0}, setter = {0};
    if (!nvn_st_proc_find(&defaults, s->defaults) || !nvn_st_proc_find(&setter, s->setter)) {
        LOG("selftest: %s or %s skipped, not resolved\n", s->defaults, s->setter);
        return 0;
    }
    memset(GP(obj), 0, NVN_ST_OBJ_BYTES);
    nvn_st_call(c, host ? &defaults.host : &defaults.lib, &(NvnStCall){.x = {obj}});
    NvnStCall a = {.x = {obj, s->x[1], s->x[2], s->x[3], s->x[4], s->x[5]}};
    a.x[1] ^= xor_value;
    nvn_st_call(c, host ? &setter.host : &setter.lib, &a);
    return 1;
}

static void run_getter(CPU* c, const NvnStProc* p, int host, uint64_t obj, uint64_t out, const NvnStGet* g,
                       NvnStRec* rec) {
    NvnStCall a = {.x = {obj}};
    a.x[1] = g->x1;
    unsigned slot = 0;
    for (unsigned r = 1; r < 6; r++)
        if (g->out & (1u << r)) a.x[r] = out + NVN_ST_OUT_GUARD + (uint64_t)(slot++) * 8u;
    uint32_t s0 = 0;
    if (g->ret == 2) a.s0_out = &s0;
    if (g->out) memset(GP(out), NVN_ST_OUT_FILL, NVN_ST_OUT_BYTES);
    uint64_t ret = nvn_st_call(c, host ? &p->host : &p->lib, &a);
    if (g->ret == 2) nvn_st_add_ret(rec, g->field, s0);
    else if (g->ret == 1) nvn_st_add_ret(rec, g->field, ret);
    if (g->out) nvn_st_add_blob(rec, g->field, GP(out), NULL, NVN_ST_OUT_BYTES);
}

static void build_side(CPU* c, const NvnStTest* t, int host, const NvnStProc* def, const NvnStProc* dev,
                       const NvnStProc* setter, const NvnStProc* pre, const NvnStProc* get_procs,
                       const NvnStGet* get_descs, unsigned n_gets, uint64_t obj, uint64_t out_base, uint64_t data,
                       NvnStRec* rec) {
    memset(GP(obj), 0, NVN_ST_OBJ_BYTES);
    nvn_st_call(c, host ? &def->host : &def->lib, &(NvnStCall){.x = {obj}});
    if (dev) nvn_st_call(c, host ? &dev->host : &dev->lib, &(NvnStCall){.x = {obj, nvn_st_device()}});
    for (unsigned i = 0; i < t->n_pre; i++) {
        NvnStCall a = {.x = {obj}};
        for (unsigned r = 1; r < 6; r++) a.x[r] = test_arg(t->pre[i].x[r], data);
        nvn_st_call(c, host ? &pre[i].host : &pre[i].lib, &a);
    }
    if (setter) {
        NvnStCall a = {.x = {obj}};
        for (unsigned r = 1; r < 6; r++) a.x[r] = test_arg(t->x[r], data);
        a.n_s = t->n_s;
        for (unsigned i = 0; i < t->n_s; i++) a.s[i] = t->s[i];
        nvn_st_call(c, host ? &setter->host : &setter->lib, &a);
    }
    for (unsigned i = 0; i < n_gets; i++)
        run_getter(c, &get_procs[i], host, obj, out_base + (uint64_t)i * NVN_ST_OUT_BYTES, &get_descs[i], rec);
}

static void run_test(CPU* c, NvnStRun* run, const NvnStKind* k, const NvnStTest* t, uint64_t obj, uint64_t out_lib,
                     uint64_t out_host, uint64_t data) {
    NvnStProc def = {0}, dev = {0}, setter = {0};
    NvnStProc pre[NVN_ST_MAX_PRE] = {0}, get_procs[NVN_ST_MAX_GETS] = {0};
    NvnStGet get_descs[NVN_ST_MAX_GETS] = {0};
    const char* missing = NULL;
    if (!nvn_st_proc_find(&def, k->defaults)) missing = k->defaults;
    if (!missing && k->device && !nvn_st_proc_find(&dev, k->device)) missing = k->device;
    if (!missing && t->setter && !nvn_st_proc_find(&setter, t->setter)) missing = t->setter;
    for (unsigned i = 0; !missing && i < t->n_pre; i++)
        if (!nvn_st_proc_find(&pre[i], t->pre[i].name)) missing = t->pre[i].name;
    if (missing) {
        run->skipped++;
        LOG("selftest: %s skipped (the game did not resolve it, or the host has no handler)\n", missing);
        return;
    }
    unsigned n_gets = 0;
    for (unsigned i = 0; i < t->n_gets; i++) {
        if (!nvn_st_proc_find(&get_procs[n_gets], t->gets[i].name)) {
            run->skipped++;
            LOG("selftest: %s skipped (the game did not resolve it, or the host has no handler)\n",
                t->gets[i].name);
            continue;
        }
        get_descs[n_gets] = t->gets[i];
        n_gets++;
    }
    if (!n_gets) return;
    memset(GP(data), 0, NVN_ST_DATA_WORDS * sizeof(uint32_t));
    for (unsigned i = 0; i < t->n_data && i < NVN_ST_DATA_WORDS; i++) gw32(data + (uint64_t)i * 4u, t->data[i]);
    NvnStRec ref = {0}, got = {0};
    build_side(c, t, 0, &def, k->device ? &dev : NULL, t->setter ? &setter : NULL, pre, get_procs, get_descs, n_gets,
               obj, out_lib, data, &ref);
    build_side(c, t, 1, &def, k->device ? &dev : NULL, t->setter ? &setter : NULL, pre, get_procs, get_descs, n_gets,
               obj, out_host, data, &got);
    NvnStDiff d;
    nvn_st_compare(&ref, &got, &d);
    nvn_st_report(run, t->setter ? t->setter : "defaults", t->args, &ref, &d);
}

static void run_kind(CPU* c, NvnStRun* run, const NvnStKind* k, uint64_t blocks, uint64_t out_lib, uint64_t out_host,
                     uint64_t data) {
    uint64_t obj = blocks + (uint64_t)k->slot * NVN_ST_OBJ_BYTES;
    for (unsigned i = 0; i < k->n_tests; i++) run_test(c, run, k, &k->tests[i], obj, out_lib, out_host, data);
}

/* nvnSamplerCompare: two samplers built from the same builder, then compared. One equal pair and one different pair. */
static void case_sampler_compare(CPU* c, NvnStRun* run, uint64_t builder, uint64_t a, uint64_t b) {
    NvnStProc def = {0}, dev = {0}, filter = {0}, init = {0}, cmp = {0};
    if (!nvn_st_proc_find(&def, "nvnSamplerBuilderSetDefaults") ||
        !nvn_st_proc_find(&dev, "nvnSamplerBuilderSetDevice") ||
        !nvn_st_proc_find(&filter, "nvnSamplerBuilderSetMinMagFilter") ||
        !nvn_st_proc_find(&init, "nvnSamplerInitialize") || !nvn_st_proc_find(&cmp, "nvnSamplerCompare")) {
        run->skipped++;
        LOG("selftest: nvnSamplerCompare skipped (a function was not resolved, or the host has no handler)\n");
        return;
    }
    static const uint32_t pairs[][4] = {{1, 1, 1, 1}, {1, 1, 2, 1}};
    static const char* const labels[] = {"equal", "different"};
    for (size_t i = 0; i < ARRAY_LEN(pairs); i++) {
        NvnStRec ref = {0}, got = {0};
        NvnStDiff d;
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            const NvnStCallee* pdef = host ? &def.host : &def.lib;
            const NvnStCallee* pdev = host ? &dev.host : &dev.lib;
            const NvnStCallee* pfilter = host ? &filter.host : &filter.lib;
            const NvnStCallee* pinit = host ? &init.host : &init.lib;
            const NvnStCallee* pcmp = host ? &cmp.host : &cmp.lib;
            for (int which = 0; which < 2; which++) {
                uint64_t sampler = which ? b : a;
                memset(GP(builder), 0, NVN_ST_OBJ_BYTES);
                nvn_st_call(c, pdef, &(NvnStCall){.x = {builder}});
                nvn_st_call(c, pdev, &(NvnStCall){.x = {builder, nvn_st_device()}});
                nvn_st_call(c, pfilter, &(NvnStCall){.x = {builder, pairs[i][which * 2], pairs[i][which * 2 + 1]}});
                nvn_st_call(c, pinit, &(NvnStCall){.x = {sampler, builder}});
            }
            nvn_st_add_ret(rec, "result", nvn_st_call(c, pcmp, &(NvnStCall){.x = {a, b}}));
        }
        nvn_st_compare(&ref, &got, &d);
        nvn_st_report(run, "nvnSamplerCompare", labels[i], &ref, &d);
    }
}

/* nvnTextureViewCompare: two views built with the same setters, then compared. */
static void case_texture_view_compare(CPU* c, NvnStRun* run, uint64_t a, uint64_t b) {
    NvnStProc def = {0}, format = {0}, cmp = {0};
    if (!nvn_st_proc_find(&def, "nvnTextureViewSetDefaults") ||
        !nvn_st_proc_find(&format, "nvnTextureViewSetFormat") || !nvn_st_proc_find(&cmp, "nvnTextureViewCompare")) {
        run->skipped++;
        LOG("selftest: nvnTextureViewCompare skipped (a function was not resolved, or the host has no handler)\n");
        return;
    }
    static const uint32_t formats[][2] = {{0x25, 0x25}, {0x25, 0x29}};
    static const char* const labels[] = {"equal", "different"};
    for (size_t i = 0; i < ARRAY_LEN(formats); i++) {
        NvnStRec ref = {0}, got = {0};
        NvnStDiff d;
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            const NvnStCallee* pdef = host ? &def.host : &def.lib;
            const NvnStCallee* pformat = host ? &format.host : &format.lib;
            const NvnStCallee* pcmp = host ? &cmp.host : &cmp.lib;
            for (int which = 0; which < 2; which++) {
                uint64_t view = which ? b : a;
                memset(GP(view), 0, NVN_ST_OBJ_BYTES);
                nvn_st_call(c, pdef, &(NvnStCall){.x = {view}});
                nvn_st_call(c, pformat, &(NvnStCall){.x = {view, formats[i][which]}});
            }
            nvn_st_add_ret(rec, "result", nvn_st_call(c, pcmp, &(NvnStCall){.x = {a, b}}));
        }
        nvn_st_compare(&ref, &got, &d);
        nvn_st_report(run, "nvnTextureViewCompare", labels[i], &ref, &d);
    }
}

void nvn_selftest_object_cases(CPU* c, NvnStRun* run) {
    uint64_t blocks = nvn_st_arena_alloc(&run->arena, (uint64_t)SLOT_COUNT * NVN_ST_OBJ_BYTES, 16);
    uint64_t out_lib = nvn_st_arena_alloc(&run->arena, NVN_ST_MAX_GETS * NVN_ST_OUT_BYTES, 16);
    uint64_t out_host = nvn_st_arena_alloc(&run->arena, NVN_ST_MAX_GETS * NVN_ST_OUT_BYTES, 16);
    uint64_t data = nvn_st_arena_alloc(&run->arena, NVN_ST_DATA_WORDS * sizeof(uint32_t), 16);
    if (!blocks || !out_lib || !out_host || !data) {
        LOG("selftest: the object cases are skipped, the arena is full\n");
        return;
    }
    for (size_t i = 0; i < ARRAY_LEN(kinds); i++) run_kind(c, run, &kinds[i], blocks, out_lib, out_host, data);
    case_sampler_compare(c, run, blocks + (uint64_t)SLOT_SAMPLER_BUILDER * NVN_ST_OBJ_BYTES,
                         blocks + (uint64_t)SLOT_SAMPLER_A * NVN_ST_OBJ_BYTES,
                         blocks + (uint64_t)SLOT_SAMPLER_B * NVN_ST_OBJ_BYTES);
    case_texture_view_compare(c, run, blocks + (uint64_t)SLOT_TEXTURE_VIEW * NVN_ST_OBJ_BYTES,
                              blocks + (uint64_t)SLOT_TEXTURE_VIEW_B * NVN_ST_OBJ_BYTES);
}
