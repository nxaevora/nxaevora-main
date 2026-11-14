/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The enum and flag arguments of the NVN trace (gpu/nvn/diag/nvn_enum_trace.c): the register mask of each function from the
   analysis table, and the seen set that keeps one line for each new value. */
#include "nx/gpu/nvn/diag/nvn_enum_trace.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_args(void) {
    CHECK(nvn_enum_args("nvnTextureBuilderSetFormat")->regs == 0x02);
    CHECK(nvn_enum_args("nvnTextureBuilderSetSwizzle")->regs == 0x1e);
    CHECK(nvn_enum_args("nvnDepthStencilStateSetStencilOp")->regs == 0x1e);
    CHECK(nvn_enum_args("nvnQueueFenceSync")->regs == 0x08);
    CHECK(nvn_enum_args("nvnCommandBufferClearColor")->regs == 0x0a);
    CHECK(nvn_enum_args("nvnCommandBufferDrawElements")->regs == 0x06);
    CHECK(nvn_enum_args("nvnSamplerBuilderSetCompare")->regs == 0x02);
    CHECK(nvn_enum_args("nvnChannelMaskStateSetChannelMask")->regs == 0x02);
    CHECK(nvn_enum_args("nvnSamplerBuilderSetWrapMode")->regs == 0x0e);
    CHECK(!nvn_enum_args("nvnDeviceGetInteger"));
    CHECK(!nvn_enum_args("nvnCommandBufferClearDepthStencil"));
    CHECK(!nvn_enum_args("nvnDepthStencilStateSetDepthTestEnable"));
    CHECK(!nvn_enum_args("nvnFoo"));
    CHECK(!nvn_enum_args(NULL));
}

static void test_seen(void) {
    NvnEnumSeen s;
    memset(&s, 0, sizeof s);
    CHECK(nvn_enum_seen_add(&s, 1, 5) == 1);
    CHECK(nvn_enum_seen_add(&s, 1, 5) == 0);
    CHECK(nvn_enum_seen_add(&s, 2, 5) == 1);
    for (uint32_t i = s.n; i < NVN_ENUM_SEEN_MAX; i++) CHECK(nvn_enum_seen_add(&s, 3, i) == 1);
    CHECK(s.n == NVN_ENUM_SEEN_MAX);
    CHECK(nvn_enum_seen_add(&s, 4, 0) == -1); /* full and new: not added */
    CHECK(s.n == NVN_ENUM_SEEN_MAX);
    CHECK(nvn_enum_seen_add(&s, 1, 5) == 0); /* full and known */
}

int main(void) {
    test_args();
    test_seen();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_enum_trace: ok\n");
    return 0;
}
