/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The render target words of gpu/nvn/nvn_rt.c: every word of a run, by hand from the rules of the task brief. */
#include "nx/gpu/nvn/nvn_rt.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

enum { MOCK_MAX_WRITES = 512 };

typedef struct { uint32_t method, value; } MockWrite;

typedef struct {
    MockWrite writes[MOCK_MAX_WRITES];
    uint32_t write_count;
    uint32_t fault_count;
    const char* last_fault;
    uint64_t last_fault_value;
} Mock;

static void mock_method(void* ctx, uint32_t method, uint32_t value) {
    Mock* k = ctx;
    if (k->write_count < MOCK_MAX_WRITES)
        k->writes[k->write_count++] = (MockWrite){method, value};
}

static void mock_fault(void* ctx, const char* what, uint64_t value) {
    Mock* k = ctx;
    k->fault_count++;
    k->last_fault = what;
    k->last_fault_value = value;
}

static NvnRtEnv make_env(Mock* k) {
    NvnRtEnv env;
    memset(&env, 0, sizeof env);
    env.ctx = k;
    env.method = mock_method;
    env.fault = mock_fault;
    return env;
}

/* The copy takes the format word from the game's table column +0x18: the tests give each index its own word. */
static uint32_t copy_format_word_stub(void* ctx, uint32_t format) {
    (void)ctx;
    return 0x5000u + format;
}

static uint32_t copy_format_word_zero(void* ctx, uint32_t format) {
    (void)ctx;
    (void)format;
    return 0u;
}

static NvnRtCopyEnv make_copy_env(Mock* k, uint32_t (*format_word)(void*, uint32_t)) {
    NvnRtCopyEnv env;
    memset(&env, 0, sizeof env);
    env.base = make_env(k);
    env.format_word = format_word;
    return env;
}

static void expect(const Mock* k, uint32_t index, uint32_t method, uint32_t value) {
    if (index >= k->write_count) {
        printf("FAIL write %u: missing\n", index);
        failures++;
        return;
    }
    if (k->writes[index].method != method || k->writes[index].value != value) {
        printf("FAIL write %u: actual %08x/%08x expected %08x/%08x\n", index,
               k->writes[index].method, k->writes[index].value, method, value);
        failures++;
    }
}

static int same_writes(const Mock* a, const Mock* b) {
    if (a->write_count != b->write_count) return 0;
    for (uint32_t i = 0; i < a->write_count; i++)
        if (a->writes[i].method != b->writes[i].method || a->writes[i].value != b->writes[i].value)
            return 0;
    return 1;
}

static int fault_is(const Mock* k, const char* what) {
    return k->fault_count == 1 && k->last_fault && strcmp(k->last_fault, what) == 0;
}

static int words_have(const uint32_t* words, uint32_t count, uint32_t value) {
    for (uint32_t i = 0; i < count; i++)
        if (words[i] == value) return 1;
    return 0;
}

static NvnTexture make_texture_with(uint32_t format, uint32_t width, uint32_t height, uint64_t address,
                                    uint16_t tile, uint32_t samples) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = 1;
    t.builder.tile = tile;
    t.builder.samples = samples;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL texture layout: format %08x %ux%u\n", format, width, height);
        failures++;
    }
    t.address = address;
    return t;
}

static NvnTexture make_texture(uint32_t format, uint32_t width, uint32_t height, uint64_t address) {
    return make_texture_with(format, width, height, address, 0u, 0u);
}

static NvnTexture make_linear_texture(uint32_t format, uint32_t width, uint32_t height, uint64_t address) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = 1;
    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL linear texture layout: format %08x %ux%u\n", format, width, height);
        failures++;
    }
    t.address = address;
    return t;
}

static NvnTexture make_array_texture(uint32_t format, uint32_t width, uint32_t height, uint32_t depth,
                                     uint64_t address) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = depth;
    t.builder.target = NVN_TEXTURE_TARGET_2D_ARRAY;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL array texture layout: format %08x %ux%u depth %u\n", format, width, height, depth);
        failures++;
    }
    t.address = address;
    return t;
}

static void test_one_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 0, 0x487, 0x0FAC6881u);
    expect(&k, 1, 0x200, 0x00000004u);
    expect(&k, 2, 0x201, 0x12345600u);
    expect(&k, 3, 0x202, 1280u);
    expect(&k, 4, 0x203, 720u);
    expect(&k, 5, 0x204, 0x000000D5u);
    expect(&k, 6, 0x205, 0x00000040u);
    expect(&k, 7, 0x206, 0x00000001u);
    expect(&k, 8, 0x207, 0x000F0000u);
    expect(&k, 9, 0x214, 0u);
    expect(&k, 10, 0x224, 0u);
    expect(&k, 11, 0x234, 0u);
    expect(&k, 12, 0x244, 0u);
    expect(&k, 13, 0x254, 0u);
    expect(&k, 14, 0x264, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x54E, 0u);
    expect(&k, 17, 0xD28, 0u);
    expect(&k, 18, 0x3FD, 0x05000000u);
    expect(&k, 19, 0x3FE, 0x02D00000u);
    expect(&k, 20, 0x574, 0u);
}

static void test_null_slot_zero(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000123456789ull);
    NvnRtColor color[2] = {{NULL, NULL}, {&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 2, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 0, 0x487, 0x0FAC6882u);
    expect(&k, 1, 0x204, 0u);
    expect(&k, 2, 0x210, 0x00000001u);
    expect(&k, 3, 0x211, 0x23456789u);
    expect(&k, 4, 0x212, 1280u);
    expect(&k, 5, 0x213, 720u);
    expect(&k, 6, 0x214, 0x000000D5u);
    expect(&k, 7, 0x215, 0x00000040u);
    expect(&k, 8, 0x216, 0x00000001u);
    expect(&k, 9, 0x217, 0x000F0000u);
    expect(&k, 10, 0x224, 0u);
    expect(&k, 11, 0x234, 0u);
    expect(&k, 12, 0x244, 0u);
    expect(&k, 13, 0x254, 0u);
    expect(&k, 14, 0x264, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x54E, 0u);
    expect(&k, 17, 0xD28, 0u);
    expect(&k, 18, 0x3FD, 0x05000000u);
    expect(&k, 19, 0x3FE, 0x02D00000u);
    expect(&k, 20, 0x574, 0u);
}

static void test_no_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    CHECK(nvn_rt_emit(&env, NULL, 0, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 14);
    expect(&k, 0, 0x487, 0x0FAC6880u);
    expect(&k, 1, 0x204, 0u);
    expect(&k, 2, 0x214, 0u);
    expect(&k, 3, 0x224, 0u);
    expect(&k, 4, 0x234, 0u);
    expect(&k, 5, 0x244, 0u);
    expect(&k, 6, 0x254, 0u);
    expect(&k, 7, 0x264, 0u);
    expect(&k, 8, 0x274, 0u);
    expect(&k, 9, 0x54E, 0u);
    expect(&k, 10, 0xD28, 0u);
    expect(&k, 11, 0x3FD, 0x40000000u);
    expect(&k, 12, 0x3FE, 0x40000000u);
    expect(&k, 13, 0x574, 0u);
}

static void test_clip_limit_zero(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0) == 1);
    expect(&k, 18, 0x3FD, 0u);
    expect(&k, 19, 0x3FE, 0u);
}

static void test_width_align_up(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 100, 16, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 1, 0x200, 0x00000000u);
    expect(&k, 2, 0x201, 0x00001000u);
    expect(&k, 3, 0x202, 112u);
    expect(&k, 4, 0x203, 16u);
    expect(&k, 5, 0x204, 0x000000D5u);
    expect(&k, 6, 0x205, 0x00000010u);
    expect(&k, 7, 0x206, 0x00000001u);
    expect(&k, 8, 0x207, 0x00000700u);
}

static void test_depth_only_d32f(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x0000000212345600ull);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 22);
    expect(&k, 0, 0x487, 0x0FAC6880u);
    for (uint32_t j = 0; j < NVN_RT_COLOR_MAX; j++)
        expect(&k, 1u + j, 0x204u + 0x10u * j, 0u);
    expect(&k, 9, 0x3F8, 0x00000002u);
    expect(&k, 10, 0x3F9, 0x12345600u);
    expect(&k, 11, 0x3FA, 0x0000000Au);
    expect(&k, 12, 0x3FB, 0x00000040u);
    expect(&k, 13, 0x3FC, 0x000F0000u);
    expect(&k, 16, 0x48A, 1280u);
    expect(&k, 17, 0x48B, 720u);
    expect(&k, 18, 0x48C, 1u);
    expect(&k, 14, 0x54E, 0u);
    expect(&k, 15, 0x54E, 1u);
    expect(&k, 19, 0x3FD, 0x05000000u);
    expect(&k, 20, 0x3FE, 0x02D00000u);
    expect(&k, 21, 0x574, 0u);
}

static void test_color_plus_depth(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x0000000212345600ull);
    NvnRtColor color[1] = {{&c, NULL}};
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 29);
    expect(&k, 0, 0x487, 0x0FAC6881u);
    expect(&k, 8, 0x207, 0x000F0000u);
    expect(&k, 9, 0x214, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x3F8, 0x00000002u);
    expect(&k, 17, 0x3F9, 0x12345600u);
    expect(&k, 18, 0x3FA, 0x0000000Au);
    expect(&k, 19, 0x3FB, 0x00000040u);
    expect(&k, 20, 0x3FC, 0x000F0000u);
    expect(&k, 23, 0x48A, 1280u);
    expect(&k, 24, 0x48B, 720u);
    expect(&k, 25, 0x48C, 1u);
    expect(&k, 21, 0x54E, 0u);
    expect(&k, 22, 0x54E, 1u);
    expect(&k, 26, 0x3FD, 0x05000000u);
    expect(&k, 27, 0x3FE, 0x02D00000u);
    expect(&k, 28, 0x574, 0u);
}

static void test_small_target_factor(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 64, 4, 0x1000);
    NvnRtColor color[1] = {{&c, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 3, 0x202, 64u);
    expect(&k, 4, 0x203, 4u);
    expect(&k, 6, 0x205, 0u);

    NvnTexture d = make_texture(0x34, 64, 4, 0x2000);
    NvnRtColor depth = {&d, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 16, 0x48A, 64u);
    expect(&k, 17, 0x48B, 4u);
    expect(&k, 12, 0x3FB, 0u);
}

static void test_small_target_tile(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    /* tile_x 2 gives factor 4 only from width 16 GOB blocks << 2 = 64 on. */
    NvnTexture c = make_texture_with(0x25, 48, 4, 0x1000, 0x2001u, 0);
    NvnRtColor color[1] = {{&c, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    /* rt_tile_factor: 48 < 64 gives factor 1; rt_align_up(48, 1 * 16) = 48. */
    expect(&k, 3, 0x202, 48u);

    /* Width 80 passes the tile, but height 4 < 8 << tile_y 1 = 16 gives factor 1. */
    NvnTexture wide = make_texture_with(0x25, 80, 4, 0x1000, 0x2011u, 0);
    NvnRtColor wide_color[1] = {{&wide, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, wide_color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    /* 80 >= 64 and the height fails, so rt_align_up(80, 1 * 16) = 80. */
    expect(&k, 3, 0x202, 80u);

    NvnTexture d = make_texture_with(0x34, 48, 4, 0x2000, 0x2001u, 0);
    NvnRtColor depth = {&d, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    /* 0x48A gets rt_align_up(48, 1 * 16) = 48. */
    expect(&k, 16, 0x48A, 48u);

    NvnTexture wide_depth = make_texture_with(0x34, 80, 4, 0x2000, 0x2011u, 0);
    NvnRtColor wide_depth_color = {&wide_depth, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &wide_depth_color, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    /* The height 4 is below 16, so rt_align_up(80, 1 * 16) = 80. */
    expect(&k, 16, 0x48A, 80u);
}

static void test_view_layer_bounds(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;

    NvnTexture c = make_array_texture(0x25, 64, 64, 4, 0x1000);
    NvnRtColor color[1] = {{&c, &view}};
    view.base_layer = 4;
    view.layer_count = 1;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 5u);

    memset(&k, 0, sizeof k);
    view.base_layer = 3;
    view.layer_count = 2;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 5u);

    memset(&k, 0, sizeof k);
    view.base_layer = 3;
    view.layer_count = 1;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 2, 0x201, (uint32_t)(c.address + (uint64_t)3 * c.layout.layer_stride));
    expect(&k, 7, 0x206, 1u);

    NvnTexture d = make_array_texture(0x34, 64, 64, 4, 0x2000);
    NvnRtColor depth = {&d, &view};
    view.base_layer = 4;
    view.layer_count = 1;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 5u);

    memset(&k, 0, sizeof k);
    view.base_layer = 3;
    view.layer_count = 2;
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 5u);

    memset(&k, 0, sizeof k);
    view.base_layer = 3;
    view.layer_count = 1;
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 10, 0x3F9, (uint32_t)(d.address + (uint64_t)3 * d.layout.layer_stride));
    expect(&k, 18, 0x48C, 1u);
}

static void test_depth_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_array_texture(0x34, 1, 1, 0xFFFFu, 0x1000);
    NvnRtColor depth = {&t, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 0xFFFFu);

    NvnTexture many = make_array_texture(0x34, 1, 1, 0x10000u, 0x1000);
    NvnRtColor many_depth = {&many, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &many_depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt depth layers") && k.last_fault_value == 0x10000u);
}

static void test_view_target_unknown(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_TARGET;
    view.target = 0xFFFFFFFFu;

    NvnTexture c = make_texture(0x25, 64, 64, 0x1000);
    NvnRtColor color[1] = {{&c, &view}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view target") && k.last_fault_value == 0xFFFFFFFFu);

    NvnTexture d = make_texture(0x34, 64, 64, 0x2000);
    NvnRtColor depth = {&d, &view};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 1u);
}

static void test_sample_mode_last_present(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c0 = make_texture_with(0x25, 64, 64, 0x1000, 0u, 4u);
    NvnTexture c1 = make_texture_with(0x25, 64, 64, 0x2000, 0u, 8u);
    NvnRtColor color[3] = {{&c0, NULL}, {&c1, NULL}, {NULL, NULL}};
    CHECK(nvn_rt_emit(&env, color, 3, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    /* The sample mode 4 comes from the present slot 1 (8 samples), not from slot 0 (4 samples, mode 2). */
    expect(&k, 27, 0x574, 4u);
    /* The absent slot 2 writes its disable word at 0x204 + 0x10 * 2 = 0x224. */
    expect(&k, 17, 0x224, 0u);
}

static void test_depth_view_address(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_array_texture(0x34, 1280, 720, 4, 0x0000000212345600ull);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;
    view.base_layer = 1;
    view.layer_count = 2;
    NvnRtColor depth = {&d, &view};
    uint64_t address = d.address + (uint64_t)1 * d.layout.layer_stride;
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 9, 0x3F8, (uint32_t)(address >> 32));
    expect(&k, 10, 0x3F9, (uint32_t)address);
    expect(&k, 18, 0x48C, 2u);
}

static void test_tile_x_nibble(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture_with(0x25, 1280, 720, 0x1000, 0x1001u, 0);
    NvnRtColor color[1] = {{&c, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 3, 0x202, 1280u);
    expect(&k, 6, 0x205, 0x00000001u);

    NvnTexture d = make_texture_with(0x34, 1280, 720, 0x2000, 0x1001u, 0);
    NvnRtColor depth = {&d, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 12, 0x3FB, 0x00000001u);
    expect(&k, 16, 0x48A, 1280u);
}

static void test_clip_includes_depth(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 1280, 720, 0x1000);
    NvnTexture d = make_texture(0x34, 64, 32, 0x2000);
    NvnRtColor color[1] = {{&c, NULL}};
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 29);
    expect(&k, 26, 0x3FD, 0x00400000u);
    expect(&k, 27, 0x3FE, 0x00200000u);
}

static void test_sample_mode(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture_with(0x34, 1280, 720, 0x2000, 0u, 4u);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 21, 0x574, 2u);

    NvnTexture c = make_texture_with(0x25, 1280, 720, 0x1000, 0u, 2u);
    NvnRtColor color[1] = {{&c, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 28, 0x574, 5u);
}

static void test_depth_view_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_array_texture(0x34, 1280, 720, 8, 0x0000000212345600ull);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;
    view.base_layer = 3;
    view.layer_count = 5;
    NvnRtColor depth = {&d, &view};
    uint64_t address = d.address + (uint64_t)3 * d.layout.layer_stride;
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 9, 0x3F8, (uint32_t)(address >> 32));
    expect(&k, 10, 0x3F9, (uint32_t)address);
    expect(&k, 18, 0x48C, 5u);
}

static void test_depth_view_checks(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x1000);
    NvnTextureView view;

    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    view.level_count = 1;
    NvnRtColor levels = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    memset(&k, 0, sizeof k);
    view.base_level = 0;
    view.level_count = 2;
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 1u);

    memset(&k, 0, sizeof k);
    view.base_level = 0;
    view.level_count = 1;
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 1);
    CHECK(k.fault_count == 0);

    memset(&k, 0, sizeof k);
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_FORMAT;
    view.format = 0x33;
    NvnRtColor format = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &format, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view format") && k.last_fault_value == 0x33u);

    memset(&k, 0, sizeof k);
    view.format = 0x34;
    CHECK(nvn_rt_emit(&env, NULL, 0, &format, 0x4000) == 1);
    CHECK(k.fault_count == 0);

    memset(&k, 0, sizeof k);
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_SWIZZLE | NVN_VIEW_DEPTH_STENCIL_MODE | NVN_VIEW_TARGET;
    view.base_layer = 7;
    view.layer_count = 3;
    NvnRtColor ignored = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &ignored, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 1u);
}

static void test_depth_linear(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_linear_texture(0x34, 64, 64, 0x1000);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);
}

static void test_2d_array_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture array = make_texture(0x25, 64, 64, 0x1000);
    array.builder.target = NVN_TEXTURE_TARGET_2D_ARRAY;
    array.layout.depth = 4;
    NvnRtColor color[1] = {{&array, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 7, 0x206, 4u);
}

static void test_refusals(void) {
    Mock k;
    NvnRtEnv env;
    NvnTexture good = make_texture(0x25, 1280, 720, 0x1000);

    memset(&k, 0, sizeof k);
    env = make_env(&k);
    NvnRtColor many[NVN_RT_COLOR_MAX + 1u] = {0};
    CHECK(nvn_rt_emit(&env, many, NVN_RT_COLOR_MAX + 1u, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 9u);

    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    NvnRtColor with_view[1] = {{&good, &view}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, with_view, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    NvnTexture not_2d = make_texture(0x25, 64, 64, 0x1000);
    not_2d.builder.target = NVN_TEXTURE_TARGET_2D_MS;
    NvnRtColor bad_target[1] = {{&not_2d, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_target, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_TARGET_2D_MS);

    NvnTexture levels;
    memset(&levels, 0, sizeof levels);
    nvn_texture_builder_defaults(&levels.builder);
    levels.builder.format = 0x25;
    levels.builder.width = 64;
    levels.builder.height = 64;
    levels.builder.depth = 1;
    levels.builder.levels = 2;
    levels.address = 0x1000;
    CHECK(nvn_texture_layout(&levels.builder, &nvn_texture_device_words, &levels.layout));
    NvnRtColor bad_levels[1] = {{&levels, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_levels, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 2u);

    NvnTexture linear = make_linear_texture(0x25, 64, 64, 0x1000);
    NvnRtColor bad_linear[1] = {{&linear, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_linear, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);

    NvnTexture bad_format = make_texture(0x39, 64, 64, 0x1000);
    NvnRtColor bad_format_color[1] = {{&bad_format, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_format_color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x39u);

    NvnTexture compressed = make_texture(0x41, 16, 16, 0x1000);
    NvnRtColor bad_block[1] = {{&compressed, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_block, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x41u);

    NvnTexture bad_depth = make_texture(0x34, 64, 64, 0x1000);
    bad_depth.layout.bytes_per_block = 0;
    NvnRtColor depth = {&bad_depth, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0u);
}

static void test_bad_input(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    CHECK(nvn_rt_emit(&env, NULL, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);

    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    t.layout.bytes_per_block = 0;
    NvnRtColor color[1] = {{&t, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);
}

static void test_null_env_fields(void) {
    Mock k = {0};
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(NULL, color, 1, NULL, 0x4000) == 0);

    NvnRtEnv env = make_env(&k);
    env.method = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);

    env = make_env(&k);
    env.fault = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);
}

static void test_color_view_no_change(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnRtColor plain[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, plain, 1, NULL, 0x4000) == 1);
    Mock base = k;

    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.level_count = 1;
    view.layer_count = 1;
    view.format = 0x25;
    view.target = NVN_TEXTURE_TARGET_2D;
    NvnRtColor seen[1] = {{&t, &view}};

    view.flags = 0x3fu;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, seen, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && same_writes(&base, &k));

    view.flags = 0x27u;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, seen, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && same_writes(&base, &k));
}

static void test_color_view_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_array_texture(0x25, 1280, 720, 8, 0x0000000412345600ull);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;
    view.base_layer = 3;
    view.layer_count = 5;
    NvnRtColor color[1] = {{&t, &view}};
    uint64_t address = t.address + (uint64_t)3 * t.layout.layer_stride;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 1, 0x200, (uint32_t)(address >> 32));
    expect(&k, 2, 0x201, (uint32_t)address);
    expect(&k, 7, 0x206, 5u);
    expect(&k, 8, 0x207, (uint32_t)(t.layout.layer_stride >> 2));

    view.base_layer = 0;
    view.layer_count = 0;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 2, 0x201, 0x12345600u);
    expect(&k, 7, 0x206, 8u);

    view.base_layer = 9;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 9u);
}

static void test_color_view_target_3d(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.target = NVN_TEXTURE_TARGET_3D;
    NvnRtColor color[1] = {{&t, &view}};

    view.flags = NVN_VIEW_TARGET;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 6, 0x205, 0x00010040u);

    view.flags = NVN_VIEW_SWIZZLE;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 6, 0x205, 0x00000040u);
}

static void test_color_view_checks(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    NvnRtColor color[1] = {{&t, &view}};

    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    memset(&k, 0, sizeof k);
    view.flags = NVN_VIEW_FORMAT;
    view.format = 0x26;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view format") && k.last_fault_value == 0x26u);

    memset(&k, 0, sizeof k);
    view.flags = 0x40u;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view flags") && k.last_fault_value == 0x40u);
}

static void test_clear_texture_one_layer(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    uint32_t region[6] = {0, 0, 0, 320, 240, 1};
    uint32_t color[4] = {0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u};
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    int nothing = -1;
    uint32_t count = nvn_rt_clear_texture_words(&env, &t, NULL, region, color, 0xFu, 0x4000u, &nothing,
                                                words);
    /* verify-s4-4b.md, "ClearTexture": the full list for one layer with no view. */
    CHECK(count == 25 && nothing == 0 && k.fault_count == 0);
    CHECK(words[0] == 0x91080002u);
    CHECK(words[1] == 0x20040360u);
    CHECK(words[2] == color[0] && words[3] == color[1] && words[4] == color[2] && words[5] == color[3]);
    CHECK(words[6] == 0x80020049u);
    CHECK(words[7] == 0xA0040E1Eu);
    CHECK(words[8] == 0x04123456u);
    CHECK(words[9] == 1280u && words[10] == 720u);
    CHECK(words[11] == 0xD5000040u);
    CHECK(words[12] == 0x200203FDu);
    CHECK(words[13] == 0x01400000u && words[14] == 0x00F00000u);
    CHECK(words[15] == 0x20010674u && words[16] == 0x3Cu);
    CHECK(words[17] == 0x80030049u && words[18] == 0xA0040E1Eu);
    CHECK(words[19] == 0x04123456u && words[20] == 1280u && words[21] == 720u && words[22] == 0xD5000040u);
    CHECK(words[23] == 0x80010049u && words[24] == 0x91000002u);
}

static void test_clear_texture_two_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_array_texture(0x25, 64, 64, 4, 0x1000);
    uint32_t region[6] = {0, 0, 1, 8, 8, 2};
    uint32_t color[4] = {0, 0, 0, 0};
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    int nothing = -1;
    uint32_t count = nvn_rt_clear_texture_words(&env, &t, NULL, region, color, 0x3u, 0x4000u, &nothing,
                                                words);
    uint32_t layer1 = (uint32_t)((t.address + t.layout.layer_stride) >> 8);
    uint32_t layer2 = (uint32_t)((t.address + 2u * t.layout.layer_stride) >> 8);
    /* verify-s4-4b.md, "ClearTexture": region[2] is a, so the second group is layer a+1 and the tail repeats it. */
    CHECK(count == 35 && nothing == 0 && k.fault_count == 0);
    CHECK(words[7] == 0xA0040E1Eu && words[8] == layer1);
    CHECK(words[17] == 0xA0040E1Eu && words[18] == layer2);
    CHECK(words[19] == 64u && words[20] == 64u);
    CHECK(words[27] == 0x80030049u && words[28] == 0xA0040E1Eu);
    CHECK(words[29] == layer2 && words[30] == 64u && words[31] == 64u);
    CHECK(words[33] == 0x80010049u && words[34] == 0x91000002u);
}

static void test_clear_texture_clip(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    uint32_t color[4] = {0, 0, 0, 0};
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    int nothing = -1;

    uint32_t region[6] = {0xFFFFFFF0u, 0, 0, 32, 6, 1};
    CHECK(nvn_rt_clear_texture_words(&env, &t, NULL, region, color, 0xFu, 8u, &nothing, words) == 25);
    /* verify-s4-4b.md, correction 2: a negative origin moves the span, and the limit clips the width. */
    CHECK(words[13] == 0x00080000u && words[14] == 0x00060000u);
    /* The same region with a wide limit: the span is 32 - 16 = 16 from origin 0. */
    CHECK(nvn_rt_clear_texture_words(&env, &t, NULL, region, color, 0xFu, 0x4000u, &nothing, words) == 25);
    CHECK(words[13] == 0x00100000u);

    NvnTexture ms = make_texture_with(0x25, 1280, 720, 0x1000, 0u, 4u);
    uint32_t sampled[6] = {2, 3, 0, 5, 4, 1};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_clear_texture_words(&env, &ms, NULL, sampled, color, 0xFu, 0x4000u, &nothing, words) == 25);
    /* verify-s4-4b.md, correction 2: a 4x texture has the sample scale sx = sy = 2. */
    CHECK(words[13] == 0x000A0004u && words[14] == 0x00080006u);
}

static void test_clear_texture_nothing(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t color[4] = {0, 0, 0, 0};
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    static const uint32_t base[6] = {0, 0, 0, 8, 8, 1};
    uint32_t region[6];
    /* verify-s4-4b.md, correction 1: a zero layer count, width count or height count writes no word. */
    for (uint32_t field = 3; field <= 5; field++) {
        memcpy(region, base, sizeof region);
        region[field] = 0;
        words[0] = 0xDEADBEEFu;
        int nothing = -1;
        memset(&k, 0, sizeof k);
        CHECK(nvn_rt_clear_texture_words(&env, &t, NULL, region, color, 0xFu, 0x4000u, &nothing, words) == 0);
        CHECK(nothing == 1 && k.fault_count == 0 && words[0] == 0xDEADBEEFu);
    }
}

static void test_clear_texture_refusals(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 8, 8, 1};
    uint32_t color[4] = {0, 0, 0, 0};
    uint32_t words[NVN_RT_CLEAR_WORDS_MAX];
    int nothing = -1;

    NvnTexture linear = make_linear_texture(0x25, 64, 64, 0x1000);
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_clear_texture_words(&env, &linear, NULL, region, color, 0xFu, 0x4000u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "rt clear linear") && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);

    NvnTexture target = make_texture(0x25, 64, 64, 0x1000);
    target.builder.target = NVN_TEXTURE_TARGET_3D;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_clear_texture_words(&env, &target, NULL, region, color, 0xFu, 0x4000u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "rt target") && k.last_fault_value == NVN_TEXTURE_TARGET_3D);

    target.builder.target = NVN_TEXTURE_TARGET_1D_ARRAY;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_clear_texture_words(&env, &target, NULL, region, color, 0xFu, 0x4000u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "rt target") && k.last_fault_value == NVN_TEXTURE_TARGET_1D_ARRAY);

    uint32_t many[6] = {0, 0, 0, 8, 8, NVN_RT_CLEAR_LAYERS_MAX + 1u};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_clear_texture_words(&env, &t, NULL, many, color, 0xFu, 0x4000u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "clear layers") && k.last_fault_value == NVN_RT_CLEAR_LAYERS_MAX + 1u);
}

static void test_copy_one_layer(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t src[6] = {0, 0, 0, 64, 64, 1};
    uint32_t dst[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = -1;
    uint32_t count = nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, src, dst, 0u, &nothing, words);
    /* verify-s4-4b.md, "CopyTextureToTexture builder": the first layer, the 0x60B5 word and the tail;
       explorer-s4-4c.md section 7: the start word and the end word. */
    CHECK(count == 43 && nothing == 0 && k.fault_count == 0);
    CHECK(words[0] == 0x91030002u);
    CHECK(words[1] == 0x2004608Cu);
    CHECK(words[2] == 0x5025u && words[3] == 0u && words[4] == 0x30u && words[5] == 1u);
    CHECK(words[6] == 0x20026092u && words[7] == 64u && words[8] == 64u);
    CHECK(words[9] == 0x20026094u && words[10] == 0u && words[11] == 0x1000u);
    CHECK(words[12] == 0x20056080u);
    CHECK(words[13] == 0x5025u && words[14] == 0u && words[15] == 0x30u && words[16] == 1u && words[17] == 0u);
    CHECK(words[18] == 0x20026086u && words[19] == 64u && words[20] == 64u);
    CHECK(words[21] == 0x20026088u && words[22] == 0u && words[23] == 0x1000u);
    CHECK(words[24] == 0x800160B5u);
    CHECK(words[25] == 0x20016223u && words[26] == 1u);
    CHECK(words[27] == 0x2004622Cu && words[28] == 0u && words[29] == 0u && words[30] == 64u && words[31] == 64u);
    CHECK(words[32] == 0x20046230u && words[33] == 0u && words[34] == 1u && words[35] == 0u && words[36] == 1u);
    CHECK(words[37] == 0x20046234u && words[38] == 0x80000000u && words[39] == 0u);
    CHECK(words[40] == 0x80000000u && words[41] == 0u);
    CHECK(words[42] == 0x91000002u);
}

static void test_copy_two_layers(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture src = make_array_texture(0x25, 64, 64, 4, 0x2000);
    NvnTexture dst = make_array_texture(0x25, 64, 64, 4, 0x1000);
    uint32_t sregion[6] = {0, 0, 0, 64, 64, 2};
    uint32_t dregion[6] = {0, 0, 1, 64, 64, 2};
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = -1;
    uint32_t count = nvn_rt_copy_texture_words(&env, &src, NULL, &dst, NULL, sregion, dregion, 0u, &nothing,
                                               words);
    uint32_t src1 = (uint32_t)(src.address + src.layout.layer_stride);
    uint32_t dst1 = (uint32_t)(dst.address + dst.layout.layer_stride);
    uint32_t dst2 = (uint32_t)(dst.address + 2u * dst.layout.layer_stride);
    /* explorer-s4-4c.md section 6: the destination layer rises by 1 and the source layer by step = R5 / S5. */
    CHECK(count == 66 && nothing == 0 && k.fault_count == 0);
    CHECK(words[23] == dst1);
    CHECK(words[24] == 0x800160B5u);
    CHECK(words[42] == 0x20026094u && words[43] == 0u && words[44] == src1);
    CHECK(words[45] == 0x20026088u && words[46] == 0u && words[47] == dst2);
    CHECK(words[48] == 0x20016223u);
    CHECK(words[65] == 0x91000002u);

    /* A source layer count of 4 over 2 destination layers makes the step 2. */
    sregion[5] = 4;
    memset(&k, 0, sizeof k);
    count = nvn_rt_copy_texture_words(&env, &src, NULL, &dst, NULL, sregion, dregion, 0u, &nothing, words);
    CHECK(count == 66 && nothing == 0 && k.fault_count == 0);
    CHECK(words[44] == (uint32_t)(src.address + 2u * src.layout.layer_stride));
    CHECK(words[47] == dst2);
}

static void test_copy_scaling(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture src = make_texture(0x38, 64, 64, 0x2000);
    NvnTexture dst = make_texture(0x38, 64, 64, 0x1000);
    uint32_t sregion[6] = {3, 2, 0, 32, 16, 1};
    uint32_t dregion[6] = {5, 7, 0, 16, 16, 1};
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = -1;
    uint32_t count = nvn_rt_copy_texture_words(&env, &src, NULL, &dst, NULL, sregion, dregion, 1u, &nothing,
                                               words);
    /* explorer-s4-4c.md sections 2, 3 and 5: a doubled source width gives ratioX 0x10000, the offsets use the
       origins, and flags bit0 with ratios other than 0x8000 keeps format 0x38. */
    CHECK(count == 43 && nothing == 0 && k.fault_count == 0);
    CHECK(words[2] == 0x5038u && words[13] == 0x5038u);
    CHECK(words[26] == 0x11u);
    CHECK(words[28] == 5u && words[29] == 7u && words[30] == 16u && words[31] == 16u);
    CHECK(words[33] == 0u && words[34] == 2u && words[35] == 0u && words[36] == 1u);
    CHECK(words[38] == 0u && words[39] == 4u);
    CHECK(words[40] == 0x80000000u && words[41] == 2u);
}

static void test_copy_format_change(void) {
    Mock k;
    NvnRtCopyEnv env;
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = -1;
    uint32_t src[6] = {0, 0, 0, 64, 64, 1};
    uint32_t dst[6] = {0, 0, 0, 64, 64, 1};

    NvnTexture a = make_texture(0x38, 64, 64, 0x1000);
    memset(&k, 0, sizeof k);
    env = make_copy_env(&k, copy_format_word_stub);
    /* explorer-s4-4c.md section 5: equal formats 0x38 become 0x25 and 0x37 becomes 0x6d. */
    CHECK(nvn_rt_copy_texture_words(&env, &a, NULL, &a, NULL, src, dst, 0u, &nothing, words) == 43);
    CHECK(words[2] == 0x5025u && words[13] == 0x5025u);

    NvnTexture b = make_texture(0x37, 64, 64, 0x1000);
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_copy_texture_words(&env, &b, NULL, &b, NULL, src, dst, 0u, &nothing, words) == 43);
    CHECK(words[2] == 0x506Du && words[13] == 0x506Du);

    /* Flags bit0 needs the exact 1:1 ratios for the change; these regions give ratioX = ratioY = 0x8000. */
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_copy_texture_words(&env, &b, NULL, &b, NULL, src, dst, 1u, &nothing, words) == 43);
    CHECK(words[2] == 0x506Du && words[13] == 0x506Du);
}

static void test_copy_nothing(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t src[6] = {0, 0, 0, 64, 64, 1};
    uint32_t base[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    /* explorer-s4-4c.md section 7: a zero width, height or layer count writes no word and no fault. */
    for (uint32_t field = 3; field <= 5; field++) {
        uint32_t dst[6];
        memcpy(dst, base, sizeof dst);
        dst[field] = 0;
        words[0] = 0xDEADBEEFu;
        int nothing = -1;
        memset(&k, 0, sizeof k);
        CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, src, dst, 0u, &nothing, words) == 0);
        CHECK(nothing == 1 && k.fault_count == 0 && words[0] == 0xDEADBEEFu);
    }
}

static void test_copy_refusals(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t src[6] = {0, 0, 0, 64, 64, 1};
    uint32_t dst[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_COPY_WORDS_MAX];
    int nothing = -1;

    NvnTexture linear = make_linear_texture(0x25, 64, 64, 0x1000);
    words[0] = 0xDEADBEEFu;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_copy_texture_words(&env, &linear, NULL, &t, NULL, src, dst, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy linear") && words[0] == 0xDEADBEEFu);

    NvnTexture block = make_texture(0x41, 16, 16, 0x1000);
    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &block, NULL, src, dst, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy block") && k.last_fault_value == 0x41u);

    NvnTexture target = make_texture(0x25, 64, 64, 0x1000);
    target.builder.target = NVN_TEXTURE_TARGET_3D;
    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &target, NULL, &t, NULL, src, dst, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy target") && k.last_fault_value == NVN_TEXTURE_TARGET_3D);

    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, src, dst, 4u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy flags") && k.last_fault_value == 4u);

    uint32_t many[6] = {0, 0, 0, 64, 64, NVN_RT_COPY_LAYERS_MAX + 1u};
    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, src, many, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy layers") && k.last_fault_value == NVN_RT_COPY_LAYERS_MAX + 1u);

    uint32_t wide_src[6] = {0, 0, 0, 0x10000u, 16, 1};
    uint32_t wide_dst[6] = {0, 0, 0, 0xFFFFFFFFu, 16, 1};
    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, wide_src, wide_dst, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy ratio"));

    env = make_copy_env(&k, copy_format_word_zero);
    memset(&k, 0, sizeof k);
    nothing = -1;
    CHECK(nvn_rt_copy_texture_words(&env, &t, NULL, &t, NULL, src, dst, 0u, &nothing, words) == 0);
    CHECK(nothing == 0 && fault_is(&k, "copy format") && k.last_fault_value == 0x25u);
}

static void test_copy_dma_block_linear(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 0u, 0u, 1, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 1 && k.fault_count == 0);
    CHECK(words[0] == 0x20048100u);
    CHECK(words[1] == 0u && words[2] == 0x200000u && words[3] == 0u && words[4] == 0x1000u);
    CHECK(words[5] == 0x20018104u);
    CHECK(words[6] == 256u);
    CHECK(words[7] == 0x200681C3u);
    CHECK(words[8] == 0x1030u && words[9] == 256u && words[10] == 64u && words[11] == 1u);
    CHECK(words[12] == 0u && words[13] == 0u);
    CHECK(words[14] == 0x20028106u && words[15] == 256u && words[16] == 64u);
    CHECK(words[17] == 0x828680C0u); /* type 2, flush, multi-line, src pitch, dst block */
    CHECK(!words_have(words, count, 0x20018105u) && !words_have(words, count, 0x200681CAu));

    memset(&k, 0, sizeof k);
    launches = 0;
    count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 512u, 0u, 1, 0u, &launches, words,
                                  NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 1 && k.fault_count == 0);
    CHECK(words[6] == 512u && words[9] == 256u && words[15] == 256u && words[16] == 64u);
}

static void test_copy_dma_mirror_y(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 64, 2, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, NVN_COPY_FLAG_MIRROR_Y, 0u, 0u, 1,
                                           0u, &launches, words, NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 2 && k.fault_count == 0);
    CHECK(words[2] == 0x200100u && words[4] == 0x1000u && words[16] == 1u && words[13] == 0u);
    count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, NVN_COPY_FLAG_MIRROR_Y, 0u, 0u, 1, 1u,
                                  &launches, words, NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 2 && k.fault_count == 0);
    CHECK(words[2] == 0x200000u && words[4] == 0x1000u && words[16] == 1u && words[13] == 0x10000u);
}

static void test_copy_dma_mirror_z(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_array_texture(0x25, 64, 64, 4, 0x2000);
    uint32_t region[6] = {0, 0, 1, 64, 64, 2};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t stride = (uint32_t)t.layout.layer_stride;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, NVN_COPY_FLAG_MIRROR_Z, 0u, 0u, 1,
                                           0u, &launches, words, NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 2 && k.fault_count == 0);
    CHECK(words[2] == 0x200000u + stride && words[4] == 0x2000u + stride && words[12] == 0u);
    count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, NVN_COPY_FLAG_MIRROR_Z, 0u, 0u, 1, 1u,
                                  &launches, words, NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 2 && k.fault_count == 0);
    CHECK(words[2] == 0x200000u && words[4] == 0x2000u + 2u * stride && words[12] == 0u);
}

static void test_copy_dma_block_compressed(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x41, 8, 8, 0x1000); /* BC1: 4x4 blocks, 8 bytes a block */
    uint32_t region[6] = {0, 0, 0, 6, 6, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 0u, 0u, 1, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 1 && k.fault_count == 0);
    CHECK(words[15] == 16u && words[16] == 2u); /* the 6x6 region is 2x2 whole blocks */
    CHECK(words[9] == 64u && words[10] == 2u && words[11] == 1u);
}

static void test_copy_dma_region_refused(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 128, 64, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    for (unsigned i = 0; i < NVN_RT_DMA_LAUNCH_WORDS; i++) words[i] = 0xAAAAAAAAu;
    uint32_t launches = 9;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 0u, 0u, 1, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 0 && launches == 0 && k.fault_count == 1);
    CHECK(fault_is(&k, "copy region"));
    CHECK(words[0] == 0xAAAAAAAAu);
}

static void test_copy_dma_to_buffer(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 0u, 0u, 0, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 18 && launches == 1 && k.fault_count == 0);
    CHECK(words[5] == 0x200681CAu);
    CHECK(words[6] == 0x1030u && words[7] == 256u && words[8] == 64u && words[9] == 1u);
    CHECK(words[10] == 0u && words[11] == 0u);
    CHECK(words[12] == 0x20018105u && words[13] == 256u);
    CHECK(words[14] == 0x20028106u && words[15] == 256u && words[16] == 64u);
    CHECK(words[17] == 0x830680C0u); /* type 2, flush, multi-line, src block, dst pitch */
    CHECK(!words_have(words, count, 0x20018104u));
}

static void test_copy_dma_linear_texture(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_linear_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {0, 0, 0, 64, 64, 1};
    uint32_t words[NVN_RT_DMA_LAUNCH_WORDS];
    uint32_t launches = 0;
    uint32_t count = nvn_rt_copy_dma_words(&env, &t, NULL, 0x200000, region, 0u, 0u, 0u, 1, 0u, &launches, words,
                                           NVN_RT_DMA_LAUNCH_WORDS);
    CHECK(count == 13 && launches == 1 && k.fault_count == 0);
    CHECK(words[5] == 0x20018104u && words[6] == 256u);
    CHECK(words[7] == 0x20018105u && words[8] == 256u);
    CHECK(words[9] == 0x20028106u && words[10] == 256u && words[11] == 64u);
    CHECK(words[12] == 0x838680C0u); /* type 2, flush, multi-line, src pitch, dst pitch */
    CHECK(!words_have(words, count, 0x200681C3u) && !words_have(words, count, 0x200681CAu));
}

static void test_copy_2d_buffer(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture t = make_texture(0x25, 64, 64, 0x1000);
    uint32_t region[6] = {8, 4, 0, 16, 8, 1};
    uint32_t words[NVN_RT_COPY_2D_WORDS_MAX];
    uint32_t count = nvn_rt_copy_buffer_texture_2d_words(&env, &t, NULL, 0x300000, region, NVN_COPY_FLAG_MIRROR_X,
                                                         0u, 0u, words);
    CHECK(count == 47 && k.fault_count == 0);
    CHECK(words[0] == 0x91010002u);
    CHECK(words[1] == 0x2004608Cu);
    CHECK(words[2] == 0x5025u && words[3] == 1u && words[4] == 0u && words[5] == 1u);
    CHECK(words[6] == 0x20026090u && words[7] == 0u && words[8] == 64u);
    CHECK(words[9] == 0x20026092u && words[10] == 16u && words[11] == 8u);
    CHECK(words[12] == 0x20026094u && words[13] == 0u && words[14] == 0x300000u);
    CHECK(words[15] == 0x20066080u && words[16] == 0x5025u && words[17] == 0u && words[18] == 0x30u);
    CHECK(words[19] == 1u && words[20] == 0u && words[21] == 0u);
    CHECK(words[22] == 0x20026086u && words[23] == 64u && words[24] == 64u);
    CHECK(words[25] == 0x20026088u && words[26] == 0u && words[27] == 0x1000u);
    CHECK(words[28] == 0x800160B5u);
    CHECK(words[29] == 0x20016223u && words[30] == 0x1u); /* corner, point: flags are not read (d82ba04) */
    CHECK(words[31] == 0x2004622Cu && words[32] == 8u && words[33] == 4u && words[34] == 16u && words[35] == 8u);
    CHECK(words[36] == 0x20046230u && words[37] == 0u && words[38] == 0xFFFFFFFFu && words[39] == 0u &&
          words[40] == 1u);
    /* x starts at 16 - 0.5 (mirrored), y at 0.5: half a step in, as the library (d82b9a8-d82b9d4) */
    CHECK(words[41] == 0x20046234u && words[42] == 0x80000000u && words[43] == 15u && words[44] == 0x80000000u &&
          words[45] == 0u);
    CHECK(words[46] == 0x91000002u);
}

static void test_downsample_factors(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture dst = make_texture(0x25, 64, 64, 0x1000);
    NvnTexture two = make_texture_with(0x25, 64, 64, 0x2000, 0u, 2u);
    NvnTexture four = make_texture_with(0x25, 64, 64, 0x2000, 0u, 4u);
    NvnTexture one = make_texture_with(0x25, 64, 64, 0x2000, 0u, 0u);
    uint32_t words[NVN_RT_DOWNSAMPLE_WORDS_MAX];
    int ratio_not_two = 1;
    uint32_t count = nvn_rt_downsample_words(&env, &two, NULL, &dst, NULL, 0, &ratio_not_two, words);
    CHECK(count == 47 && ratio_not_two == 0 && k.fault_count == 0);
    CHECK(words[0] == 0x91050002u);
    CHECK(words[29] == 0x20016223u && words[30] == 0x10u); /* center, bilinear (d82ca3c-d82ca90) */
    CHECK(words[37] == 0u && words[38] == 2u && words[39] == 0u && words[40] == 1u); /* 2 x 1 */
    CHECK(words[32] == 0u && words[33] == 0u && words[34] == 64u && words[35] == 64u);
    count = nvn_rt_downsample_words(&env, &four, NULL, &dst, NULL, 0, &ratio_not_two, words);
    CHECK(count == 47 && k.fault_count == 0);
    CHECK(words[38] == 2u && words[40] == 2u); /* 2 x 2 */
    count = nvn_rt_downsample_words(&env, &one, NULL, &dst, NULL, 0, &ratio_not_two, words);
    CHECK(count == 47 && k.fault_count == 0);
    CHECK(words[38] == 1u && words[40] == 1u); /* the word is outside the table: factor 1 */
}

static void test_tiled_downsample_ratio(void) {
    Mock k = {0};
    NvnRtCopyEnv env = make_copy_env(&k, copy_format_word_stub);
    NvnTexture src = make_texture(0x25, 64, 64, 0x2000);
    NvnTexture dst = make_texture(0x25, 32, 32, 0x1000);
    uint32_t words[NVN_RT_DOWNSAMPLE_WORDS_MAX];
    int ratio_not_two = 0;
    uint32_t count = nvn_rt_downsample_words(&env, &src, NULL, &dst, NULL, 1, &ratio_not_two, words);
    CHECK(count == 47 && ratio_not_two == 0 && k.fault_count == 0);
    CHECK(words[0] == 0x91060002u);
    CHECK(words[30] == 0x10u && words[46] == 0x91000002u);
    CHECK(words[38] == 2u && words[40] == 2u);
    NvnTexture three = make_texture(0x25, 96, 96, 0x2000);
    count = nvn_rt_downsample_words(&env, &three, NULL, &dst, NULL, 1, &ratio_not_two, words);
    CHECK(count == 47 && ratio_not_two == 1 && k.fault_count == 0);
    CHECK(words[38] == 3u && words[40] == 3u);
}

int main(void) {
    test_one_target();
    test_null_slot_zero();
    test_no_target();
    test_clip_limit_zero();
    test_width_align_up();
    test_depth_only_d32f();
    test_color_plus_depth();
    test_small_target_factor();
    test_small_target_tile();
    test_tile_x_nibble();
    test_clip_includes_depth();
    test_sample_mode();
    test_depth_view_layers();
    test_depth_view_address();
    test_depth_layers();
    test_view_layer_bounds();
    test_view_target_unknown();
    test_sample_mode_last_present();
    test_depth_view_checks();
    test_color_view_no_change();
    test_color_view_layers();
    test_color_view_target_3d();
    test_color_view_checks();
    test_clear_texture_one_layer();
    test_clear_texture_two_layers();
    test_clear_texture_clip();
    test_clear_texture_nothing();
    test_clear_texture_refusals();
    test_copy_one_layer();
    test_copy_two_layers();
    test_copy_scaling();
    test_copy_format_change();
    test_copy_nothing();
    test_copy_refusals();
    test_copy_dma_block_linear();
    test_copy_dma_mirror_y();
    test_copy_dma_mirror_z();
    test_copy_dma_block_compressed();
    test_copy_dma_region_refused();
    test_copy_dma_to_buffer();
    test_copy_dma_linear_texture();
    test_copy_2d_buffer();
    test_downsample_factors();
    test_tiled_downsample_ratio();
    test_depth_linear();
    test_2d_array_target();
    test_refusals();
    test_bad_input();
    test_null_env_fields();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_rt: ok\n");
    return 0;
}
