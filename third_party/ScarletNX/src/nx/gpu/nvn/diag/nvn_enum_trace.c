/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The enum and flag arguments of the observed NVN functions (gpu/nvn/diag/nvn_enum_trace.h). Table 1 of the analysis of the
   sdk module, rows of kind enum or flags, without the bool enables (a test of the low byte) and without
   nvnDeviceGetInteger (already logged as VALUE_GET_INTEGER). Rows of one function are merged into one mask. */
#include "nx/gpu/nvn/diag/nvn_enum_trace.h"
#include <string.h>

static const NvnEnumArgs table[] = {
    {"nvnDeviceBuilderSetFlags", 0x02},
    {"nvnDeviceSetDepthMode", 0x02},
    {"nvnDeviceSetWindowOriginMode", 0x02},
    {"nvnQueueBuilderSetFlags", 0x02},
    {"nvnQueueBuilderSetQueuePriority", 0x02},
    {"nvnMemoryPoolBuilderSetFlags", 0x02},
    {"nvnTextureBuilderSetFlags", 0x02},
    {"nvnTextureBuilderSetFormat", 0x02},
    {"nvnTextureBuilderSetTarget", 0x02},
    {"nvnTextureBuilderSetDepthStencilMode", 0x02},
    {"nvnTextureBuilderSetSwizzle", 0x1e},
    {"nvnWindowSetPresentInterval", 0x02},
    {"nvnCommandBufferBarrier", 0x02},
    {"nvnCommandBufferClearColor", 0x0a},
    {"nvnQueueFenceSync", 0x08},
    {"nvnBlendStateSetBlendEquation", 0x06},
    {"nvnBlendStateSetBlendFunc", 0x1e},
    {"nvnBlendStateSetBlendTarget", 0x02},
    {"nvnChannelMaskStateSetChannelMask", 0x02},
    {"nvnChannelMaskStateGetChannelMask", 0x02},
    {"nvnColorStateSetAlphaTest", 0x02},
    {"nvnColorStateSetBlendEnable", 0x02},
    {"nvnColorStateGetBlendEnable", 0x02},
    {"nvnColorStateSetLogicOp", 0x02},
    {"nvnDepthStencilStateSetDepthFunc", 0x02},
    {"nvnDepthStencilStateSetStencilFunc", 0x06},
    {"nvnDepthStencilStateSetStencilOp", 0x1e},
    {"nvnDepthStencilStateGetStencilFunc", 0x02},
    {"nvnDepthStencilStateGetStencilOp", 0x02},
    {"nvnMultisampleStateSetCoverageModulationMode", 0x02},
    {"nvnMultisampleStateSetCoverageToColorOutput", 0x02},
    {"nvnMultisampleStateSetRasterSamples", 0x02},
    {"nvnMultisampleStateSetSamples", 0x02},
    {"nvnPolygonStateSetCullFace", 0x02},
    {"nvnPolygonStateSetFrontFace", 0x02},
    {"nvnPolygonStateSetPolygonMode", 0x02},
    {"nvnPolygonStateSetPolygonOffsetEnables", 0x02},
    {"nvnVertexAttribStateSetFormat", 0x02},
    {"nvnCommandBufferBindProgram", 0x04},
    {"nvnCommandBufferBindStorageBuffer", 0x02},
    {"nvnCommandBufferBindTexture", 0x02},
    {"nvnCommandBufferBindUniformBuffer", 0x02},
    {"nvnCommandBufferBindImage", 0x02},
    {"nvnCommandBufferDrawArrays", 0x02},
    {"nvnCommandBufferDrawArraysInstanced", 0x02},
    {"nvnCommandBufferDrawElements", 0x06},
    {"nvnCommandBufferDrawElementsBaseVertex", 0x06},
    {"nvnCommandBufferDrawElementsInstanced", 0x06},
    {"nvnCommandBufferSetStencilMask", 0x02},
    {"nvnCommandBufferSetStencilRef", 0x02},
    {"nvnCommandBufferSetStencilValueMask", 0x02},
    {"nvnCommandBufferClearTexture", 0x20},
    {"nvnCommandBufferCopyTextureToTexture", 0x80},
    {"nvnCommandBufferReportCounter", 0x02},
    {"nvnCommandBufferResetCounter", 0x02},
    {"nvnTextureViewSetDepthStencilMode", 0x02},
    {"nvnTextureViewSetFormat", 0x02},
    {"nvnTextureViewSetSwizzle", 0x1e},
    {"nvnTextureViewSetTarget", 0x02},
    {"nvnSamplerBuilderSetMinMagFilter", 0x06},
    {"nvnSamplerBuilderSetWrapMode", 0x0e},
    {"nvnSamplerBuilderSetCompare", 0x02},
    {"nvnSamplerBuilderSetReductionFilter", 0x02},
};

const NvnEnumArgs* nvn_enum_args(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!strcmp(table[i].name, name)) return &table[i];
    return NULL;
}

int nvn_enum_seen_add(NvnEnumSeen* s, uint32_t reg, uint32_t value) {
    uint64_t key = (uint64_t)reg << 32 | value;
    for (uint32_t i = 0; i < s->n; i++)
        if (s->keys[i] == key) return 0;
    if (s->n == NVN_ENUM_SEEN_MAX) return -1;
    s->keys[s->n++] = key;
    return 1;
}
