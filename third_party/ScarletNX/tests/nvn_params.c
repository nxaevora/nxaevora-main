/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Device and queue cases use the library results of 2026-11-02. Pool cases check the accepted disassembly table
   and the host GPU address allocation rule. */
#include "nx/gpu/nvn_params.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_window_sync_cycle_params(void) {
    const uint8_t free1[] = {NVN_IMAGE_FREE};
    const uint8_t free2[] = {NVN_IMAGE_FREE, NVN_IMAGE_FREE};
    const uint8_t free4[] = {NVN_IMAGE_FREE, NVN_IMAGE_FREE, NVN_IMAGE_FREE, NVN_IMAGE_FREE};
    const uint8_t acquired4[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED};
    const uint8_t mixed[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_FREE, NVN_IMAGE_ACQUIRED, NVN_IMAGE_FREE};
    const uint8_t presented2[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_PRESENTED};
    const uint8_t presented4[] = {NVN_IMAGE_PRESENTED, NVN_IMAGE_PRESENTED, NVN_IMAGE_PRESENTED, NVN_IMAGE_PRESENTED};
    CHECK(nvn_window_pick_image(free1, 1, -1) == 0);
    CHECK(nvn_window_pick_image(free1, 1, 0) == 0);
    CHECK(nvn_window_pick_image(free2, 2, -1) == 0);
    CHECK(nvn_window_pick_image(free2, 2, 0) == 1);
    CHECK(nvn_window_pick_image(free2, 2, 1) == 0);
    CHECK(nvn_window_pick_image(free4, 4, -1) == 0);
    CHECK(nvn_window_pick_image(free4, 4, 1) == 2);
    CHECK(nvn_window_pick_image(free4, 4, 3) == 0);
    CHECK(nvn_window_pick_image(acquired4, 4, -1) == -1);
    CHECK(nvn_window_pick_image(mixed, 4, 0) == 1);
    CHECK(nvn_window_pick_image(mixed, 4, 1) == 3);
    CHECK(nvn_window_pick_image(mixed, 4, 3) == 1);
    CHECK(nvn_window_pick_image(free1, 0, -1) == -1);
    CHECK(nvn_window_pick_image(free4, 5, -1) == -1);
    CHECK(nvn_window_pick_image(presented2, 2, -1) == -1);
    CHECK(nvn_window_pick_image(presented2, 2, 0) == -1);
    CHECK(nvn_window_pick_image(presented4, 4, -1) == -1);
    CHECK(nvn_sync_wait_action(NVN_SYNC_SIGNALED) == NVN_SYNC_ACTION_SIGNALED);
    CHECK(nvn_sync_wait_action(NVN_SYNC_UNSIGNALED) == NVN_SYNC_ACTION_REFUSE);
    CHECK(nvn_sync_wait_action(NVN_SYNC_FENCED) == NVN_SYNC_ACTION_WAIT);
    CHECK(nvn_window_has_presented(presented2, 2));
}

static void test_sync_wait_limit(void) {
    const uint8_t acquired4[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED};
    const uint8_t mixed[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_PRESENTED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED};
    CHECK(!nvn_window_has_presented(acquired4, 4));
    CHECK(nvn_window_has_presented(mixed, 4));
    CHECK(!nvn_window_has_presented(mixed, 1));
    CHECK(!nvn_window_has_presented(mixed, 5));
    CHECK(!nvn_window_has_presented(NULL, 4));

    NvnWaitDeadline d = nvn_wait_deadline(0, 100);
    CHECK(d.has_deadline && d.deadline_ns == 100);
    CHECK(nvn_wait_remaining_ns(d, 99) == 1);
    CHECK(nvn_wait_remaining_ns(d, 100) == 0);
    CHECK(nvn_wait_remaining_ns(d, 101) == 0);

    d = nvn_wait_deadline(1, 100);
    CHECK(d.has_deadline && d.deadline_ns == 101);
    CHECK(nvn_wait_remaining_ns(d, 100) == 1);
    CHECK(nvn_wait_remaining_ns(d, 101) == 0);

    d = nvn_wait_deadline(NVN_SYNC_WAIT_NO_LIMIT, 100);
    CHECK(!d.has_deadline);
    CHECK(nvn_wait_remaining_ns(d, 0) == -1);
    CHECK(nvn_wait_remaining_ns(d, UINT64_MAX) == -1);

    /* a huge timeout clamps the wait to INT64_MAX; an overflow of now + timeout stays finite */
    d = nvn_wait_deadline(0x8000000000000000ull, 0);
    CHECK(d.has_deadline && d.deadline_ns == 0x8000000000000000ull);
    CHECK(nvn_wait_remaining_ns(d, 0) == INT64_MAX);
    d = nvn_wait_deadline(10, UINT64_MAX - 5);
    CHECK(d.has_deadline && d.deadline_ns == UINT64_MAX);
    CHECK(nvn_wait_remaining_ns(d, UINT64_MAX - 5) == 5);
    CHECK(nvn_wait_remaining_ns(d, UINT64_MAX) == 0);
}

static void test_window_params(void) {
    NvnWindowBuilder b;
    nvn_window_builder_defaults(&b);
    CHECK(b.device == 0 && b.native_window == 0 && b.texture_count == 0 && b.textures == 0 && b.interval == 1);
    nvn_window_builder_set_device(&b, 0x1234);
    nvn_window_builder_set_native_window(&b, 0x5678);
    nvn_window_builder_set_textures(&b, 4, 0x9abc);
    nvn_window_builder_set_interval(&b, 0);
    CHECK(b.device == 0x1234 && b.native_window == 0x5678 && b.texture_count == 4 && b.textures == 0x9abc);
    CHECK(b.interval == 0);
    CHECK(!nvn_window_initialize_check(0));
    CHECK(nvn_window_initialize_check(1));
    CHECK(nvn_window_initialize_check(4));
    CHECK(!nvn_window_initialize_check(5));
    CHECK(!nvn_window_initialize_check(UINT32_MAX));
    NvnWindowCrop crop = nvn_window_crop(8, 9, 0, 12, 640, 480);
    CHECK(crop.x == 0 && crop.y == 0 && crop.width == 640 && crop.height == 480);
    crop = nvn_window_crop(8, 9, 12, 0, 640, 480);
    CHECK(crop.x == 0 && crop.y == 0 && crop.width == 640 && crop.height == 480);
    crop = nvn_window_crop(8, 9, 12, 13, 640, 480);
    CHECK(crop.x == 8 && crop.y == 9 && crop.width == 12 && crop.height == 13);
    crop = nvn_window_crop(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 640, 480);
    CHECK(crop.x == UINT32_MAX && crop.y == UINT32_MAX && crop.width == UINT32_MAX && crop.height == UINT32_MAX);
}

static void test_device_integer(void) {
    static const struct {
        uint32_t what;
        int32_t value;
    } seen[] = {{4, 256}, {15, 32}, {19, 4}, {20, 8}, {40, 1}, {76, 1}, {82, 65536}, {85, 262144}, {90, 0}, {92, 16384}};
    for (size_t i = 0; i < sizeof seen / sizeof seen[0]; i++) {
        int32_t v = -1;
        CHECK(nvn_device_integer(seen[i].what, &v) && v == seen[i].value);
    }
    int32_t v = 7;
    CHECK(!nvn_device_integer(0, &v) && v == 7); /* no run asked for it: no value, the output is untouched */
    CHECK(nvn_device_integer(NVN_DEVICE_INTEGER_LIMIT + 1, &v) && v == 0); /* the library's switch default */
    CHECK(!nvn_device_integer(NVN_DEVICE_INTEGER_LIMIT, &v));
}

static void test_timestamp(void) {
    CHECK(nvn_timestamp_ns(0) == 0);
    CHECK(nvn_timestamp_ns(384) == 625);
    CHECK(nvn_timestamp_ns(383) == 623); /* floor(383 * 625 / 384) */
    /* the library's split into whole and rest equals the exact floor of ticks * 625 / 384, also near 2^64 */
    uint64_t samples[] = {1, 2, 1000, 0xF53674EEull, 0x1201E96D2ull, 1ull << 40, 1ull << 62, ~0ull / 625};
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++)
        CHECK(nvn_timestamp_ns(samples[i]) == (uint64_t)((unsigned __int128)samples[i] * 625 / 384));
}

/* the builder that the game made in the run: flags 0, command 0x20000, compute 0x40000, control 0x20000 */
static void test_queue_memory(void) {
    NvnQueueBuilder b;
    memset(&b, 0, sizeof b);
    nvn_queue_builder_defaults(&b);
    b.has_command = b.has_compute = b.has_control = 1;
    b.command = 0x20000;
    b.compute = 0x40000;
    b.control = 0x20000;
    CHECK(nvn_queue_memory_size(&b, &nvn_queue_device_words) == 0x8A000); /* the library's answer in the run */

    /* The cases below have no value from the library. They check that the function follows its port. */
    nvn_queue_builder_defaults(&b); /* sizes not set: the library's defaults 0x10000, 0x40000 and 0x4000 */
    CHECK(nvn_queue_memory_size(&b, &nvn_queue_device_words) == 0x5E000);
}

static void test_queue_defaults(void) {
    NvnQueueBuilder b;
    memset(&b, 0xAB, sizeof b);
    b.device = 0x1234;
    nvn_queue_builder_defaults(&b);
    CHECK(b.device == 0x1234); /* SetDefaults leaves the device */
    CHECK(b.priority == NVN_QUEUE_PRIORITY_DEFAULT);
    CHECK(!b.has_command && !b.has_compute && !b.has_control && !b.has_flush_threshold);
    CHECK(b.flags == 0 && b.memory == 0 && b.memory_size == 0);
}

static void test_memory_pool_defaults(void) {
    NvnMemoryPoolBuilder b;
    memset(&b, 0xab, sizeof b);
    b.device = 0x1234;
    nvn_memory_pool_builder_defaults(&b);
    CHECK(b.device == 0x1234 && b.flags == 0x21);
    CHECK(!b.storage && !b.size && !b.handle);
    CHECK(!b.optional[0] && !b.optional[1] && !b.optional[2]);
}

static void test_memory_pool_params(void) {
    NvnMemoryPoolBuilder b = {.storage = 0x1000, .size = 0x1000};
    uint32_t flags;
    /* Each low-bit combination, with upper bits preserved. */
    static const uint32_t cpu[] = {2, 1, 2, 2, 4, 4, 2, 2};
    static const uint32_t gpu[] = {32, 8, 16, 16, 32, 32, 32, 32};
    for (uint32_t f = 0; f < 64; f++) {
        b.flags = f | 0x80000040u;
        CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_OK);
        CHECK(flags == (0x80000040u | cpu[f & 7] | gpu[f >> 3]));
    }
    b.flags = NVN_POOL_DEFAULT_FLAGS;
    CHECK(nvn_memory_pool_params(&b, NVN_POOL_PAGE_SIZE, &flags) == NVN_POOL_OK && flags == 0x21);
    b.flags = NVN_POOL_PHYSICAL | 0x3f;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_INVALID_PAGE_SIZE);
    CHECK(flags == (NVN_POOL_PHYSICAL | 2 | 8));
    b.size = 0x10000;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_OK);
    b.flags = NVN_POOL_VIRTUAL | 0x3f;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_UNSUPPORTED_VIRTUAL);
    CHECK(flags == (NVN_POOL_VIRTUAL | 1 | 32));
    b.flags = NVN_POOL_IMPORT;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_UNSUPPORTED_IMPORT);
    b.flags = 0;
    b.storage = 0;
    b.handle = 1;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.storage = 0x1001;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.storage = 0x1000;
    b.size = 0;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.size = 0x1001;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.size = 0x1000;
    CHECK(nvn_memory_pool_params(&b, 0, &flags) == NVN_POOL_INVALID_PAGE_SIZE);
    CHECK(nvn_memory_pool_map(NVN_POOL_CPU_NO_ACCESS, 0x123000) == 0);
    CHECK(nvn_memory_pool_map(NVN_POOL_CPU_CACHED, 0x123000) == 0x123000);
    CHECK(nvn_memory_pool_flags(0xffffffffu) == 0x01ffffffu);
}

static void test_pool_gpu_allocate(void) {
    uint64_t address = 7, next = 9;
    CHECK(nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1001, 0x1000, &address, &next));
    CHECK(address == NVN_POOL_GPU_BASE && next == address + 0x2000);
    uint64_t first_end = next;
    CHECK(nvn_pool_gpu_allocate(next, 0x10000, 0x10000, &address, &next));
    CHECK(address >= first_end && address % 0x10000 == 0 && next == address + 0x10000);
    CHECK(nvn_pool_gpu_allocate(NVN_POOL_GPU_END - 0x1000, 0x1000, 0x1000, &address, &next));
    CHECK(next == NVN_POOL_GPU_END);
    address = 7;
    next = 9;
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_END, 0x1000, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_END - 1, 1, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, UINT64_MAX, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1000, 0, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1000, 0x800, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE - 1, 0x1000, 0x1000, &address, &next));
    CHECK(address == 7 && next == 9);
}


static void test_sampler(void) {
    NvnSampler s = {.device = 0x1234};
    nvn_sampler_defaults(&s);
    CHECK(s.device == 0x1234 && s.min_filter == 1 && s.mag_filter == 1);
    CHECK(s.wrap[0] == 1 && s.wrap[1] == 1 && s.wrap[2] == 1);
    CHECK(s.min_lod == 0 && s.max_lod == 1000 && s.lod_bias == 0);
    CHECK(s.compare_enable == 0 && s.compare == 2 && s.anisotropy == 1);
    CHECK(!s.border[0] && !s.border[1] && !s.border[2] && !s.border[3] && !s.reduction && !s.lod_snap);
    NvnSamplerDeviceWords dev = nvn_sampler_device_words;
    uint32_t words[8];
    const uint32_t defaults[8] = {0x26000, 0x362, 0xf00000, 0, 0, 0, 0, 0};
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(!memcmp(words, defaults, sizeof words));
    s.wrap[0] = 0; s.wrap[1] = 2; s.wrap[2] = 7;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x260bc); /* 4 | (7<<3) | (2<<6) */
    {
        uint32_t before[8];
        memcpy(before, words, sizeof before);
        s.wrap[1] = 8;
        CHECK(!nvn_sampler_descriptor(&s, &dev, words) && !memcmp(words, before, sizeof words));
        s.wrap[1] = 2;
    }
    nvn_sampler_defaults(&s);
    s.compare_enable = 9; s.compare = 2;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x26600); /* enable 0x200 and compare code 1 at bit 10 */
    nvn_sampler_defaults(&s);
    s.anisotropy = 16;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x726000);
    s.anisotropy = 2.5f;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x126000); /* round to even: 2, anisotropy code 1 */
    nvn_sampler_defaults(&s);
    s.border[0] = s.border[3] = 0x3f800000;
    s.border[2] = 0x3f000000;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[2] == 0xfff00000 && words[3] == 0x0bc00000); /* sRGB(0.5) rounds to 188 */
    CHECK(!memcmp(words + 4, s.border, sizeof s.border));
    s.border[0] = 0x3a800000; /* linear sRGB branch: (1/1024)*12.92*255 rounds to 3 */
    s.border[1] = 0xbf800000;
    s.border[2] = 0x40000000;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[2] == 0x03f00000 && words[3] == 0x0ff00000);
    nvn_sampler_defaults(&s);
    s.lod_snap = 0.25f;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x21fc0362); /* index 8, bias -64/256 */
    s.lod_snap = 0; s.lod_bias = -100; s.min_lod = 20; s.max_lod = -1;
    dev = (NvnSamplerDeviceWords){16, 0x100};
    s.reduction = 2;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x1100b62 && words[2] == 0xf00f00);
    nvn_sampler_defaults(&s);
    s.lod_bias = 0.5f; s.reduction = 1;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x180762); /* device adds 1, bias 384/256 */
}
static void test_snap_formula(void) {
    CHECK(nvn_sampler_snap_bias(0) == 0 && !signbit(nvn_sampler_snap_bias(0)));
    CHECK(nvn_sampler_snap_bias(1) == -1.0f / 18.0f);
    CHECK(nvn_sampler_snap_bias(7) == -7.0f / 30.0f);
    CHECK(nvn_sampler_snap_bias(8) == -0.25f);
    CHECK(nvn_sampler_snap_bias(16) == -0.375f);
    CHECK(nvn_sampler_snap_bias(24) == -0.4375f);
    CHECK(nvn_sampler_snap_bias(31) == -7.0f / 15.0f);
    for (unsigned i = 1; i < 32; i++) CHECK(nvn_sampler_snap_bias(i) < nvn_sampler_snap_bias(i - 1));
}
static void test_texture_layout(void) {
    NvnTextureBuilder b = {.device = 0x1234};
    nvn_texture_builder_defaults(&b);
    CHECK(b.device == 0x1234 && b.target == 1 && b.levels == 1);
    CHECK(!b.width && !b.height && !b.depth && !b.flags && !b.pool && !b.offset && !b.packaged);
    CHECK(!b.format && !b.samples && !b.stride && !b.tile && !b.gl_name && !b.depth_stencil_mode);
    for (unsigned i = 0; i < 4; i++) CHECK(b.swizzle[i] == i + 2);
    NvnTextureLayout l;
    b = (NvnTextureBuilder){.flags = 9, .target = 1, .width = 1920, .height = 1080, .depth = 1,
                            .levels = 1, .format = 0x25};
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x10000 && l.size == 0x870000 && l.layer_stride == 0x870000);
    CHECK(l.bytes_per_block == 4 && l.log2_bytes == 2 && l.sample_x == 1 && l.sample_y == 1);
    CHECK(l.block_width == 1 && l.block_height == 1 && l.levels == 1 && l.dim == 2 && l.tile_y == 4 && !l.kind);
    b.flags = 8; b.width = 960; b.height = 540; b.format = 0x3f;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x10000 && l.size == 0x260000 && l.layer_stride == 0x258000);
    b.flags = 0x10; b.format = 1; b.height = 1; b.stride = 0x20;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x20 && l.size == 0x20 && l.linear_stride == 0x20 && l.levels == 1 && l.dim == 2);
    b.target = 10; b.flags = 0; b.width = 7; b.format = 0x22;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 4 && l.size == 84 && l.layer_stride == 84 && l.dim == 1 && l.levels == 1);
    b = (NvnTextureBuilder){.target = 1, .width = 16, .height = 16, .depth = 1, .levels = 3, .format = 0x25};
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 512 && l.size == 2048 && l.layer_stride == 2048); /* mips: 1024+512+512 */
    b.target = 8; b.levels = 1;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.depth == 6 && l.layer_stride == 1024 && l.size == 6144);
    b.target = 3; b.height = 4;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.height == 1 && l.depth == 4 && l.dim == 1 && l.layer_stride == 512 && l.size == 2048);
    b.target = 1; b.depth = 1; b.width = 128; b.height = 128; b.flags = 0x20;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 3 && l.tile_y == 4 && l.tile_z == 0 && l.size == 65536);
    b.flags = 2; b.width = 16; b.height = 16;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_y == 1 && l.size == 1024);
    b.flags = 0x200; b.height = 17;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_y == 0 && l.size == 1536); /* automatic tile costs 2048, minimal tile costs 1536 */
    b.flags = 0; b.width = 8; b.height = 8; b.depth = 4; b.target = 2; b.levels = 2;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.dim == 3 && l.tile_z == 2 && l.size == 3072);
    b.target = 1; b.depth = 1; b.levels = 1; b.width = b.height = 16; b.samples = 4;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.sample_x == 2 && l.sample_y == 2 && l.samples == 4 && l.size == 4096);
    b.samples = 0; b.width = b.height = 4; b.format = 0x41;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.bytes_per_block == 8 && l.block_width == 4 && l.block_height == 4 && l.size == 512);
    b.flags = 0; b.format = 0x25; b.width = 16; b.height = 64; b.samples = 0; b.tile = 0x21;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 0 && l.tile_y == 2 && l.tile_z == 0 && l.size == 4096);
    b.tile = 0; b.flags = 0x20; b.width = 16; b.height = 16;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 0 && l.tile_y == 1 && l.size == 1024); /* first mip does not fit a sparse tile */
    b.width = b.height = 4; b.flags = 0x40;
    CHECK(nvn_texture_storage_alignment(&b, &nvn_texture_device_words) == 0x80);
    b.flags = 0; b.format = 0x35;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.depth_stencil == 3 && l.alignment == 0x10000 && l.size == 0x10000);
    b.gl_name = 1;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l) && l.size == 0 && l.alignment == 0x10000);
    b.gl_name = 0; b.format = 0x25; b.flags = 8;
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    dev.w19f8 = NVN_TEXTURE_COMPRESSION_DISABLED;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.alignment == 512 && l.size == 512);
    b.levels = 0;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.size == 0);
    b.target = 5;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.levels == 1 && l.size == 512);
    b.target = 10; dev.w1b18 = 7;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 0x10000);
    dev.w1b18 = 0xfe;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 4);
    b.format = 0xa1;
    CHECK(!nvn_texture_layout(&b, &dev, &l) && !l.size);
    b.flags = 0x10;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 0x20);
}

static void test_texture_level(void) {
    NvnTexture t = {0};
    NvnTextureLevel lv;
    nvn_texture_builder_defaults(&t.builder);
    t.builder = (NvnTextureBuilder){.target = 1, .width = 16, .height = 16, .depth = 1,
                                    .levels = 3, .format = 0x25};
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    CHECK(nvn_texture_level(&t, 0, &lv) && lv.offset == 0 && lv.size == 1024);
    CHECK(lv.gobs_x == 1 && lv.rows == 16 && lv.block_height_log2 == 1);
    CHECK(nvn_texture_level(&t, 1, &lv) && lv.offset == 1024 && lv.size == 512);
    CHECK(nvn_texture_level(&t, 2, &lv) && lv.offset == 1536 && lv.size == 512);
    CHECK(lv.offset + lv.size == t.layout.layer_stride);
    CHECK(!nvn_texture_level(&t, 3, &lv)); /* no fourth level */
    t.builder = (NvnTextureBuilder){.target = 1, .width = 16, .height = 16, .depth = 1,
                                    .levels = 2, .format = 0x41};
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    CHECK(nvn_texture_level(&t, 0, &lv) && lv.offset == 0 && lv.size == 512);
    CHECK(nvn_texture_level(&t, 1, &lv) && lv.offset == 512 && lv.size == 512);
    CHECK(lv.offset + lv.size == t.layout.layer_stride);
    lv = (NvnTextureLevel){.offset = 7, .size = 7};
    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    t.builder.stride = 0x40;
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    CHECK(!nvn_texture_level(&t, 0, &lv) && lv.offset == 7 && lv.size == 7); /* linear */
    t.builder.flags = 0;
    t.builder.target = NVN_TEXTURE_TARGET_BUFFER;
    t.builder.width = 7;
    t.builder.format = 0x22;
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    CHECK(!nvn_texture_level(&t, 0, &lv)); /* BUFFER target */
    t.builder = (NvnTextureBuilder){.target = NVN_TEXTURE_TARGET_3D, .width = 8, .height = 8, .depth = 4,
                                    .levels = 1, .format = 0x25};
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    /* A 3D level: 8 by 8 by 4 texels of 4 bytes, blocks 2 slices deep (ez = 2), 2048 bytes. */
    CHECK(nvn_texture_level(&t, 0, &lv) && lv.offset == 0 && lv.size == 2048 && lv.depth == 4 &&
          lv.depth_log2 == 2 && lv.gobs_x == 1 && lv.rows == 8 && lv.block_height_log2 == 0);
    t.builder = (NvnTextureBuilder){.target = NVN_TEXTURE_TARGET_2D_MS, .width = 16, .height = 16, .depth = 1,
                                    .levels = 1, .format = 0x25, .samples = 4};
    CHECK(nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout));
    CHECK(!nvn_texture_level(&t, 0, &lv)); /* multisample */
}

/* The block linear byte offset of (x, y), from Eden's SWIZZLE_X_BITS and SWIZZLE_Y_BITS: the low bits of x and y
   go into the set bits of the mask, in order, and the block column and block row parts follow. */
static uint32_t deposit_bits(uint32_t value, uint32_t bits) {
    uint32_t out = 0, source = 1;
    while (bits) {
        uint32_t target = bits & (0u - bits);
        if (value & source) out |= target;
        source <<= 1;
        bits &= bits - 1;
    }
    return out;
}
static uint64_t reference_level_offset(uint32_t gobs_x, uint32_t bh, uint32_t x, uint32_t y) {
    uint32_t x_bits = 0x12fu; /* 0b100101111 */
    uint32_t y_bits = 0xd0u;  /* 0b011010000 */
    return ((uint64_t)(x >> 6) << (9 + bh)) + deposit_bits(x, x_bits) +
           ((uint64_t)((y >> 3) >> bh) * gobs_x << (9 + bh)) +
           (((y >> 3) & ((1u << bh) - 1)) << 9) + deposit_bits(y & 7, y_bits);
}
static void test_texture_write_rows(void) {
    /* 128 bytes by 32 rows, two blocks of two GOBs high each: 4096 bytes */
    NvnTextureLevel lv = {.offset = 0, .size = 4096, .gobs_x = 2, .rows = 32, .depth = 1,
                          .block_height_log2 = 1};
    uint8_t level[4096], expected[4096], source[2048];
    const uint32_t x_start = 20, x_end = 76, row_start = 5, row_end = 27;
    const uint32_t row_bytes = x_end - x_start;
    for (size_t i = 0; i < sizeof source; i++) source[i] = (uint8_t)(i * 37u + 11u);
    memset(level, 0xa5, sizeof level);
    memset(expected, 0xa5, sizeof expected);
    for (uint32_t r = row_start; r < row_end; r++)
        for (uint32_t x = x_start; x < x_end; x++)
            expected[reference_level_offset(lv.gobs_x, lv.block_height_log2, x, r)] =
                source[(r - row_start) * row_bytes + (x - x_start)];
    CHECK(nvn_texture_write_rows(level, &lv, x_start, x_end, row_start, row_end, source, row_bytes, 0));
    CHECK(memcmp(level, expected, sizeof level) == 0);
    /* refusals write nothing */
    memset(level, 0xa5, sizeof level);
    CHECK(!nvn_texture_write_rows(level, &lv, 0, 129, 0, 1, source, 129, 0));
    CHECK(!nvn_texture_write_rows(level, &lv, 0, 1, 0, 33, source, 1, 0));
    for (size_t i = 0; i < sizeof level; i++) CHECK(level[i] == 0xa5);
    NvnTextureLevel small = lv;
    small.size = 100;
    CHECK(!nvn_texture_write_rows(level, &small, 0, 64, 0, 1, source, 64, 0));
    for (size_t i = 0; i < sizeof level; i++) CHECK(level[i] == 0xa5);
}

/* The block linear offset of byte (x, y) of slice z, from the same GOB geometry as reference_level_offset:
   a block is 2^bh GOBs high and 2^bd slices deep, blocks go gobs_x to a row, the rows of a slab come next and then
   the slabs (gpu.c's bl_offset). */
static uint64_t reference_level_offset_3d(uint32_t gobs_x, uint32_t rows, uint32_t bh, uint32_t bd,
                                          uint32_t x, uint32_t y, uint32_t z) {
    uint32_t x_bits = 0x12fu, y_bits = 0xd0u;
    uint64_t block_rows = ((uint64_t)rows + (8u << bh) - 1) >> (3 + bh);
    uint64_t gy = y >> 3;
    return ((uint64_t)(x >> 6) << (9 + bh + bd)) + deposit_bits(x, x_bits) +
           (((z >> bd) * block_rows + (gy >> bh)) * gobs_x << (9 + bh + bd)) +
           ((((z & ((1u << bd) - 1)) << bh) + (gy & ((1u << bh) - 1))) << 9) + deposit_bits(y & 7, y_bits);
}
static void test_texture_write_rows_3d(void) {
    /* 64 bytes by 16 block rows, two slices in blocks 2 GOBs high and 2 slices deep: 2048 bytes */
    NvnTextureLevel lv = {.offset = 0, .size = 2048, .gobs_x = 1, .rows = 16, .depth = 2,
                          .block_height_log2 = 0, .depth_log2 = 1};
    uint8_t level[2048], expected[2048], source[2 * 64 * 16];
    for (size_t i = 0; i < sizeof source; i++) source[i] = (uint8_t)(i * 53u + 7u);
    for (uint32_t z = 0; z < 2; z++)
        for (uint32_t y = 0; y < 16; y++)
            for (uint32_t x = 0; x < 64; x++)
                expected[reference_level_offset_3d(lv.gobs_x, lv.rows, lv.block_height_log2, lv.depth_log2,
                                                   x, y, z)] = source[z * 64 * 16 + y * 64 + x];
    memset(level, 0xa5, sizeof level);
    CHECK(nvn_texture_write_rows(level, &lv, 0, 64, 0, 16, source, 64, 0));
    CHECK(nvn_texture_write_rows(level, &lv, 0, 64, 0, 16, source + 64 * 16, 64, 1));
    CHECK(memcmp(level, expected, sizeof level) == 0);
    /* hand computed: z = 1's first slice half at 512, z = 0's second block row at 1024, the last byte at 2047 */
    CHECK(reference_level_offset_3d(1, 16, 0, 1, 0, 0, 1) == 512);
    CHECK(reference_level_offset_3d(1, 16, 0, 1, 0, 8, 0) == 1024);
    CHECK(reference_level_offset_3d(1, 16, 0, 1, 63, 15, 1) == 2047);
    /* ez = 0: each slice is a slab of two block rows, so slice z = 1 starts at 1024 */
    NvnTextureLevel flat = lv;
    flat.depth_log2 = 0;
    CHECK(reference_level_offset_3d(1, 16, 0, 0, 0, 0, 1) == 1024);
    CHECK(reference_level_offset_3d(1, 16, 0, 0, 0, 8, 0) == 512);
    for (uint32_t z = 0; z < 2; z++)
        for (uint32_t y = 0; y < 16; y++)
            for (uint32_t x = 0; x < 64; x++)
                expected[reference_level_offset_3d(flat.gobs_x, flat.rows, flat.block_height_log2,
                                                   flat.depth_log2, x, y, z)] = source[z * 64 * 16 + y * 64 + x];
    memset(level, 0xa5, sizeof level);
    CHECK(nvn_texture_write_rows(level, &flat, 0, 64, 0, 16, source, 64, 0));
    CHECK(nvn_texture_write_rows(level, &flat, 0, 64, 0, 16, source + 64 * 16, 64, 1));
    CHECK(memcmp(level, expected, sizeof level) == 0);
    /* bh = 1 and bd = 1 together: 128 bytes by 32 rows, blocks 2 GOBs high and 2 slices deep, 3 slices (two slabs) */
    NvnTextureLevel deep = {.offset = 0, .size = 16384, .gobs_x = 2, .rows = 32, .depth = 3,
                            .block_height_log2 = 1, .depth_log2 = 1};
    static uint8_t deep_level[16384], deep_expected[16384], deep_source[3 * 128 * 32];
    for (size_t i = 0; i < sizeof deep_source; i++) deep_source[i] = (uint8_t)(i * 29u + 3u);
    memset(deep_level, 0xa5, sizeof deep_level);
    memset(deep_expected, 0xa5, sizeof deep_expected);
    for (uint32_t z = 0; z < 3; z++) {
        for (uint32_t y = 0; y < 32; y++)
            for (uint32_t x = 0; x < 128; x++)
                deep_expected[reference_level_offset_3d(deep.gobs_x, deep.rows, deep.block_height_log2,
                                                        deep.depth_log2, x, y, z)] =
                    deep_source[z * 128 * 32 + y * 128 + x];
        CHECK(nvn_texture_write_rows(deep_level, &deep, 0, 128, 0, 32, deep_source + z * 128 * 32, 128, z));
    }
    CHECK(memcmp(deep_level, deep_expected, sizeof deep_level) == 0);
    /* hand computed: slice 1 of block (0, 0) starts 2 GOBs in, slice 2 starts the second slab (2 block rows of 2 blocks) */
    CHECK(reference_level_offset_3d(2, 32, 1, 1, 0, 0, 1) == 1024);
    CHECK(reference_level_offset_3d(2, 32, 1, 1, 0, 8, 0) == 512);
    CHECK(reference_level_offset_3d(2, 32, 1, 1, 0, 0, 2) == 8192);
    /* a slice outside the level depth is refused and writes nothing */
    memset(level, 0xa5, sizeof level);
    CHECK(!nvn_texture_write_rows(level, &lv, 0, 64, 0, 16, source, 64, 2));
    for (size_t i = 0; i < sizeof level; i++) CHECK(level[i] == 0xa5);
}

/* Expected words below are hand-computed from the accepted encoder table. The synthetic seed
   has component codes (1,2,3,4), bit31, and low format bits 0x25. No game format data is used. */
static void check_texture_descriptor_mode(const NvnTexture* t, const NvnTextureView* v,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* r, NvnTextureDescriptorMode mode,
    const uint32_t expected[8]) {
    uint32_t words[8];
    CHECK(nvn_texture_descriptor(t, v, dev, r, mode, words) == NVN_TEXTURE_DESCRIPTOR_OK);
    for (unsigned i = 0; i < 8; i++) {
        if (words[i] != expected[i])
            printf("descriptor word %u: actual=%08x expected=%08x\n", i, words[i], expected[i]);
        CHECK(words[i] == expected[i]);
    }
}
static void check_texture_descriptor(const NvnTexture* t, const NvnTextureView* v,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* r, const uint32_t expected[8]) {
    check_texture_descriptor_mode(t, v, dev, r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, expected);
}
static void test_texture_descriptor(void) {
    NvnTexture t = {0};
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = 0x25;
    t.layout = (NvnTextureLayout){.width = 16, .height = 8, .depth = 1, .levels = 1,
        .tile_x = 3, .tile_y = 1, .tile_z = 2, .layer_stride = 0x1000};
    t.address = 0x0412345600ull;
    NvnTextureFormatRecord r = {.seed = 0xc6880025};
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    /* q3: enable 0x10000, quality 0x60000, tile 0xc88. Row 2 gives q6=0x3000000.
       q4: fixed 0xe0000000, sector 1<<27, target 1<<23, width-1=15. */
    const uint32_t rgba[8] = {0xc6880025, 0x12345600, 0x00600004, 0x00070c88,
                              0xe880000f, 0x80000007, 0x03000000, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, rgba);
    CHECK(dev.w08 == 0x4000 && dev.w1a48 == 2 && dev.w1a4c == 1 && dev.w1a50 == 0);
    CHECK(dev.w1a54 == 0xffffffff && dev.w1a88 == 2 && dev.w1b1c == 1);
    CHECK(nvn_sampler_device_words.w1a80 == 0 && nvn_sampler_device_words.w1a84 == 0x111);
    /* Linear: stride>>5=0x55e6f, low16=0x5e6f; clear bits10..12, insert tile_x=3.
       Pitch kind=2 and target code=7. No sector or mip fields. */
    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    t.layout.linear_stride = 0xabcde0;
    const uint32_t linear[8] = {0xc6880025, 0x12345600, 0x00400004, 0x00004e6f,
                                0xe380000f, 0x80000007, 0x03000000, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, linear);
    /* Buffer: width-1=0x123455 splits across q3 and q4. Only target code 6 remains. */
    t.builder.flags = 0; t.builder.target = 10; t.layout.width = 0x123456;
    const uint32_t buffer[8] = {0xc6880025, 0x12345600, 4, 0x12, 0x03003455, 0, 0, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, buffer);
    /* Cube uses target code 3 and depth 1, even with six faces in the layout. */
    t.builder.target = 8; t.layout.width = 16; t.layout.depth = 6;
    uint32_t cube[8]; memcpy(cube, rgba, sizeof cube); cube[4] = 0xe980000f;
    check_texture_descriptor(&t, NULL, &dev, &r, cube);
    /* Array: layer 0x109 adds 0x109000 to A, puts 1 in q4[16:18],
       1 in q2[16:20] and 1 in q2[29:31]. Two layers give q5[16:29]=1. */
    t.builder.target = 4; t.layout.depth = 300;
    NvnTextureView v = {.flags = NVN_VIEW_LAYERS, .base_layer = 0x109, .layer_count = 2};
    const uint32_t array[8] = {0xc6880025, 0x1244e600, 0x20610004, 0x00070c88,
                               0xea81000f, 0x80010007, 0x03000000, 0};
    check_texture_descriptor(&t, &v, &dev, &r, array);
    /* Count 0 uses Z-base=35. Clearing the flag stops the address change,
       but registration still copies raw base-layer bits. */
    v.layer_count = 0;
    uint32_t remaining[8]; memcpy(remaining, array, sizeof remaining); remaining[5] = 0x80220007;
    check_texture_descriptor(&t, &v, &dev, &r, remaining);
    v.flags = 0; memcpy(remaining, array, sizeof remaining);
    remaining[1] = 0x12345600; remaining[5] = 0x812b0007;
    check_texture_descriptor(&t, &v, &dev, &r, remaining);
    /* View selects 2D and another synthetic seed. Swizzle (A,ONE,ZERO,R)
       selects codes (4,6,0,1). sRGB adds bit22 in q4. */
    t.builder.target = 10; t.layout.depth = 1;
    v = (NvnTextureView){.flags = NVN_VIEW_FORMAT | NVN_VIEW_SWIZZLE | NVN_VIEW_TARGET,
        .format = 0x26, .target = 1, .swizzle = {5, 1, 0, 2}};
    r = (NvnTextureFormatRecord){.seed = 0xc6880055, .one_code = 1, .srgb = 1};
    uint32_t override[8]; memcpy(override, rgba, sizeof override);
    override[0] = 0x91a00055; override[4] = 0xe8c0000f;
    check_texture_descriptor(&t, &v, &dev, &r, override);
    r.one_code = 0; override[0] = 0x91e00055;
    check_texture_descriptor(&t, &v, &dev, &r, override);
    /* Depth/stencil mode 1 uses B=0x24900000 for f=0x35 and changes low6 to 14.
       Mode 9 uses B=0x76d80000. Identity swizzle preserves each changed seed. */
    t.builder.target = 1; t.builder.format = 0x35;
    r = (NvnTextureFormatRecord){.seed = 0xc6880025, .depth_size = 24, .stencil_size = 8};
    v = (NvnTextureView){.flags = NVN_VIEW_DEPTH_STENCIL_MODE, .depth_stencil_mode = 1};
    uint32_t ds[8]; memcpy(ds, rgba, sizeof ds); ds[0] = 0xa490000e;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    v.depth_stencil_mode = 9; ds[0] = 0xf6d80025;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    /* Ordinary depth format: B=0x36d80000 or 0x74900000. A 32/8 record uses low6=5. */
    t.builder.format = 0x36; r.depth_size = 32; v.depth_stencil_mode = 1; ds[0] = 0xb6d80005;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    t.builder.depth_stencil_mode = 2; ds[0] = 0xf4900025;
    check_texture_descriptor(&t, NULL, &dev, &r, ds);
    r.depth_size = 0; ds[0] = 0xa4900025;
    check_texture_descriptor(&t, NULL, &dev, &r, ds);
    /* Multisample target 6 has code 5. Four samples give sample code 2 at q7 bit8. */
    t.builder.format = 0x25; t.builder.target = 6; t.layout.samples = 4;
    r = (NvnTextureFormatRecord){.seed = 0xc6880025};
    uint32_t ms[8]; memcpy(ms, rgba, sizeof ms); ms[4] = 0xea80000f; ms[7] = 0x200;
    check_texture_descriptor(&t, NULL, &dev, &r, ms);
    /* M=6-1=5; hi=min(4-1,5)=3. View base2,count1 gives lo=hi=2. */
    t.builder.target = 1; t.builder.levels = 4; t.layout.levels = 6;
    uint32_t mip[8]; memcpy(mip, rgba, sizeof mip); mip[3] = 0x50070c88; mip[7] = 0x30;
    check_texture_descriptor(&t, NULL, &dev, &r, mip);
    v = (NvnTextureView){.flags = NVN_VIEW_LEVELS, .base_level = 2, .level_count = 1};
    mip[7] = 0x22; check_texture_descriptor(&t, &v, &dev, &r, mip);
    v.base_level = 9; v.level_count = 0; mip[7] = 0x35;
    check_texture_descriptor(&t, &v, &dev, &r, mip);
    /* Clamp row index to 5: q3 adds 31<<21; q6=(3<<25)|(3<<23)|(1<<30). */
    dev.w1a88 = UINT32_MAX; t.builder.levels = t.layout.levels = 1;
    uint32_t clamp[8]; memcpy(clamp, rgba, sizeof clamp);
    clamp[3] = 0x03e70c88; clamp[6] = 0x47800000;
    check_texture_descriptor(&t, NULL, &dev, &r, clamp);
    uint32_t out[8], unchanged[8];
    memset(out, 0xa5, sizeof out); memcpy(unchanged, out, sizeof out);
    t.builder.format = 0xa1;
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT);
    CHECK(!memcmp(out, unchanged, sizeof out));
    t.builder.format = 0xa0; t.builder.target = 11;
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_TARGET);
    CHECK(!memcmp(out, unchanged, sizeof out));
    v = (NvnTextureView){.flags = NVN_VIEW_FORMAT | NVN_VIEW_TARGET, .format = 0xa1, .target = 1};
    CHECK(nvn_texture_descriptor(&t, &v, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT);
    v.format = 0; v.target = 11;
    CHECK(nvn_texture_descriptor(&t, &v, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_TARGET);
    CHECK(!memcmp(out, unchanged, sizeof out));
    t.builder.target = 1;
    v = (NvnTextureView){.flags = NVN_VIEW_SWIZZLE, .swizzle = {2, 3, 4, NVN_TEXTURE_SWIZZLE_MAX + 1}};
    CHECK(nvn_texture_descriptor(&t, &v, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE);
    t.builder.swizzle[0] = 6;
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, out) == NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE);
    CHECK(!memcmp(out, unchanged, sizeof out));
}
/* Image mode keeps the cube depth, uses target code 5 and sets q2 bit 24.
   The texture-mode words of each case are derived in test_texture_descriptor. */
static void test_texture_descriptor_image(void) {
    NvnTexture t = {0};
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = 0x25;
    t.layout = (NvnTextureLayout){.width = 16, .height = 8, .depth = 6, .levels = 1,
        .tile_x = 3, .tile_y = 1, .tile_z = 2, .layer_stride = 0x1000};
    t.address = 0x0412345600ull;
    NvnTextureFormatRecord r = {.seed = 0xc6880025};
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    /* Cube texture words are rgba with q4=0xe980000f. Image: code 3->5 adds 0x01000000
       to q4, depth stays 6 so q5=0x80050007, and q2 gains bit 24. */
    t.builder.target = NVN_TEXTURE_TARGET_CUBE;
    const uint32_t cube_image[8] = {0xc6880025, 0x12345600, 0x01600004, 0x00070c88,
                                    0xea80000f, 0x80050007, 0x03000000, 0};
    check_texture_descriptor_mode(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE, cube_image);
    /* Cube array: texture divides depth 12 by 6 and uses code 8.
       Image keeps depth 12 and code 5, so q5=0x800b0007 and q4=0xea80000f. */
    t.builder.target = NVN_TEXTURE_TARGET_CUBE_ARRAY;
    t.layout.depth = 12;
    const uint32_t cube_array_image[8] = {0xc6880025, 0x12345600, 0x01600004, 0x00070c88,
                                          0xea80000f, 0x800b0007, 0x03000000, 0};
    check_texture_descriptor_mode(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE, cube_array_image);
    /* A non-cube 2D texture differs only in the q2 mode bit. */
    t.builder.target = NVN_TEXTURE_TARGET_2D;
    t.layout.depth = 1;
    uint32_t texture[8], image[8];
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE, texture) ==
          NVN_TEXTURE_DESCRIPTOR_OK);
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE, image) ==
          NVN_TEXTURE_DESCRIPTOR_OK);
    CHECK((texture[2] ^ image[2]) == 0x01000000);
    for (unsigned i = 0; i < 8; i++)
        if (i != 2) CHECK(texture[i] == image[i]);
}
static void test_texture_descriptor_fields(void) {
    NvnTexture t = {.address = 0x0412345600ull};
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = 0x25;
    t.layout = (NvnTextureLayout){.width = 1, .height = 1, .depth = 1, .levels = 1, .layer_stride = 0x1000};
    NvnTextureFormatRecord r = {.seed = 0xc6880025};
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    uint32_t expected[8] = {0xc6880025, 0x12345600, 0x00600004, 0x70000, 0xe8800000, 0x80000000, 0x3000000, 0};
    /* Sector codes for device values 0,1,2,3,4 are 0,2,1,3,0 at q4 bit27. */
    const uint32_t sectors[] = {0xe0800000, 0xf0800000, 0xe8800000, 0xf8800000, 0xe0800000};
    for (unsigned i = 0; i < 5; i++) {
        dev.w1a48 = i; expected[4] = sectors[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; expected[4] = 0xe8800000;
    /* Enable adds bit16. Quality codes 1,2,3,0 occupy q3 bits17..18. */
    const uint32_t overrides[] = {0x914b83af, 0xab0341cf, 0x13478abe, 0x034e1413};
    const uint32_t quality[] = {0x30000, 0x50000, 0x70000, 0x10000};
    for (unsigned i = 0; i < 4; i++) {
        dev.w1a54 = overrides[i]; expected[3] = quality[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; dev.w1a50 = 20; expected[3] = 0x50000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    dev.w1a4c = 0; expected[3] = 0x40000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    dev = nvn_texture_device_words;
    /* Each row puts its five fields at q6:25, q6:23, q3:19, q6:30, q3:21. */
    const uint32_t row_q3[] = {0x70000, 0xf0000, 0x70000, 0x70000, 0x1270000, 0x3e70000};
    const uint32_t row_q6[] = {0x1000000, 0x41000000, 0x3000000, 0x3800000, 0x7800000, 0x47800000};
    for (unsigned i = 0; i < 6; i++) {
        dev.w1a88 = i; expected[3] = row_q3[i]; expected[6] = row_q6[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; expected[3] = 0x70000; expected[6] = 0x3000000;
    /* Targets 5 and 6 both use the sample table. Samples 0,2,4,8,16,3 give 0,5,2,4,6,0. */
    const uint32_t samples[] = {0, 2, 4, 8, 16, 3};
    const uint32_t sample_words[] = {0, 0x500, 0x200, 0x400, 0x600, 0};
    for (unsigned target = 5; target <= 6; target++) {
        t.builder.target = target; expected[4] = target == 5 ? 0xe8800000 : 0xea800000;
        for (unsigned i = 0; i < 6; i++) {
            t.layout.samples = samples[i]; expected[7] = sample_words[i];
            check_texture_descriptor(&t, NULL, &dev, &r, expected);
        }
    }
    /* Target 7 clears q5 bit31. A dimension above +8 changes its target code from 1 to 7. */
    t.builder.target = 7; expected[4] = 0xe8800000; expected[5] = 0; expected[7] = 0;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    t.layout.width = 0x4001; expected[4] = 0xeb804000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    t.layout.width = 1; t.layout.height = 0x4001; expected[4] = 0xeb800000; expected[5] = 0x4000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    /* Cube array: twelve selected faces become two cubes. A adds six layer strides. */
    t.builder.target = 9; t.layout.height = 1; t.layout.depth = 18;
    NvnTextureView v = {.flags = NVN_VIEW_LAYERS, .base_layer = 6, .layer_count = 12};
    expected[1] = 0x1234b600; expected[4] = 0xec060000; expected[5] = 0x80010000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    v.layer_count = 0;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    /* Target 6 also applies layer count; target 7 retains all 18 layout layers. */
    t.builder.target = 6; v.layer_count = 2; expected[4] = 0xea860000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    t.builder.target = 7; expected[4] = 0xe8860000; expected[5] = 0x00110000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    /* Registration: flags bit5 -> q5 bit30; bit3 -> q6 bit0 for block kind 3. */
    t.builder.target = 1; t.builder.flags = 0x28; t.layout.depth = 1;
    expected[1] = 0x12345600; expected[4] = 0xe8800000; expected[5] = 0xc0000000; expected[6] = 0x3000001;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    /* Buffer kind 0 puts flags bit3 at q5 bit31 instead. */
    t.builder.target = 10;
    expected[2] = 4; expected[3] = 0; expected[4] = 0x03000000; expected[6] = 0;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
}
static void test_packaged_tile(void) {
    CHECK(nvn_texture_packaged_tile(0) == 1);
    CHECK(nvn_texture_packaged_tile(0x123) == 0x1231);
    CHECK(nvn_texture_packaged_tile(0xfffffabc) == 0xabc1);
    CHECK(nvn_texture_packaged_tile(UINT32_MAX) == 0xfff1);
}

/* Optional external diagnostic input. The game log remains outside the source tree. */
static void test_texture_queries(const char* path) {
    FILE* file = fopen(path, "r");
    CHECK(file != NULL);
    if (!file) return;
    char line[2048];
    unsigned count = 0, agree = 0;
    while (fgets(line, sizeof line, file)) {
        char* query = strstr(line, "nvn-size: nvnTextureBuilderGetStorage");
        if (!query) continue;
        int alignment = strstr(query, "GetStorageAlignment") != NULL;
        char* result = strstr(query, "result=");
        if (!result) continue;
        uint64_t expected = strtoull(result + 7, NULL, 16);
        uint32_t words[32] = {0};
        unsigned rows = 0;
        for (; rows < 8 && fgets(line, sizeof line, file); rows++) {
            char* row = strstr(line, "nvn-size:   word");
            unsigned at, a, b, c, d;
            if (!row || sscanf(row, "nvn-size:   word %u: %x %x %x %x", &at, &a, &b, &c, &d) != 5 || at != rows * 4) break;
            words[at] = a; words[at + 1] = b; words[at + 2] = c; words[at + 3] = d;
        }
        CHECK(rows == 8);
        if (rows != 8) break;
        NvnTextureBuilder b = {.flags = words[8], .target = words[9], .width = words[10], .height = words[11],
            .depth = words[12], .levels = words[13], .format = words[14], .samples = words[15],
            .tile = (uint16_t)words[24], .stride = words[25], .gl_name = words[26]};
        NvnTextureLayout l;
        nvn_texture_layout(&b, &nvn_texture_device_words, &l);
        uint64_t actual = alignment ? nvn_texture_storage_alignment(&b, &nvn_texture_device_words) : l.size;
        count++;
        if (expected == actual) agree++;
        else printf("query %u: %s actual=%llx expected=%llx\n", count, alignment ? "alignment" : "size",
                    (unsigned long long)actual, (unsigned long long)expected);
    }
    fclose(file);
    printf("texture queries: %u/%u agree\n", agree, count);
    CHECK(count == 40 && agree == count);
}

static void test_pool_range_fits(void) {
    CHECK(nvn_pool_range_fits(0x1000, 0, 0x1000));
    CHECK(nvn_pool_range_fits(0x1000, 0x1000, 0));
    CHECK(!nvn_pool_range_fits(0x1000, 0x1000, 1));
    CHECK(!nvn_pool_range_fits(0x1000, 0x1001, 0));
    CHECK(!nvn_pool_range_fits(0x1000, UINT64_MAX, 2));
    CHECK(!nvn_pool_range_fits(0x1000, 0x800, UINT64_MAX));
}

static void test_buffer_check(void) {
    CHECK(nvn_buffer_check(0x21, 0x1000, 0, 0x1000) == NVN_BUFFER_OK);
    CHECK(nvn_buffer_check(0x21, 0x1000, 0x800, 0) == NVN_BUFFER_OK);
    CHECK(nvn_buffer_check(0x21, 0x1000, 0x800, 0x801) == NVN_BUFFER_OUT_OF_POOL);
    CHECK(nvn_buffer_check(0x21, 0x1000, UINT64_MAX, 2) == NVN_BUFFER_OUT_OF_POOL);
    CHECK(nvn_buffer_check(0x121, 0x1000, 0, 0x10) == NVN_BUFFER_PHYSICAL_POOL);
    CHECK(nvn_buffer_check(0x121, 0x1000, 0, 0x2000) == NVN_BUFFER_PHYSICAL_POOL); /* the flag is tested first */
}

static void test_texture_layout_wide_size(void) {
    NvnTextureBuilder b;
    NvnTextureLayout l;
    memset(&b, 0, sizeof b);
    nvn_texture_builder_defaults(&b);
    b.format = 0x25; b.width = 1; b.height = 0x4000; b.flags = NVN_TEXTURE_FLAG_LINEAR; b.stride = 0x40000;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l) && l.size == 0x100000000ull);
    b.target = NVN_TEXTURE_TARGET_BUFFER; b.flags = 0; b.width = 0x40000000u;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l) && l.size == 0x100000000ull);
}

static void test_descriptor_offset(void) {
    uint64_t offset = 99;
    CHECK(nvn_descriptor_offset(0x100, 4, 0x1000, 32, 0, &offset) && offset == 0x100);
    CHECK(nvn_descriptor_offset(0x100, 4, 0x1000, 32, 3, &offset) && offset == 0x160);
    CHECK(nvn_descriptor_offset(0xf80, 4, 0x1000, 32, 3, &offset) && offset == 0xfe0);
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, -1, &offset));
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, 4, &offset));
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, INT32_MIN, &offset));
    CHECK(!nvn_descriptor_offset(0xf81, 4, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(0x1001, 0, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(UINT64_MAX, 1, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(0, UINT32_MAX, 0x1000, 32, 0, &offset));
}

static void test_rt_format_word(void) {
    CHECK(nvn_rt_format_word(0) == 0);
    CHECK(nvn_rt_format_word(0x01) == 0xf3);
    CHECK(nvn_rt_format_word(0x02) == 0xf4);
    CHECK(nvn_rt_format_word(0x25) == 0xd5);
    CHECK(nvn_rt_format_word(0x29) == 0xca);
    CHECK(nvn_rt_format_word(0x34) == 0x0a);
    CHECK(nvn_rt_format_word(0x35) == 0x14);
    CHECK(nvn_rt_format_word(0x6d) == 0xf9);
    CHECK(nvn_rt_format_word(0x95) == 0xe8);
    CHECK(nvn_rt_format_word(0x9c) == 0xd0);
    CHECK(nvn_rt_format_word(0x19) == 0);
    CHECK(nvn_rt_format_word(0x39) == 0);
    CHECK(nvn_rt_format_word(0x98) == 0);
    CHECK(nvn_rt_format_word(0xa0) == 0);
    CHECK(nvn_rt_format_word(0xa1) == 0);
    CHECK(nvn_rt_format_word(UINT32_MAX) == 0);
}

static void test_window_present_args(void) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    t.builder.width = 640;
    t.builder.height = 480;
    t.builder.format = 0x25; /* RGBA8, the RT word 0xD5 */
    t.builder.offset = 0x1000;
    t.layout.tile_y = 4;
    NvnWindowCrop crop = {8, 9, 100, 120};
    NvnPresentArgs args;
    memset(&args, 0, sizeof args);
    CHECK(nvn_window_present_args(&t, 0x2000, &crop, &args));
    CHECK(args.cpu_addr == 0x3000 && args.w == 640 && args.h == 480);
    CHECK(args.layout == NVN_PRESENT_LAYOUT_BLOCK && args.pitch == 640 * 4 && args.bhl == 4);
    CHECK(args.fmt == NVN_PRESENT_FORMAT_RGBA8888);
    CHECK(args.crop[0] == 8 && args.crop[1] == 9 && args.crop[2] == 108 && args.crop[3] == 129);

    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    t.layout.linear_stride = 0x800;
    CHECK(nvn_window_present_args(&t, 0x2000, &crop, &args));
    CHECK(args.layout == NVN_PRESENT_LAYOUT_PITCH && args.pitch == 0x800 && args.bhl == 0);

    NvnPresentArgs before = args;
    t.builder.format = 0x01; /* not RGBA8 */
    CHECK(!nvn_window_present_args(&t, 0x2000, &crop, &args));
    CHECK(memcmp(&args, &before, sizeof args) == 0);

    t.builder.format = 0x25;
    NvnWindowCrop wide = {INT32_MAX, 0, 1, 1};
    CHECK(!nvn_window_present_args(&t, 0x2000, &wide, &args));
    t.builder.offset = UINT64_MAX;
    CHECK(!nvn_window_present_args(&t, 1, &crop, &args));
}

static void test_sync_after_token(void) {
    uint32_t fences = 2;
    CHECK(nvn_sync_after_token(NVN_SYNC_FENCED, &fences) == NVN_SYNC_FENCED && fences == 1);
    CHECK(nvn_sync_after_token(NVN_SYNC_FENCED, &fences) == NVN_SYNC_SIGNALED && fences == 0);
    CHECK(nvn_sync_after_token(NVN_SYNC_FENCED, &fences) == NVN_SYNC_SIGNALED && fences == 0);
    CHECK(nvn_sync_after_token(NVN_SYNC_SIGNALED, &fences) == NVN_SYNC_SIGNALED && fences == 0);
    CHECK(nvn_sync_after_token(NVN_SYNC_UNSIGNALED, &fences) == NVN_SYNC_SIGNALED && fences == 0);
    CHECK(nvn_sync_after_token(NVN_SYNC_FENCED, NULL) == NVN_SYNC_FENCED);
    fences = 3;
    CHECK(nvn_sync_after_token(NVN_SYNC_FENCED, &fences) == NVN_SYNC_FENCED && fences == 2);
}

static void test_handles(void) {
    CHECK(nvn_texture_handle(0x147, 0x100) == 0x110000147ull);
    CHECK(nvn_texture_handle(0, 0) == 0x100000000ull);
    CHECK(nvn_texture_handle(0x80000000u, 0) == 0xFFFFFFFF80000000ull);
    CHECK(nvn_texture_handle(0, 0x800u) == 0xFFFFFFFF80000000ull);
    CHECK(nvn_image_handle(0) == 0);
    CHECK(nvn_image_handle(5) == 5);
    CHECK(nvn_image_handle(0x80000000u) == 0xFFFFFFFF80000000ull);
}

static void test_bind_buffer_active(void) {
    CHECK(!nvn_bind_buffer_active(NVN_BIND_UNIFORM, 0x400000000ull, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_UNIFORM, 0x400000000ull, 16));
    CHECK(nvn_bind_buffer_active(NVN_BIND_UNIFORM, 0, 16));
    CHECK(nvn_bind_buffer_active(NVN_BIND_STORAGE, 0x400000000ull, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_STORAGE, 0, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_STORAGE, 0x400000000ull, 16));
    CHECK(!nvn_bind_buffer_active(NVN_BIND_VERTEX, 0x400000000ull, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_VERTEX, 0, 16));
    CHECK(nvn_bind_buffer_active(NVN_BIND_VERTEX, 0x400000000ull, 16));
    CHECK(!nvn_bind_buffer_active(NVN_BIND_TRANSFORM_FEEDBACK, 0, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_TRANSFORM_FEEDBACK, 0x400000000ull, 0));
    CHECK(nvn_bind_buffer_active(NVN_BIND_TRANSFORM_FEEDBACK, 0x400000000ull, 16));
}

static void test_state_insert_and_flag(void) {
    CHECK(nvn_state_insert(0xFFFFFFFFu, 0x7u, 0, 0x2u) == 0xFFFFFFFAu);
    CHECK(nvn_state_insert(0, 0x18u, 3, 1) == 8);
    CHECK(nvn_state_insert(0x12345678u, 0xFFFF0000u, 16, 0xABCDu) == 0xABCD5678u);
    CHECK(nvn_state_insert(0, 0xFu, 0, 0x1Fu) == 0x1Fu);
    CHECK(nvn_state_insert(0, 0xFu, 28, 0x1Fu) == 0xF0000000u);
    CHECK(nvn_state_insert(0x80000000u, 0x80000000u, 31, 0) == 0);
    CHECK(nvn_state_set_flag(0, 0, 1) == 1);
    CHECK(nvn_state_set_flag(1, 0, 0x100) == 0);
    CHECK(nvn_state_set_flag(0, 31, 0x1FF) == 0x80000000u);
    CHECK(nvn_state_set_flag(0x80000000u, 31, 0x100) == 0);
    CHECK(nvn_state_set_flag(0xFFFFFFFFu, 5, 2) == 0xFFFFFFFFu);
    CHECK(nvn_state_set_flag(0, 3, 0xFF) == 8);
}

static void test_state_setters(void) {
    CHECK(nvn_blend_state_set_blend_func(0x01020102u, 2, 1, 2, 1) == 0x01020102u);
    CHECK(nvn_blend_state_set_blend_func(0, 0x81u, 0x82u, 0x83u, 0x84u) == 0x84838281u);
    CHECK(nvn_blend_state_set_blend_func(0, 0x12345678u, 0, 0, 0) == 0x78u);
    CHECK(nvn_blend_state_set_blend_equation(0x110C0000u, 1, 1) == 0x110C0000u);
    CHECK(nvn_blend_state_set_blend_equation(0xFFFFFFFFu, 0, 0) == 0x88FFFFFFu);
    CHECK(nvn_blend_state_set_blend_equation(0, 0xFFu, 0xFu) == 0xFF000000u);
    CHECK(nvn_blend_state_set_blend_equation(0, 0x10u, 0) == 0);

    CHECK(nvn_channel_mask_state_set_channel_mask(0xFFFFFFFFu, 0, 1, 0, 0, 0) == 0xFFFFFFF1u);
    CHECK(nvn_channel_mask_state_set_channel_mask(0, 3, 1, 1, 1, 1) == 0xF000u);
    CHECK(nvn_channel_mask_state_set_channel_mask(0, 9, 1, 0, 0, 0) == 0x10u);
    CHECK(nvn_channel_mask_state_set_channel_mask(0xFFFFFFFFu, 0, 0x100u, 0, 0, 0) == 0xFFFFFFF0u);
    CHECK(nvn_channel_mask_state_set_channel_mask(0, 15, 0, 1, 0, 0) == 0x20000000u);

    CHECK(nvn_color_state_set_blend_enable(0x08030000u, 0, 1) == 0x08030100u);
    CHECK(nvn_color_state_set_blend_enable(0x08030000u, 1, 1) == 0x08030200u);
    CHECK(nvn_color_state_set_blend_enable(0x08000000u, 8, 1) == 0x08010000u);
    CHECK(nvn_color_state_set_blend_enable(0x08030000u, 24, 1) == 0x08030000u);
    CHECK(nvn_color_state_set_blend_enable(0x08030000u, 0, 0x100u) == 0x08030000u);
    CHECK(nvn_color_state_set_blend_enable(0x12345678u, 0, 1) == 0x12345778u);

    CHECK(nvn_depth_stencil_state_set_stencil_func(0x11180118u, 1, 5) == 0x11180115u);
    CHECK(nvn_depth_stencil_state_set_stencil_func(0x11180118u, 2, 6) == 0x11160118u);
    CHECK(nvn_depth_stencil_state_set_stencil_func(0x11180118u, 3, 0x123u) == 0x11330133u);
    CHECK(nvn_depth_stencil_state_set_stencil_func(0x11180118u, 0, 5) == 0x11180118u);

    CHECK(nvn_depth_stencil_state_set_stencil_op(0x11180118u, 1, 2, 3, 4) == 0x11183428u);
    CHECK(nvn_depth_stencil_state_set_stencil_op(0x11180118u, 2, 5, 6, 7) == 0x67580118u);
    CHECK(nvn_depth_stencil_state_set_stencil_op(0x11180118u, 0, 2, 3, 4) == 0x11180118u);
    CHECK(nvn_depth_stencil_state_set_stencil_op(0, 1, 0x10000u, 0, 0) == 0x100000u);
    CHECK(nvn_depth_stencil_state_set_stencil_op(0, 2, 0, 0, 0x1Fu) == 0x1F000000u);
}

static void test_state_defaults(void) {
    uint32_t w[6];
    nvn_color_state_defaults(w);
    CHECK(w[0] == 0x08030000u);
    nvn_depth_stencil_state_defaults(w);
    CHECK(w[0] == 0x00000022u && w[1] == 0x11180118u);
    nvn_blend_state_defaults(w);
    CHECK(w[0] == 0x110C0000u && w[1] == 0x01020102u);
    nvn_polygon_state_defaults(w);
    CHECK(w[0] == 0x14u);
    nvn_channel_mask_state_defaults(w);
    CHECK(w[0] == 0xFFFFFFFFu);
    for (uint32_t i = 0; i < 6; i++) w[i] = 0xDEADBEEFu;
    nvn_multisample_state_defaults(w);
    CHECK(w[0] == 5 && w[1] == 0x88888888u && w[2] == 0x88888888u && w[3] == 0x88888888u);
    CHECK(w[4] == 0x88888888u && w[5] == 0);
}

static void test_state_getters(void) {
    uint32_t out[4];
    uint8_t bytes[4];
    CHECK(nvn_blend_state_get_blend_target(0x110C0000u) == 0);
    CHECK(nvn_blend_state_get_blend_target(0xFFFFFFFFu) == 7);
    CHECK(nvn_blend_state_get_blend_target(5) == 5);
    nvn_blend_state_get_blend_func(0x01020102u, out);
    CHECK(out[0] == 2 && out[1] == 1 && out[2] == 2 && out[3] == 1);
    nvn_blend_state_get_blend_func(0xFFFFFFFFu, out);
    CHECK(out[0] == 0x7Fu && out[1] == 0x7Fu && out[2] == 0x7Fu && out[3] == 0x7Fu);
    nvn_channel_mask_state_get_channel_mask(0xFFFFFFFFu, 0, bytes);
    CHECK(bytes[0] == 1 && bytes[1] == 1 && bytes[2] == 1 && bytes[3] == 1);
    nvn_channel_mask_state_get_channel_mask(0x0000000Au, 0, bytes);
    CHECK(bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 1);
    nvn_channel_mask_state_get_channel_mask(0xF0000000u, 7, bytes);
    CHECK(bytes[0] == 1 && bytes[1] == 1 && bytes[2] == 1 && bytes[3] == 1);
    nvn_channel_mask_state_get_channel_mask(0x0000000Au, 8, bytes);
    CHECK(bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 1);

    CHECK(nvn_color_state_get_alpha_test(0x08030000u) == 8);
    CHECK(nvn_color_state_get_alpha_test(0xFF000000u) == 0xF);
    CHECK(nvn_color_state_get_blend_enable(0x08030000u, 0) == 0);
    CHECK(nvn_color_state_get_blend_enable(0x00000100u, 0) == 1);
    CHECK(nvn_color_state_get_blend_enable(0x00008000u, 7) == 1);
    CHECK(nvn_color_state_get_blend_enable(0x00000100u, 8) == 0);

    CHECK(nvn_depth_stencil_state_get_depth_func(0x00000022u) == 2);
    CHECK(nvn_depth_stencil_state_get_depth_test_enable(0x00000022u) == 0);
    CHECK(nvn_depth_stencil_state_get_depth_write_enable(0x00000022u) == 1);
    CHECK(nvn_depth_stencil_state_get_depth_func(0xFFFFFFFFu) == 0xF);
    CHECK(nvn_depth_stencil_state_get_depth_test_enable(0xFFFFFFFFu) == 1);
    CHECK(nvn_depth_stencil_state_get_depth_write_enable(0) == 0);

    CHECK(nvn_multisample_state_get_multisample_enable(5) == 1);
    CHECK(nvn_multisample_state_get_samples(5) == 0);
    CHECK(nvn_multisample_state_get_alpha_to_coverage_enable(5) == 0);
    CHECK(nvn_multisample_state_get_alpha_to_coverage_dither(5) == 1);
    CHECK(nvn_multisample_state_get_alpha_to_coverage_dither(1) == 0);
    CHECK(nvn_multisample_state_get_coverage_modulation_mode(5) == 0);
    CHECK(nvn_multisample_state_get_raster_samples(5) == 0);
    CHECK(nvn_multisample_state_get_coverage_to_color_enable(5) == 0);
    CHECK(nvn_multisample_state_get_coverage_to_color_output(5) == 0);
    CHECK(nvn_multisample_state_get_multisample_enable(0xFFFFFFFFu) == 1);
    CHECK(nvn_multisample_state_get_samples(0xFFFFFFFFu) == 0x1F);
    CHECK(nvn_multisample_state_get_alpha_to_coverage_enable(0xFFFFFFFFu) == 1);
    CHECK(nvn_multisample_state_get_alpha_to_coverage_dither(0xFFFFFFFFu) == 1);
    CHECK(nvn_multisample_state_get_coverage_modulation_mode(0xFFFFFFFFu) == 3);
    CHECK(nvn_multisample_state_get_raster_samples(0xFFFFFFFFu) == 0x1F);
    CHECK(nvn_multisample_state_get_coverage_to_color_enable(0xFFFFFFFFu) == 1);
    CHECK(nvn_multisample_state_get_coverage_to_color_output(0xFFFFFFFFu) == 7);

    CHECK(nvn_polygon_state_get_cull_face(0x14u) == 0);
    CHECK(nvn_polygon_state_get_front_face(0x14u) == 1);
    CHECK(nvn_polygon_state_get_cull_face(3) == 3);
    CHECK(nvn_polygon_state_get_front_face(3) == 0);
    CHECK(nvn_polygon_state_get_cull_face(0xFFFFFFFFu) == 3);
    CHECK(nvn_polygon_state_get_front_face(0xFFFFFFFFu) == 1);
}

static void test_state_getters_extra(void) {
    uint32_t rgb = 0xFFFFu, alpha = 0xFFFFu;
    CHECK(nvn_color_state_get_logic_op(0x08030000u) == 3u);
    CHECK(nvn_color_state_get_logic_op(0x00AB0000u) == 0xABu);
    CHECK(nvn_color_state_get_logic_op(0xFFFFFFFFu) == 0xFFu);

    CHECK(nvn_depth_stencil_state_get_stencil_test_enable(0x00000022u) == 0u);
    CHECK(nvn_depth_stencil_state_get_stencil_test_enable(0x00000004u) == 1u);
    CHECK(nvn_depth_stencil_state_get_stencil_test_enable(0xFFFFFFFFu) == 1u);

    CHECK(nvn_depth_stencil_state_get_stencil_func(0x11180118u, 0) == 8u);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0x11180118u, 1) == 8u);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0x11180118u, 2) == 8u);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0x1234ABCDu, 0) == 0xDu);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0x1234ABCDu, 2) == 0x4u);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0x1234ABCDu, 3) == 0xDu);
    CHECK(nvn_depth_stencil_state_get_stencil_func(0xFFFFFFFFu, 2) == 0xFu);

    CHECK(nvn_polygon_state_get_polygon_mode(0x14u) == 2u);
    CHECK(nvn_polygon_state_get_polygon_mode(0x08u) == 1u);
    CHECK(nvn_polygon_state_get_polygon_mode(0xFFFFFFFFu) == 3u);

    nvn_blend_state_get_blend_equation(0x110C0000u, &rgb, &alpha);
    CHECK(rgb == 1u && alpha == 1u);
    nvn_blend_state_get_blend_equation(0x28000000u, &rgb, &alpha);
    CHECK(rgb == 0u && alpha == 2u);
    nvn_blend_state_get_blend_equation(0xFFFFFFFFu, &rgb, &alpha);
    CHECK(rgb == 7u && alpha == 7u);
    alpha = 0;
    nvn_blend_state_get_blend_equation(0xFFFFFFFFu, NULL, &alpha);
    CHECK(alpha == 7u);
    rgb = 0;
    nvn_blend_state_get_blend_equation(0xFFFFFFFFu, &rgb, NULL);
    CHECK(rgb == 7u);
}

static void test_vertex_attrib_state(void) {
    CHECK(nvn_vertex_attrib_state_set_format(0, 0x22, 0) == 0x04400040u);
    CHECK(nvn_vertex_attrib_state_set_format(0xFFFFFFFFu, 0x22, 0xC) == 0x8440067Fu);
    CHECK(nvn_vertex_attrib_state_set_format(0, 0x400, 0) == 0x80000040u);       /* format bit 10 reaches bit 31 */
    CHECK(nvn_vertex_attrib_state_set_format(0, 0, 0x4000) == 0x00200040u);      /* offset bit 14 reaches bit 21 */
    CHECK(nvn_vertex_attrib_state_set_format(0x3Fu, 0, 0) == 0x7Fu);             /* bits 0-5 kept, bit 6 set */
    CHECK(nvn_vertex_attrib_state_set_stream_index(0, 5) == 5u);
    CHECK(nvn_vertex_attrib_state_set_stream_index(0xFFFFFFFFu, 0) == 0xFFFFFFE0u);
    CHECK(nvn_vertex_attrib_state_set_stream_index(0, 0x100) == 0x100u);          /* not masked: spills into stride */
    CHECK(nvn_vertex_attrib_state_set_stream_index(0x20u, 0) == 0x20u);           /* bit 5 kept */

    uint32_t format = 0;
    uint64_t offset = 0;
    uint32_t word = nvn_vertex_attrib_state_set_stream_index(nvn_vertex_attrib_state_set_format(0, 0x22, 0xC), 7);
    nvn_vertex_attrib_state_get_format(word, &format, &offset);
    CHECK(format == 0x22u && offset == 0xCu && nvn_vertex_attrib_state_get_stream_index(word) == 7u);
    nvn_vertex_attrib_state_get_format(0xFFFFFFFFu, &format, &offset);
    CHECK(format == 0x3FFu && offset == 0x3FFFu);                                /* bit 31 and bits 0-6 not read */
    CHECK(nvn_vertex_attrib_state_get_stream_index(0xFFFFFFFFu) == 0x1Fu);         /* bit 5 not read */
}

static void test_xfb_words(void) {
    uint32_t out[NVN_XFB_WORDS_MAX];
    CHECK(nvn_bind_xfb_words(2, 0x123456780ull, 0x100u, out) == 5u);
    CHECK(out[0] == 0x200400F0u && out[1] == 1u && out[2] == 1u && out[3] == 0x23456780u && out[4] == 0x100u);
    CHECK(nvn_bind_xfb_words(3, 0, 0x100u, out) == 1u && out[0] == 0x800000F8u); /* a null buffer: inline 0 */
    CHECK(nvn_bind_xfb_words(0, 0x10ull, 0, out) == 5u && out[0] == 0x200400E0u);
    CHECK(nvn_bind_xfb_words(NVN_XFB_BUFFER_COUNT, 0x10ull, 4u, out) == 0u);

    CHECK(nvn_xfb_words(NVN_XFB_BEGIN, 0x1234567800ull, out) == 3u);
    CHECK(out[0] == 0xA0020E00u && out[1] == 0x12u && out[2] == 0x34567800u);

    static const uint32_t query[4] = {0x1D005002u, 0x1D005022u, 0x1D005042u, 0x1D005062u};
    CHECK(nvn_xfb_words(NVN_XFB_END, 0x1FFFFFFF8ull, out) == 21u);
    CHECK(out[0] == 0x800001D1u);
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t* r = out + 1 + 5 * i;
        uint64_t at = 0x1FFFFFFF8ull + 4u * (i + 1u);
        CHECK(r[0] == 0x200406C0u && r[1] == (uint32_t)(at >> 32) && r[2] == (uint32_t)at);
        CHECK(r[3] == 0u && r[4] == query[i]);
    }
    CHECK(out[7] == 2u && out[8] == 0u); /* buffer 1 at 0x200000000: the high word carries */
    CHECK(nvn_xfb_words(NVN_XFB_KIND_COUNT, 0, out) == 0u);
}

static void test_reset_counter_word(void) {
    uint32_t word = 0;
    CHECK(nvn_reset_counter_word(0, &word) && word == 0x01u);
    CHECK(nvn_reset_counter_word(2, &word) && word == 0x12u);
    CHECK(nvn_reset_counter_word(13, &word) && word == 0x1Fu);
    CHECK(nvn_reset_counter_word(16, &word) && word == 0x01u); /* the timestamp type */
    word = 77;
    CHECK(!nvn_reset_counter_word(17, &word) && word == 77u);
    CHECK(!nvn_reset_counter_word(0xFFFFFFFFu, &word) && word == 77u);
}

static void test_depth_range_methods(void) {
    NvnDepthRangeMethod out[NVN_DEPTH_RANGE_METHODS_MAX];
    nvn_depth_range_methods(3, 1, 0.25f, 0.75f, out);
    CHECK(out[0].method == 0x29Au && out[0].value == 0x3F000000u); /* 0x282 + 8*3, far - near */
    CHECK(out[1].method == 0x29Du && out[1].value == 0x3E800000u); /* 0x285 + 8*3, near */
    CHECK(out[2].method == 0x30Eu && out[2].value == 0x3E800000u); /* 0x302 + 4*3, minimum */
    CHECK(out[3].method == 0x30Fu && out[3].value == 0x3F400000u); /* 0x303 + 4*3, maximum */
    nvn_depth_range_methods(0, 0, 0.25f, 0.75f, out);
    CHECK(out[0].method == 0x282u && out[0].value == 0x3E800000u); /* (far - near) * 0.5 */
    CHECK(out[1].method == 0x285u && out[1].value == 0x3F000000u); /* (near + far) * 0.5 */
    CHECK(out[2].value == 0x3E800000u && out[3].value == 0x3F400000u);
    nvn_depth_range_methods(1, 1, 1.0f, 0.0f, out); /* near above far */
    CHECK(out[0].method == 0x28Au && out[0].value == 0xBF800000u);
    CHECK(out[1].method == 0x28Du && out[1].value == 0x3F800000u);
    CHECK(out[2].method == 0x306u && out[2].value == 0x00000000u);
    CHECK(out[3].method == 0x307u && out[3].value == 0x3F800000u);
    nvn_depth_range_methods(2, 0, 0.5f, 0.5f, out); /* equal */
    CHECK(out[0].value == 0 && out[1].value == 0x3F000000u);
    CHECK(out[2].value == 0x3F000000u && out[3].value == 0x3F000000u);
    nvn_depth_range_methods(0, 0, NAN, 0.5f, out); /* a NaN near takes far in both the minimum and the maximum */
    float value;
    memcpy(&value, &out[0].value, sizeof value);
    CHECK(isnan(value));
    memcpy(&value, &out[1].value, sizeof value);
    CHECK(isnan(value));
    CHECK(out[2].value == 0x3F000000u && out[3].value == 0x3F000000u);
}

static void test_stencil_op_and_polygon_offset(void) {
    uint32_t out[3];
    nvn_depth_stencil_state_get_stencil_op(0x1234ABCDu, 2, out);
    CHECK(out[0] == 3u && out[1] == 1u && out[2] == 2u);
    nvn_depth_stencil_state_get_stencil_op(0x1234ABCDu, 0, out);
    CHECK(out[0] == 0xCu && out[1] == 0xAu && out[2] == 0xBu);
    nvn_depth_stencil_state_get_stencil_op(0x1234ABCDu, 1, out);
    CHECK(out[0] == 0xCu && out[1] == 0xAu && out[2] == 0xBu);
    nvn_depth_stencil_state_get_stencil_op(0x1234ABCDu, 3, out);
    CHECK(out[0] == 0xCu && out[1] == 0xAu && out[2] == 0xBu);
    nvn_depth_stencil_state_get_stencil_op(0xFFFFFFFFu, 2, out);
    CHECK(out[0] == 0xFu && out[1] == 0xFu && out[2] == 0xFu);
    CHECK(nvn_polygon_state_get_polygon_offset_enables(0x78000u) == 0xFu);
    CHECK(nvn_polygon_state_get_polygon_offset_enables(0x12345678u) == 8u);
    CHECK(nvn_polygon_state_get_polygon_offset_enables(0) == 0u);
    CHECK(nvn_polygon_state_get_polygon_offset_enables(0xFFFFFFFFu) == 0xFu);
}

static void test_raster_methods(void) {
    NvnRasterMethod out[NVN_RASTER_METHODS_MAX];
    uint32_t count = 0;
    CHECK(nvn_raster_methods(NVN_RASTER_DEPTH_BOUNDS, (const uint32_t[3]){1u, 0x3F000000u, 0x3F800000u}, out,
                             &count));
    CHECK(count == 3u);
    CHECK(out[0].method == NVN_RASTER_METHOD_DEPTH_BOUNDS && out[0].value == 1u);
    CHECK(out[1].method == NVN_RASTER_METHOD_DEPTH_BOUNDS_MIN && out[1].value == 0x3F000000u);
    CHECK(out[2].method == NVN_RASTER_METHOD_DEPTH_BOUNDS_MAX && out[2].value == 0x3F800000u);
    CHECK(nvn_raster_methods(NVN_RASTER_DEPTH_BOUNDS, (const uint32_t[3]){0x100u, 1u, 2u}, out, &count));
    CHECK(count == 1u && out[0].method == 0x66Fu && out[0].value == 0u);

    CHECK(nvn_raster_methods(NVN_RASTER_DISCARD, (const uint32_t[3]){0u, 0u, 0u}, out, &count));
    CHECK(count == 1u && out[0].method == 0x0DFu && out[0].value == 1u);
    CHECK(nvn_raster_methods(NVN_RASTER_DISCARD, (const uint32_t[3]){1u, 0u, 0u}, out, &count));
    CHECK(count == 1u && out[0].value == 0u);

    CHECK(nvn_raster_methods(NVN_RASTER_POLYGON_OFFSET_CLAMP,
                             (const uint32_t[3]){0xABCDu, 0x3FC00000u, 0x61F00000u}, out, &count));
    CHECK(count == 3u);
    CHECK(out[0].method == 0x55Bu && out[0].value == 0xABCDu);
    CHECK(out[1].method == 0x56Fu && out[1].value == 0x40400000u); /* 1.5 + 1.5 = 3.0 */
    CHECK(out[2].method == 0x61Fu && out[2].value == 0x61F00000u);
    uint32_t nan_pattern = 0x7FC00001u;
    CHECK(nvn_raster_methods(NVN_RASTER_POLYGON_OFFSET_CLAMP, (const uint32_t[3]){0u, nan_pattern, 0u}, out,
                             &count));
    float doubled;
    memcpy(&doubled, &out[1].value, sizeof doubled);
    CHECK(isnan(doubled));

    CHECK(nvn_raster_methods(NVN_RASTER_DEPTH_CLAMP, (const uint32_t[3]){1u, 0u, 0u}, out, &count));
    CHECK(count == 1u && out[0].method == 0x64Fu && out[0].value == 0x101Au);
    CHECK(nvn_raster_methods(NVN_RASTER_DEPTH_CLAMP, (const uint32_t[3]){0u, 0u, 0u}, out, &count));
    CHECK(count == 1u && out[0].value == 0x181Du);

    CHECK(nvn_raster_methods(NVN_RASTER_LINE_WIDTH, (const uint32_t[3]){0x40000000u, 0u, 0u}, out, &count));
    CHECK(count == 2u);
    CHECK(out[0].method == 0x4ECu && out[0].value == 0x40000000u);
    CHECK(out[1].method == 0x4EDu && out[1].value == 0x40000000u);

    CHECK(nvn_raster_methods(NVN_RASTER_POINT_SIZE, (const uint32_t[3]){0x40800000u, 0u, 0u}, out, &count));
    CHECK(count == 2u);
    CHECK(out[0].method == 0x546u && out[0].value == 0x40800000u); /* the float as given */
    CHECK(out[1].method == 0x68Bu && out[1].value == 0u);

    count = 77;
    CHECK(!nvn_raster_methods(NVN_RASTER_KIND_COUNT, (const uint32_t[3]){0u, 0u, 0u}, out, &count));
    CHECK(count == 77u);
}

static void test_program_header_stage(void) {
    uint32_t index = 99;
    CHECK(nvn_program_header_stage(1, 5, 0x120, 0xb, 0, &index) && index == 0);
    index = 99;
    CHECK(nvn_program_header_stage(1, 16, 0x120, 0xb, 1, &index) && index == 1);
    index = 99;
    CHECK(nvn_program_header_stage(1, 10, 0x120, 0xb, 5, &index) && index == 5);
    index = 99;
    CHECK(!nvn_program_header_stage(1, 4, 0x120, 0xb, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(1, 17, 0x120, 0xb, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(0, 5, 0x120, 0xb, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(2, 5, 0x120, 0xb, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(1, 5, 0x121, 0xb, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(1, 5, 0x120, 0xc, 0, &index) && index == 99);
    CHECK(!nvn_program_header_stage(1, 5, 0x120, 0xb, 6, &index) && index == 99);
    CHECK(!nvn_program_header_stage(1, 5, 0x120, 0xb, 0, NULL));
}

static void test_program_build_vertex(void) {
    const NvnProgramDeviceIn dev = {0};
    const NvnProgramStageIn vertex = {.code_offset = 0x1234u, .cb_offset = 0x560u, .header_value = 0x40u};
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 77;
    CHECK(nvn_program_build_words(NULL, &vertex, NULL, NULL, &dev, 0x400000000ull, words, &count));
    CHECK(count == NVN_PROGRAM_VERTEX_WORDS);
    /* IMM(0x800,0); macro p0..p4 = 1,0,code,0,(P+cb)>>8; IMM(0x813,0x40). */
    CHECK(words[0] == 0x80000800u);
    CHECK(words[1] == 0xa0050e18u);
    CHECK(words[2] == 1u);
    CHECK(words[3] == 0u);
    CHECK(words[4] == 0x1234u);
    CHECK(words[5] == 0u);
    CHECK(words[6] == 0x04000005u);
    CHECK(words[7] == 0x80400813u);
}

static void test_program_build_vertex_fragment(void) {
    const NvnProgramStageIn vertex = {.code_offset = 0x100u, .cb_offset = 0x200u, .header_value = 0x30u};
    const NvnProgramStageIn fragment = {.code_offset = 0x300u, .cb_offset = 0x400u, .header_value = 0x20u};
    const NvnProgramDeviceIn dev = {
        .d1a38 = 0x087f6080u, .d1a68 = 0x20u, .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u,
    };
    const NvnProgramFragmentIn frag = {0};
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 0;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u);
    /* vertex: 8 words. fragment: IMM(0x850,0x51), macro p0..p4 = 5,0,code,4,(P+cb)>>8, IMM(0x853,0x20), then
       0,0,0x200200d8, d1a38 (top byte 8, already below 0x20), d1a68, IMM(0x489,0), IMM(0x65B,0), IMM(0x1D5,1). */
    CHECK(words[0] == 0x80000800u);
    CHECK(words[1] == 0xa0050e18u);
    CHECK(words[2] == 1u);
    CHECK(words[3] == 0u);
    CHECK(words[4] == 0x100u);
    CHECK(words[5] == 0u);
    CHECK(words[6] == 0x04000002u);
    CHECK(words[7] == 0x80300813u);
    CHECK(words[8] == 0x80510850u);
    CHECK(words[9] == 0xa0050e18u);
    CHECK(words[10] == 5u);
    CHECK(words[11] == 0u);
    CHECK(words[12] == 0x300u);
    CHECK(words[13] == 4u);
    CHECK(words[14] == 0x04000004u);
    CHECK(words[15] == 0x80200853u);
    CHECK(words[16] == 0x800003d0u);
    CHECK(words[17] == 0x80000084u);
    CHECK(words[18] == 0x800003c7u);
    CHECK(words[19] == 0x200200d8u);
    CHECK(words[20] == 0x087f6080u);
    CHECK(words[21] == 0x20u);
    CHECK(words[22] == 0x80000489u);
    CHECK(words[23] == 0x8000065bu);
    CHECK(words[24] == 0x800101d5u);
}

static void test_program_build_fragment_2004(void) {
    const NvnProgramDeviceIn dev = {
        .d1a38 = 0x087f6080u, .d1a68 = 0x20u, .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u,
    };
    const NvnProgramStageIn vertex = {.code_offset = 0x100u, .cb_offset = 0x200u, .header_value = 0x30u};
    const NvnProgramStageIn fragment = {.code_offset = 0x300u, .cb_offset = 0x400u, .header_value = 0x20u};
    NvnProgramFragmentIn frag = {0};
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 0;
    /* g158 != 0 and g160 == 0: 0x3D0 is 1 and the 0x2004 branch adds five words after it. */
    frag.g158 = 1;
    frag.g1a0[0] = 0x11111111u;
    frag.g1a0[1] = 0x22222222u;
    frag.g1a0[2] = 0x33333333u;
    frag.g1a0[3] = 0x44444444u;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 30u);
    CHECK(words[16] == 0x800103d0u);
    CHECK(words[17] == 0x200403d1u);
    CHECK(words[18] == 0x11111111u);
    CHECK(words[19] == 0x22222222u);
    CHECK(words[20] == 0x33333333u);
    CHECK(words[21] == 0x44444444u);
    CHECK(words[22] == 0x80000084u);
    CHECK(words[23] == 0x800003c7u);
    CHECK(words[24] == 0x200200d8u);
    CHECK(words[25] == 0x087f6080u);
    CHECK(words[26] == 0x20u);
    CHECK(words[27] == 0x80000489u);
    CHECK(words[28] == 0x8000065bu);
    CHECK(words[29] == 0x800101d5u);
    /* g160 != 0: no insertion, 0x3D0 stays 0. */
    frag.g160 = 1;
    count = 0;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[16] == 0x800003d0u && words[17] == 0x80000084u);
    /* g158 == 0: no insertion. */
    frag = (NvnProgramFragmentIn){0};
    count = 0;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[16] == 0x800003d0u && words[17] == 0x80000084u);
}

static void test_program_build_fragment_values(void) {
    const NvnProgramDeviceIn dev = {
        .d1a38 = 0x087f6080u, .d1a68 = 0x20u, .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u,
    };
    const NvnProgramStageIn vertex = {.code_offset = 1u, .cb_offset = 2u, .header_value = 3u};
    const NvnProgramStageIn fragment = {.code_offset = 4u, .cb_offset = 5u, .header_value = 6u};
    NvnProgramFragmentIn frag = {0};
    uint32_t words[NVN_PROGRAM_WORDS_MAX];
    uint32_t count;
    /* keys: g190=2 gives key 1. g188!=0: table {1,9,1,9}; g188==0: table {2,10,1,9}; key>=4 gives 0. */
    frag.g188 = 1; frag.g190 = 2;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[22] == 0x80090489u && words[24] == 0x803001d5u);
    frag = (NvnProgramFragmentIn){0}; frag.g190 = 3;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[22] == 0x80010489u && words[24] == 0x800101d5u);
    frag = (NvnProgramFragmentIn){0}; frag.g190 = 5;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[22] == 0x80000489u);
    frag = (NvnProgramFragmentIn){0};
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[22] == 0x80000489u);
    /* d1a40 overrides the table. */
    NvnProgramDeviceIn dev2 = dev;
    dev2.d1a40 = 0x1234u;
    frag.g190 = 5;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[22] == 0x92340489u);
    /* 0x084 = 1 for g148 != 0 or d1a3c != 0. */
    frag = (NvnProgramFragmentIn){0}; frag.g148 = 1;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[17] == 0x80010084u);
    frag = (NvnProgramFragmentIn){0};
    dev2 = dev; dev2.d1a3c = 1;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[17] == 0x80010084u);
    /* D8: g168 clamps the top byte to 0x18; g170 writes the constant; the else clamps to 0x20 and may min d1a74. */
    frag = (NvnProgramFragmentIn){0}; frag.g168 = 1;
    dev2 = dev; dev2.d1a38 = 0x30ab1234u;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[20] == 0x18ab1234u && words[21] == 0x20u);
    frag = (NvnProgramFragmentIn){0}; frag.g170 = 1;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[20] == 0x06164010u && words[21] == 0x20u);
    frag = (NvnProgramFragmentIn){0};
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[20] == 0x20ab1234u && words[21] == 0x20u);
    frag = (NvnProgramFragmentIn){0}; frag.g198 = 3;
    dev2.d1a74 = 0x0a112233u;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[20] == 0x0aab1234u);
    frag.g198 = 2;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev2, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[20] == 0x20ab1234u);
    /* 0x65B = g178 | g180 << 4, no mask. */
    frag = (NvnProgramFragmentIn){0}; frag.g178 = 0x0au; frag.g180 = 0x03u;
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && words[23] == 0x803a065bu);
}

static void test_program_build_compute(void) {
    const NvnProgramComputeIn compute = {
        .code_offset = 0x11111111u, .cb_offset = 0x22222222u,
        .h6fc = 0x33333333u, .h710 = 0x44444444u, .h70c = 0x55555555u,
        .h718 = 0x66666666u, .h71c = 0x77777777u, .h720 = 0x88888888u, .h724 = 0x99999999u,
        .h728 = 0xAAAAAAAAu, .h72c = 0xBBBBBBBBu, .h730 = 0xCCCCCCCCu, .h734 = 0xDDDDDDDDu,
        .h6f8 = 0xEEEEEEEEu, .h750_low = 0x12345678u, .h750_high = 0x9ABCDEF0u,
        .flags = 0x5u,
    };
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 0;
    CHECK(nvn_program_build_words(&compute, NULL, NULL, NULL, NULL, 0, words, &count));
    CHECK(count == NVN_PROGRAM_COMPUTE_WORDS);
    CHECK(words[0] == 0x11111111u);
    CHECK(words[1] == 0x22222222u);
    CHECK(words[2] == 0x33333333u);
    CHECK(words[3] == 0x44444444u);
    CHECK(words[4] == 0x55555555u);
    CHECK(words[5] == 0x66666666u);
    CHECK(words[6] == 0x77777777u);
    CHECK(words[7] == 0x88888888u);
    CHECK(words[8] == 0x99999999u);
    CHECK(words[9] == 0xAAAAAAAAu);
    CHECK(words[10] == 0xBBBBBBBBu);
    CHECK(words[11] == 0xCCCCCCCCu);
    CHECK(words[12] == 0xDDDDDDDDu);
    CHECK(words[13] == 0xEEEEEEEEu);
    CHECK(words[14] == 0x12345678u);
    CHECK(words[15] == 0x9ABCDEF0u);
    CHECK(words[16] == 0x5u);
    CHECK(words[17] == 0u);
    /* Every combination of the three flag bits. */
    NvnProgramComputeIn flagged = compute;
    for (uint32_t flags = 0; flags < 8u; flags++) {
        flagged.flags = flags;
        count = 0;
        CHECK(nvn_program_build_words(&flagged, NULL, NULL, NULL, NULL, 0, words, &count));
        CHECK(count == NVN_PROGRAM_COMPUTE_WORDS && words[16] == flags && words[17] == 0u);
    }
}

static void test_program_build_refusals(void) {
    const NvnProgramDeviceIn dev = {0};
    const NvnProgramStageIn vertex = {.code_offset = 1u, .cb_offset = 2u, .header_value = 0x2000u};
    const NvnProgramStageIn fragment = {.code_offset = 4u, .cb_offset = 5u, .header_value = 6u};
    const NvnProgramComputeIn compute = {.code_offset = 7u};
    NvnProgramFragmentIn frag = {0};
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 77;
    /* An immediate value above 13 bits. */
    CHECK(!nvn_program_build_words(NULL, &vertex, NULL, NULL, &dev, 0, words, &count));
    CHECK(count == 77u);
    CHECK(!nvn_program_build_words(NULL, &vertex, NULL, NULL, &dev, 0, NULL, &count));
    CHECK(!nvn_program_build_words(NULL, NULL, &fragment, &frag, &dev, 0, words, NULL));
    /* A fragment without a vertex block is 17 words. */
    count = 0;
    CHECK(nvn_program_build_words(NULL, NULL, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == NVN_PROGRAM_FRAGMENT_WORDS);
    CHECK(words[0] == 0x80510850u && words[6] == 0x04000000u && words[16] == 0x800101d5u);
    /* g160 != 0 is the normal path, and 0x3D0 stays 0. */
    frag.g158 = 1; frag.g160 = 1;
    count = 0;
    CHECK(nvn_program_build_words(NULL, NULL, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 17u && words[8] == 0x800003d0u);
    /* A compute stage cannot share a program with vertex or fragment. */
    count = 77;
    CHECK(!nvn_program_build_words(&compute, &vertex, NULL, NULL, &dev, 0, words, &count));
    CHECK(count == 77u);
    CHECK(!nvn_program_build_words(&compute, NULL, &fragment, &frag, &dev, 0, words, &count));
    CHECK(count == 77u);
    CHECK(!nvn_program_build_words(&compute, NULL, NULL, NULL, NULL, 0, NULL, &count));
    CHECK(count == 77u);
}

static void test_word_stream(void) {
    NvnWordStream s = {0};
    uint32_t m = 0, v = 0;
    int last = -1;
    /* Mode 1: two words, the method increments and last is only on the second. */
    CHECK(nvn_word_stream_next(&s, 0x20020820u, &m, &v, &last) == 0);
    CHECK(nvn_word_stream_next(&s, 0x20u, &m, &v, &last) == 1 && m == 0x820u && v == 0x20u && last == 0);
    CHECK(nvn_word_stream_next(&s, 0x821u, &m, &v, &last) == 1 && m == 0x821u && v == 0x821u && last == 1);
    CHECK(nvn_word_stream_complete(&s));
    /* Mode 5: the first word goes to 0xE18, the other four to 0xE19, last is only on the fifth. */
    s = (NvnWordStream){0};
    CHECK(nvn_word_stream_next(&s, 0xa0050e18u, &m, &v, &last) == 0);
    for (uint32_t i = 0; i < 5u; i++) {
        CHECK(nvn_word_stream_next(&s, i, &m, &v, &last) == 1 && m == (i ? 0xE19u : 0xE18u) && v == i);
        CHECK(last == (i == 4u));
    }
    CHECK(nvn_word_stream_complete(&s));
    /* Inline: the count field is the value. */
    s = (NvnWordStream){0};
    CHECK(nvn_word_stream_next(&s, 0x80300830u, &m, &v, &last) == 1 && m == 0x830u && v == 0x30u && last == 1);
    CHECK(nvn_word_stream_complete(&s));
    /* Unknown modes, and a run with a count of 0. */
    s = (NvnWordStream){0};
    CHECK(nvn_word_stream_next(&s, 0x00000000u, &m, &v, &last) == -1);
    CHECK(nvn_word_stream_next(&s, 0x40000500u, &m, &v, &last) == -1);
    CHECK(nvn_word_stream_next(&s, 0x60000500u, &m, &v, &last) == -1);
    CHECK(nvn_word_stream_next(&s, 0x20000000u, &m, &v, &last) == -1);
    CHECK(nvn_word_stream_next(&s, 0xa0000000u, &m, &v, &last) == -1);
    /* A run left open. */
    s = (NvnWordStream){0};
    CHECK(nvn_word_stream_next(&s, 0x20020820u, &m, &v, &last) == 0);
    CHECK(nvn_word_stream_next(&s, 0x20u, &m, &v, &last) == 1);
    CHECK(!nvn_word_stream_complete(&s));
    CHECK(!nvn_word_stream_complete(NULL));
    CHECK(nvn_word_stream_next(NULL, 0, &m, &v, &last) == -1);
}

typedef struct {
    uint32_t method[8], value[8], count;
} PushbufRec;

static uint32_t pushbuf_header(uint32_t type, uint32_t count, uint32_t sub, uint32_t method) {
    return (type << 29) | (count << 16) | (sub << 13) | method;
}

static void pushbuf_record(void* ctx, uint32_t method, uint32_t value) {
    PushbufRec* r = ctx;
    if (r->count < 8) {
        r->method[r->count] = method;
        r->value[r->count] = value;
    }
    r->count++;
}

static void test_pushbuf_decode(void) {
    uint32_t words[8];
    uint32_t bad = 0;
    /* Type 1: the method rises by one for each data word. */
    PushbufRec rec = {0};
    words[0] = pushbuf_header(1, 2, 0, 0x100);
    words[1] = 0xAAAA;
    words[2] = 0xBBBB;
    CHECK(nvn_pushbuf_decode(words, 3, pushbuf_record, &rec, &bad) == 0 && rec.count == 2);
    CHECK(rec.method[0] == 0x100 && rec.method[1] == 0x101);
    CHECK(rec.value[0] == 0xAAAA && rec.value[1] == 0xBBBB);
    /* Type 3: every word goes to the header method. */
    rec = (PushbufRec){0};
    words[0] = pushbuf_header(3, 2, 0, 0x200);
    words[1] = 0x1111;
    words[2] = 0x2222;
    CHECK(nvn_pushbuf_decode(words, 3, pushbuf_record, &rec, &bad) == 0 && rec.count == 2);
    CHECK(rec.method[0] == 0x200 && rec.method[1] == 0x200);
    /* Type 5: the first word goes to the header method, the rest to method + 1. */
    rec = (PushbufRec){0};
    words[0] = pushbuf_header(5, 3, 0, 0x45);
    words[1] = 1;
    words[2] = 2;
    words[3] = 3;
    CHECK(nvn_pushbuf_decode(words, 4, pushbuf_record, &rec, &bad) == 0 && rec.count == 3);
    CHECK(rec.method[0] == 0x45 && rec.method[1] == 0x46 && rec.method[2] == 0x46);
    CHECK(rec.value[0] == 1 && rec.value[1] == 2 && rec.value[2] == 3);
    /* Type 4: the count field is the value, and no data word follows. */
    rec = (PushbufRec){0};
    words[0] = pushbuf_header(4, 0x1234, 0, 0x300);
    CHECK(nvn_pushbuf_decode(words, 1, pushbuf_record, &rec, &bad) == 0 && rec.count == 1);
    CHECK(rec.method[0] == 0x300 && rec.value[0] == 0x1234);
    /* Type 0 words are skipped. */
    rec = (PushbufRec){0};
    words[0] = 0x00000500;
    words[1] = pushbuf_header(1, 1, 0, 0x400);
    words[2] = 0x4444;
    CHECK(nvn_pushbuf_decode(words, 3, pushbuf_record, &rec, &bad) == 0 && rec.count == 1);
    CHECK(rec.method[0] == 0x400 && rec.value[0] == 0x4444);
    /* A subchannel other than 0 stops the decode at its word. */
    rec = (PushbufRec){0};
    words[0] = pushbuf_header(1, 1, 0, 0x500);
    words[1] = 0x5555;
    words[2] = pushbuf_header(1, 1, 1, 0x600);
    words[3] = 0x6666;
    CHECK(nvn_pushbuf_decode(words, 4, pushbuf_record, &rec, &bad) == -1 && bad == 2);
    /* An unknown type, and a count that runs past the end. */
    words[0] = pushbuf_header(6, 1, 0, 0x700);
    CHECK(nvn_pushbuf_decode(words, 4, pushbuf_record, &rec, &bad) == -1 && bad == 0);
    words[0] = pushbuf_header(1, 3, 0, 0x700);
    CHECK(nvn_pushbuf_decode(words, 3, pushbuf_record, &rec, &bad) == -1 && bad == 0);
    words[0] = pushbuf_header(1, 2, 0, 0x700);
    CHECK(nvn_pushbuf_decode(words, 2, pushbuf_record, &rec, &bad) == -1 && bad == 0);
    /* A run of zero words uses its header only. */
    words[0] = pushbuf_header(1, 0, 0, 0x700);
    rec = (PushbufRec){0};
    CHECK(nvn_pushbuf_decode(words, 1, pushbuf_record, &rec, &bad) == 0 && rec.count == 0);
    CHECK(nvn_pushbuf_decode(words, 1, NULL, &rec, &bad) == -1);
    CHECK(nvn_pushbuf_decode(NULL, 1, pushbuf_record, &rec, &bad) == -1);
}

static int decode_program_words(const uint32_t* words, uint32_t count) {
    NvnWordStream s = {0};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t m, v;
        int last;
        if (nvn_word_stream_next(&s, words[i], &m, &v, &last) < 0) return 0;
    }
    return nvn_word_stream_complete(&s);
}

static void test_program_words_decode(void) {
    const NvnProgramDeviceIn dev = {
        .d1a38 = 0x087f6080u, .d1a68 = 0x20u, .d1a70 = 1u, .d1a74 = 0x20806080u, .d94 = 2u,
    };
    const NvnProgramStageIn vertex = {.code_offset = 0x100u, .cb_offset = 0x200u, .header_value = 0x30u};
    const NvnProgramStageIn fragment = {.code_offset = 0x300u, .cb_offset = 0x400u, .header_value = 0x20u};
    NvnProgramFragmentIn frag = {0};
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 0;
    CHECK(nvn_program_build_words(NULL, &vertex, NULL, NULL, &dev, 0x400000000ull, words, &count));
    CHECK(count == 8u && decode_program_words(words, count));
    /* A truncated array leaves the 0xE18 run of five words open. */
    CHECK(!decode_program_words(words, 2u));
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 25u && decode_program_words(words, count));
    CHECK(nvn_program_build_words(NULL, NULL, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 17u && decode_program_words(words, count));
    frag.g158 = 1;
    for (uint32_t i = 0; i < 4u; i++) frag.g1a0[i] = 0x11111111u * (i + 1u);
    CHECK(nvn_program_build_words(NULL, &vertex, &fragment, &frag, &dev, 0x400000000ull, words, &count));
    CHECK(count == 30u && decode_program_words(words, count));
}

static void test_program_disable(void) {
    static const NvnProgramMethod tcs[] = {{0x820u, 0x20u}, {0x821u, NVN_PROGRAM_DISABLE_TCS_OFFSET}, {0xD1Eu, 0u}};
    static const NvnProgramMethod tes[] = {{0x830u, 0x30u}};
    static const NvnProgramMethod gs[] = {{0x840u, 0x40u}, {0x47Cu, 0u}};
    static const NvnProgramMethod fs[] = {{0x850u, 0x50u}};
    NvnProgramMethod out[NVN_PROGRAM_DISABLE_MAX] = {0};
    /* The 16 combinations of bits 1 to 4, in the library order 3, 4, 2, 1. */
    for (uint32_t m = 0; m < 16u; m++) {
        uint32_t requested = m << 1;
        NvnProgramMethod expected[NVN_PROGRAM_DISABLE_MAX] = {0};
        uint32_t at = 0;
        if (requested & (1u << 3)) for (uint32_t i = 0; i < 3u; i++) expected[at++] = tcs[i];
        if (requested & (1u << 4)) expected[at++] = tes[0];
        if (requested & (1u << 2)) for (uint32_t i = 0; i < 2u; i++) expected[at++] = gs[i];
        if (requested & (1u << 1)) expected[at++] = fs[0];
        uint32_t n = nvn_program_disable_methods(requested, 0, out);
        CHECK(n == at);
        for (uint32_t i = 0; i < at; i++)
            CHECK(out[i].method == expected[i].method && out[i].value == expected[i].value);
        CHECK(nvn_program_disable_methods(requested, requested, out) == 0u);
    }
    /* Bits 0 and 5 have no disable words. */
    CHECK(nvn_program_disable_methods(1u << 0, 0, out) == 0u);
    CHECK(nvn_program_disable_methods(1u << 5, 0, out) == 0u);
    CHECK(nvn_program_disable_methods(0x3Fu, 0x3Fu, out) == 0u);
    /* Bit 3 in the program removes its three words; the others stay in order. */
    CHECK(nvn_program_disable_methods(0x1Eu, 1u << 3, out) == 4u);
    CHECK(out[0].method == 0x830u && out[1].method == 0x840u && out[2].method == 0x47Cu && out[3].method == 0x850u);
    CHECK(nvn_program_disable_methods(0x1Eu, 0, NULL) == 0u);
}

static void test_sampler_equal(void) {
    NvnSampler a, b;
    nvn_sampler_defaults(&a);
    b = a;
    CHECK(nvn_sampler_equal(&a, &b));
    b = a; b.device++; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.min_filter++; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.mag_filter++; CHECK(!nvn_sampler_equal(&a, &b));
    for (unsigned i = 0; i < 3; i++) {
        b = a; b.wrap[i]++;
        CHECK(!nvn_sampler_equal(&a, &b));
    }
    b = a; b.min_lod += 1.0f; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.max_lod += 1.0f; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.lod_bias += 1.0f; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.compare_enable++; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.compare++; CHECK(!nvn_sampler_equal(&a, &b));
    for (unsigned i = 0; i < 4; i++) {
        b = a; b.border[i]++;
        CHECK(!nvn_sampler_equal(&a, &b));
    }
    b = a; b.anisotropy += 1.0f; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.reduction++; CHECK(!nvn_sampler_equal(&a, &b));
    b = a; b.lod_snap += 1.0f; CHECK(!nvn_sampler_equal(&a, &b));
    NvnSampler nan;
    nvn_sampler_defaults(&nan);
    nan.lod_bias = NAN;
    CHECK(!nvn_sampler_equal(&nan, &nan));
}

static void check_bind_words(const uint32_t* got, uint32_t count, const uint32_t* want, uint32_t want_count) {
    CHECK(count == want_count);
    uint32_t n = count < want_count ? count : want_count;
    for (uint32_t i = 0; i < n; i++) CHECK(got[i] == want[i]);
}

static void bind_sentinel(uint32_t* out, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) out[i] = 0xDEADBEEFu;
}

static void check_bind_sentinel(const uint32_t* out, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) CHECK(out[i] == 0xDEADBEEFu);
}

static void test_bind_color(void) {
    static const uint32_t typical[] = {0x20020671u, 1u, 0x1505u, 0x800104BBu, 0x800404C5u};
    static const uint32_t logic8[] = {0x20010E10u, 0x09030000u, 0x800104BBu, 0x800904C5u};
    static const uint32_t alpha8[] = {0x20020671u, 1u, 0x1500u, 0x800004BBu};
    static const uint32_t both8[] = {0x20010E10u, 0x08030000u, 0x800004BBu};
    static const uint32_t edges[] = {0x20020671u, 1u, 0x15FFu, 0x800104BBu, 0x800F04C5u};
    uint32_t out[NVN_BIND_COLOR_WORDS_MAX];
    uint32_t count = nvn_bind_color_words(0x04050000u, out);
    check_bind_words(out, count, typical, 5);
    count = nvn_bind_color_words(0x09030000u, out);
    check_bind_words(out, count, logic8, 4);
    count = nvn_bind_color_words(0x08000000u, out);
    check_bind_words(out, count, alpha8, 4);
    count = nvn_bind_color_words(0x08030000u, out);
    check_bind_words(out, count, both8, 3);
    count = nvn_bind_color_words(0x0FFF0000u, out);
    check_bind_words(out, count, edges, 5);
    CHECK(nvn_bind_color_words(0x04050000u, NULL) == 0);
}

static void test_bind_channel_mask(void) {
    static const uint32_t all[] = {0xA0040E12u, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u};
    static const uint32_t target5[] = {0xA0040E12u, 0u, 0u, 0x11110000u, 0u};
    static const uint32_t setter[] = {0xA0040E12u, 0u, 0x101u, 0u, 0u};
    static const uint32_t zero[] = {0xA0040E12u, 0u, 0u, 0u, 0u};
    uint32_t out[NVN_BIND_CHANNEL_MASK_WORDS_MAX];
    uint32_t count = nvn_bind_channel_mask_words(0xFFFFFFFFu, out);
    check_bind_words(out, count, all, 5);
    count = nvn_bind_channel_mask_words(0x00F00000u, out);
    check_bind_words(out, count, target5, 5);
    uint32_t word = nvn_channel_mask_state_set_channel_mask(0, 5, 1, 1, 1, 1);
    CHECK(word == 0x00F00000u);
    count = nvn_bind_channel_mask_words(word, out);
    check_bind_words(out, count, target5, 5);
    word = nvn_channel_mask_state_set_channel_mask(0, 2, 1, 0, 1, 0);
    CHECK(word == 0x500u);
    count = nvn_bind_channel_mask_words(word, out);
    check_bind_words(out, count, setter, 5);
    count = nvn_bind_channel_mask_words(0, out);
    check_bind_words(out, count, zero, 5);
    CHECK(nvn_bind_channel_mask_words(0, NULL) == 0);
}

static void test_bind_depth_stencil(void) {
    static const uint32_t off[] = {0x800004B3u, 0x800004E0u};
    static const uint32_t depth_only[] = {0x800004BAu, 0x800104B3u, 0x800004C3u, 0x800004E0u};
    static const uint32_t stencil_only[] = {0x800004B3u, 0x20010E1Au, 0x11223344u};
    static const uint32_t both[] = {0x800104BAu, 0x800104B3u, 0x800504C3u, 0x20010E1Au, 0x11223344u};
    static const uint32_t edges[] = {0x800104BAu, 0x800104B3u, 0x800F04C3u, 0x20010E1Au, 0xAABBCCDDu};
    uint32_t out[NVN_BIND_DEPTH_STENCIL_WORDS_MAX];
    uint32_t count = nvn_bind_depth_stencil_words(0, 0xFFFFFFFFu, out);
    check_bind_words(out, count, off, 2);
    count = nvn_bind_depth_stencil_words(1, 0xFFFFFFFFu, out);
    check_bind_words(out, count, depth_only, 4);
    count = nvn_bind_depth_stencil_words(4, 0x11223344u, out);
    check_bind_words(out, count, stencil_only, 3);
    count = nvn_bind_depth_stencil_words(0x57u, 0x11223344u, out);
    check_bind_words(out, count, both, 5);
    count = nvn_bind_depth_stencil_words(0xFFFFFFFFu, 0xAABBCCDDu, out);
    check_bind_words(out, count, edges, 5);
    CHECK(nvn_bind_depth_stencil_words(0, 0, NULL) == 0);
}

static void test_bind_polygon(void) {
    static const uint32_t solid[] = {0x89000647u, 0x80000646u, 0x9B00036Bu, 0x9B00036Cu, 0x80000370u};
    static const uint32_t cull1[] = {0x89000647u, 0x80010646u, 0x84040648u, 0x9B00036Bu, 0x9B00036Cu,
                                     0x80000370u};
    static const uint32_t pm1[] = {0x89010647u, 0x80010646u, 0x84050648u, 0x9B01036Bu, 0x9B01036Cu, 0x80010371u};
    static const uint32_t pm2[] = {0x89000647u, 0x80010646u, 0x84080648u, 0x9B02036Bu, 0x9B02036Cu, 0x80010372u};
    uint32_t out[NVN_BIND_POLYGON_WORDS_MAX];
    uint32_t count = nvn_bind_polygon_words(0, out);
    check_bind_words(out, count, solid, 5);
    count = nvn_bind_polygon_words(1, out);
    check_bind_words(out, count, cull1, 6);
    count = nvn_bind_polygon_words(0x1000Eu, out);
    check_bind_words(out, count, pm1, 6);
    count = nvn_bind_polygon_words(0x20013u, out);
    check_bind_words(out, count, pm2, 6);
    bind_sentinel(out, NVN_BIND_POLYGON_WORDS_MAX);
    CHECK(nvn_bind_polygon_words(0x18u, out) == 0);
    check_bind_sentinel(out, NVN_BIND_POLYGON_WORDS_MAX);
    CHECK(nvn_bind_polygon_words(0, NULL) == 0);
}

static void test_bind_blend(void) {
    static const uint32_t typical[] = {0x80060D35u, 0x80000374u, 0x800104B9u, 0x20060789u,
                                       3u, 0x12u, 0x4014u, 5u, 0x8016u, 0xC018u};
    static const uint32_t edges[] = {0x800F0D35u, 0x80000374u, 0x800104B9u, 0x200607B9u,
                                     7u, 0xC01Fu, 0xC01Fu, 7u, 0xC01Fu, 0xC01Fu};
    static const uint32_t zero[] = {0x80000D35u, 0x80000374u, 0x800104B9u, 0x20060781u,
                                    0u, 0u, 0u, 0u, 0u, 0u};
    uint32_t out[NVN_BIND_BLEND_WORDS_MAX];
    uint32_t func[4], rgb = 0, alpha = 0;
    uint32_t count = nvn_bind_blend_words(0x53050001u, 0x78563412u, out);
    check_bind_words(out, count, typical, 10);
    CHECK(nvn_blend_state_get_blend_target(0x53050001u) == 1u);
    nvn_blend_state_get_blend_equation(0x53050001u, &rgb, &alpha);
    CHECK(rgb == 3u && alpha == 5u);
    nvn_blend_state_get_blend_func(0x78563412u, func);
    CHECK(func[0] == 0x12u && func[1] == 0x34u && func[2] == 0x56u && func[3] == 0x78u);
    CHECK(((typical[5] & 0x1Fu) | (((typical[5] >> 14) & 3u) << 5)) == func[0]);
    CHECK(((typical[6] & 0x1Fu) | (((typical[6] >> 14) & 3u) << 5)) == func[1]);
    CHECK(((typical[8] & 0x1Fu) | (((typical[8] >> 14) & 3u) << 5)) == func[2]);
    CHECK(((typical[9] & 0x1Fu) | (((typical[9] >> 14) & 3u) << 5)) == func[3]);
    count = nvn_bind_blend_words(0x770F0007u, 0x7F7F7F7Fu, out);
    check_bind_words(out, count, edges, 10);
    count = nvn_bind_blend_words(0, 0, out);
    check_bind_words(out, count, zero, 10);
    bind_sentinel(out, NVN_BIND_BLEND_WORDS_MAX);
    CHECK(nvn_bind_blend_words(0x2A00u, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_BLEND_WORDS_MAX);
    CHECK(nvn_bind_blend_words(0, 0, NULL) == 0);
}

static void test_bind_vertex_attrib(void) {
    static const uint32_t defaults[] = {
        0x20100458u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u,
    };
    static const uint32_t one_default[] = {
        0x20100458u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u,
    };
    static const uint32_t one_format[] = {
        0x20100458u, 0x38400600u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u,
    };
    static const uint32_t two[] = {
        0x20100458u, 0x3A400040u, 0x38400600u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u, 0x3A400040u,
        0x3A400040u, 0x3A400040u, 0x3A400040u,
    };
    static const uint32_t sixteen[] = {
        0x20100458u, 0x30000000u, 0x30000001u, 0x30000002u, 0x30000003u, 0x30000004u, 0x30000005u,
        0x30000006u, 0x30000007u, 0x30000008u, 0x30000009u, 0x3000000Au, 0x3000000Bu, 0x3000000Cu,
        0x3000000Du, 0x3000000Eu, 0x3000000Fu,
    };
    uint32_t s[16], fmt[16], out[NVN_BIND_VERTEX_ATTRIB_WORDS_MAX];
    CHECK(nvn_vertex_attrib_format_index(0x04400640u) == 0x22u);
    CHECK(nvn_vertex_attrib_format_index(0) == 0u);
    CHECK(nvn_vertex_attrib_format_index(0x7FE00000u) == 0x3FFu);
    CHECK(nvn_vertex_attrib_format_index(0x84400640u) == 0x22u);
    uint32_t count = nvn_bind_vertex_attrib_words(0, NULL, NULL, out);
    check_bind_words(out, count, defaults, 17);
    s[0] = 0x001FFF1Fu;
    fmt[0] = 0x11111111u;
    count = nvn_bind_vertex_attrib_words(1, s, fmt, out);
    check_bind_words(out, count, one_default, 17);
    s[0] = 0x04400640u;
    fmt[0] = 0x38400000u;
    count = nvn_bind_vertex_attrib_words(1, s, fmt, out);
    check_bind_words(out, count, one_format, 17);
    s[1] = 0x04400640u;
    s[0] = 0x001FFF1Fu;
    fmt[1] = 0x38400000u;
    count = nvn_bind_vertex_attrib_words(2, s, fmt, out);
    check_bind_words(out, count, two, 17);
    for (uint32_t i = 0; i < 16; i++) {
        s[i] = 0x40u;
        fmt[i] = 0x30000000u + i;
    }
    count = nvn_bind_vertex_attrib_words(16, s, fmt, out);
    check_bind_words(out, count, sixteen, 17);
    bind_sentinel(out, NVN_BIND_VERTEX_ATTRIB_WORDS_MAX);
    CHECK(nvn_bind_vertex_attrib_words(17, s, fmt, out) == 0);
    check_bind_sentinel(out, NVN_BIND_VERTEX_ATTRIB_WORDS_MAX);
    CHECK(nvn_bind_vertex_attrib_words(1, NULL, fmt, out) == 0);
    CHECK(nvn_bind_vertex_attrib_words(0, NULL, NULL, NULL) == 0);
}

static void test_bind_vertex_stream(void) {
    static const uint32_t fill[] = {
        0x80000700u, 0x80000704u, 0x80000708u, 0x8000070Cu, 0x80000710u, 0x80000714u, 0x80000718u,
        0x8000071Cu, 0x80000720u, 0x80000724u, 0x80000728u, 0x8000072Cu, 0x80000730u, 0x80000734u,
        0x80000738u, 0x8000073Cu,
    };
    static const uint32_t one_zero[] = {
        0x900C0700u, 0x80000620u, 0x80000704u, 0x80000708u, 0x8000070Cu, 0x80000710u, 0x80000714u,
        0x80000718u, 0x8000071Cu, 0x80000720u, 0x80000724u, 0x80000728u, 0x8000072Cu, 0x80000730u,
        0x80000734u, 0x80000738u, 0x8000073Cu,
    };
    static const uint32_t one_div[] = {
        0x900C0700u, 0x80010620u, 0x20010703u, 2u, 0x80000704u, 0x80000708u, 0x8000070Cu, 0x80000710u,
        0x80000714u, 0x80000718u, 0x8000071Cu, 0x80000720u, 0x80000724u, 0x80000728u, 0x8000072Cu,
        0x80000730u, 0x80000734u, 0x80000738u, 0x8000073Cu,
    };
    static const uint32_t two[] = {
        0x9FFF0700u, 0x80010620u, 0x20010703u, 1u, 0x90000704u, 0x80000621u, 0x80000708u, 0x8000070Cu,
        0x80000710u, 0x80000714u, 0x80000718u, 0x8000071Cu, 0x80000720u, 0x80000724u, 0x80000728u,
        0x8000072Cu, 0x80000730u, 0x80000734u, 0x80000738u, 0x8000073Cu,
    };
    static const uint32_t full[] = {
        0x90040700u, 0x80000620u, 0x90040704u, 0x80000621u, 0x90040708u, 0x80000622u, 0x9004070Cu,
        0x80000623u, 0x90040710u, 0x80000624u, 0x90040714u, 0x80000625u, 0x90040718u, 0x80000626u,
        0x9004071Cu, 0x80000627u, 0x90040720u, 0x80000628u, 0x90040724u, 0x80000629u, 0x90040728u,
        0x8000062Au, 0x9004072Cu, 0x8000062Bu, 0x90040730u, 0x8000062Cu, 0x90040734u, 0x8000062Du,
        0x90040738u, 0x8000062Eu, 0x9004073Cu, 0x8000062Fu,
    };
    uint32_t stride[16], divisor[16], out[NVN_BIND_VERTEX_STREAM_WORDS_MAX];
    uint32_t count = nvn_bind_vertex_stream_words(0, NULL, NULL, out);
    check_bind_words(out, count, fill, 16);
    stride[0] = 0xCu;
    divisor[0] = 0;
    count = nvn_bind_vertex_stream_words(1, stride, divisor, out);
    check_bind_words(out, count, one_zero, 17);
    divisor[0] = 2;
    count = nvn_bind_vertex_stream_words(1, stride, divisor, out);
    check_bind_words(out, count, one_div, 19);
    stride[0] = 0xFFFu;
    divisor[0] = 1;
    stride[1] = 0x1000u;
    divisor[1] = 0;
    count = nvn_bind_vertex_stream_words(2, stride, divisor, out);
    check_bind_words(out, count, two, 20);
    for (uint32_t i = 0; i < 16; i++) {
        stride[i] = 4u;
        divisor[i] = 0;
    }
    count = nvn_bind_vertex_stream_words(16, stride, divisor, out);
    check_bind_words(out, count, full, 32);
    stride[0] = 0;
    divisor[0] = 0xFFFFFFFFu;
    count = nvn_bind_vertex_stream_words(1, stride, divisor, out);
    CHECK(count == 19u && out[0] == 0x90000700u && out[1] == 0x80010620u && out[2] == 0x20010703u);
    CHECK(out[3] == 0xFFFFFFFFu);
    bind_sentinel(out, NVN_BIND_VERTEX_STREAM_WORDS_MAX);
    CHECK(nvn_bind_vertex_stream_words(17, stride, divisor, out) == 0);
    check_bind_sentinel(out, NVN_BIND_VERTEX_STREAM_WORDS_MAX);
    CHECK(nvn_bind_vertex_stream_words(0, NULL, NULL, NULL) == 0);
}

static void test_bind_vertex_buffer(void) {
    static const uint32_t normal[] = {0x20020701u, 0x12u, 0x3456789Au, 0x200207C0u, 0x12u, 0x34567999u};
    static const uint32_t disabled[] = {0x2002073Du, 0u, 0x1000u, 0x200207DEu, 0u, 0xFFFu};
    static const uint32_t sized[] = {0x2002070Du, 4u, 0u, 0x200207C6u, 4u, 0u};
    uint32_t out[NVN_BIND_VERTEX_BUFFER_WORDS_MAX];
    uint32_t count = nvn_bind_vertex_buffer_words(0, 0x123456789Aull, 0x100u, out);
    check_bind_words(out, count, normal, 6);
    count = nvn_bind_vertex_buffer_words(15, UINT64_MAX, 0, out);
    check_bind_words(out, count, disabled, 6);
    count = nvn_bind_vertex_buffer_words(3, 0x400000000ull, 1, out);
    check_bind_words(out, count, sized, 6);
    bind_sentinel(out, NVN_BIND_VERTEX_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_vertex_buffer_words(0, UINT64_MAX, 1, out) == 0);
    check_bind_sentinel(out, NVN_BIND_VERTEX_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_vertex_buffer_words(0, 0xFFFFFFFFFFFFFF00ull, 0x100u, out) == 0);
    check_bind_sentinel(out, NVN_BIND_VERTEX_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_vertex_buffer_words(16, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_VERTEX_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_vertex_buffer_words(0, 0, 0, NULL) == 0);
}

static void test_bind_uniform_buffer(void) {
    static const uint32_t zero_size[] = {0x80300904u};
    static const uint32_t sized[] = {0x200308E0u, 0x1000u, 0x12u, 0x3456789Au, 0x80510924u};
    static const uint32_t slot_max[] = {0x80200914u};
    static const uint32_t rounded[] = {0x200308E0u, 0x200u, 0u, 0u, 0x80310904u};
    static const uint32_t stage2[] = {0x8030091Cu};
    static const uint32_t stage3[] = {0x8030090Cu};
    static const uint32_t size_max[] = {0x200308E0u, 0xFFFFFF00u, 0u, 0u, 0x80310904u};
    uint32_t out[NVN_BIND_UNIFORM_BUFFER_WORDS_MAX];
    uint32_t count = nvn_bind_uniform_buffer_words(0, 0, 0, 0, out);
    check_bind_words(out, count, zero_size, 1);
    count = nvn_bind_uniform_buffer_words(1, 2, 0x123456789Aull, 0x1000u, out);
    check_bind_words(out, count, sized, 5);
    count = nvn_bind_uniform_buffer_words(4, 31, 0, 0, out);
    check_bind_words(out, count, slot_max, 1);
    count = nvn_bind_uniform_buffer_words(0, 0, 0, 0x101u, out);
    check_bind_words(out, count, rounded, 5);
    count = nvn_bind_uniform_buffer_words(2, 0, 0, 0, out);
    check_bind_words(out, count, stage2, 1);
    count = nvn_bind_uniform_buffer_words(3, 0, 0, 0, out);
    check_bind_words(out, count, stage3, 1);
    count = nvn_bind_uniform_buffer_words(0, 0, 0, 0xFFFFFF00u, out);
    check_bind_words(out, count, size_max, 5);
    bind_sentinel(out, NVN_BIND_UNIFORM_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_uniform_buffer_words(5, 0, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_UNIFORM_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_uniform_buffer_words(0, 32, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_UNIFORM_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_uniform_buffer_words(0, 0, 0, 0xFFFFFFFFu, out) == 0);
    check_bind_sentinel(out, NVN_BIND_UNIFORM_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_uniform_buffer_words(0, 0, 0, 0xFFFFFF01u, out) == 0);
    check_bind_sentinel(out, NVN_BIND_UNIFORM_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_uniform_buffer_words(0, 0, 0, 0, NULL) == 0);
}

static void test_bind_storage_buffer(void) {
    static const uint32_t vertex[] = {0x80440E20u, 0x200308E4u, 0x3456789Au, 0x12u, 0x200u};
    static const uint32_t slot_max[] = {0x83400E20u, 0x200308E4u, 0u, 0u, 0u};
    static const uint32_t geometry[] = {0x81040E20u, 0x200308E4u, 0u, 1u, 1u};
    static const uint32_t tess_control[] = {0x80840E20u, 0x200308E4u, 0u, 0u, 0u};
    static const uint32_t tess_eval[] = {0x80C40E20u, 0x200308E4u, 0u, 0u, 0u};
    uint32_t out[NVN_BIND_STORAGE_BUFFER_WORDS_MAX];
    uint32_t count = nvn_bind_storage_buffer_words(0, 0, 0x123456789Aull, 0x200u, out);
    check_bind_words(out, count, vertex, 5);
    count = nvn_bind_storage_buffer_words(1, 127, 0, 0, out);
    check_bind_words(out, count, slot_max, 5);
    count = nvn_bind_storage_buffer_words(2, 0, 0x100000000ull, 1, out);
    check_bind_words(out, count, geometry, 5);
    count = nvn_bind_storage_buffer_words(3, 0, 0, 0, out);
    check_bind_words(out, count, tess_control, 5);
    count = nvn_bind_storage_buffer_words(4, 0, 0, 0, out);
    check_bind_words(out, count, tess_eval, 5);
    bind_sentinel(out, NVN_BIND_STORAGE_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_storage_buffer_words(5, 0, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_STORAGE_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_storage_buffer_words(0, 128, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_STORAGE_BUFFER_WORDS_MAX);
    CHECK(nvn_bind_storage_buffer_words(0, 0, 0, 0, NULL) == 0);
}

static void test_bind_texture(void) {
    static const uint32_t plain[] = {0x80040E0E, 0x200208E3, 0x20, 0x12345678};
    static const uint32_t descriptor[] = {0x80040E0E, 0x8000068B, 0x200208E3, 0x20, 0x12345678};
    static const uint32_t slot_max[] = {0x80020E0E, 0x200208E3, 0x418, 0x12345678};
    static const uint32_t stage0[] = {0x80000E0E, 0x200208E3, 0x20, 0};
    static const uint32_t stage2[] = {0x80030E0E, 0x200208E3, 0x20, 0};
    static const uint32_t stage3[] = {0x80010E0E, 0x200208E3, 0x20, 0};
    uint32_t out[NVN_BIND_TEXTURE_WORDS_MAX];
    uint32_t count = nvn_bind_texture_words(1, 0, 0x12345678ull, 0, out);
    check_bind_words(out, count, plain, 4);
    count = nvn_bind_texture_words(1, 0, 0x12345678ull, 1, out);
    check_bind_words(out, count, descriptor, 5);
    count = nvn_bind_texture_words(4, 127, 0xFFFFFFFF12345678ull, 0, out);
    check_bind_words(out, count, slot_max, 4);
    count = nvn_bind_texture_words(0, 0, 0, 0, out);
    check_bind_words(out, count, stage0, 4);
    count = nvn_bind_texture_words(2, 0, 0, 0, out);
    check_bind_words(out, count, stage2, 4);
    count = nvn_bind_texture_words(3, 0, 0, 0, out);
    check_bind_words(out, count, stage3, 4);
    bind_sentinel(out, NVN_BIND_TEXTURE_WORDS_MAX);
    CHECK(nvn_bind_texture_words(5, 0, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_TEXTURE_WORDS_MAX);
    CHECK(nvn_bind_texture_words(0, 128, 0, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_TEXTURE_WORDS_MAX);
    CHECK(nvn_bind_texture_words(0, 0, 0, 0, NULL) == 0);
}

static void test_draw_words(void) {
    const uint64_t address = 0x123456789Aull;
    static const uint32_t arrays[] = {0xA0030E32u, 4u, 7u, 0x123u};
    static const uint32_t arrays_instanced[] = {0xA0050E36u, 4u, 0x123u, 3u, 7u, 0u};
    static const uint32_t arrays_instanced_constants[] = {0x81900E20u, 0xA0050E44u, 4u, 0x123u, 3u, 7u, 0x22u};
    static const uint32_t elements[] = {0xA0050E38u, 4u, 0x12u, 0x3456789Au, 2u, 0x123u};
    static const uint32_t elements_base[] = {0x20010446u, 0u, 0x2001050Du, 0u, 0xA0050E38u, 4u, 0x12u, 0x3456789Au,
                                             2u, 0x123u, 0x8000050Du, 0x80000446u};
    static const uint32_t elements_base_constants[] = {
        0x81900E20u, 0x200108E4u, 0x11u, 0x20010446u, 0x11u, 0x2001050Du, 0x11u, 0xA0050E38u, 4u,
        0x12u, 0x3456789Au, 2u, 0x123u, 0x8000050Du, 0x80000446u, 0x200208E3u, 0x640u, 0u};
    static const uint32_t elements_instanced[] = {0x800205F6u, 0x200205F2u, 0x12u, 0x3456789Au, 0xA0060E3Cu,
                                                  4u, 0x123u, 3u, 0u, 0u, 0u};
    static const uint32_t elements_instanced_constants[] = {0x81900E20u, 0x800205F6u, 0x200205F2u, 0x12u,
                                                            0x3456789Au, 0xA0060E46u, 4u, 0x123u, 3u, 0u,
                                                            0x11u, 0x22u};
    uint32_t out[NVN_DRAW_WORDS_MAX];
    int constants;
    uint32_t count;

    count = nvn_draw_words(NVN_DRAW_ARRAYS, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants);
    check_bind_words(out, count, arrays, 4);
    CHECK(constants == 0);
    count = nvn_draw_words(NVN_DRAW_ARRAYS_INSTANCED, 4u, 2u, 0x123u, address, 7u, 0x11u, 0u, 3u, out, &constants);
    check_bind_words(out, count, arrays_instanced, 6);
    CHECK(constants == 0);
    count = nvn_draw_words(NVN_DRAW_ARRAYS_INSTANCED, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants);
    check_bind_words(out, count, arrays_instanced_constants, 7);
    CHECK(constants == 1);
    count = nvn_draw_words(NVN_DRAW_ELEMENTS, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants);
    check_bind_words(out, count, elements, 6);
    CHECK(constants == 0);
    count =
        nvn_draw_words(NVN_DRAW_ELEMENTS_BASE_VERTEX, 4u, 2u, 0x123u, address, 7u, 0u, 0x22u, 3u, out, &constants);
    check_bind_words(out, count, elements_base, 12);
    CHECK(constants == 0);
    count = nvn_draw_words(NVN_DRAW_ELEMENTS_BASE_VERTEX, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out,
                           &constants);
    check_bind_words(out, count, elements_base_constants, NVN_DRAW_WORDS_MAX);
    CHECK(count == NVN_DRAW_WORDS_MAX);
    CHECK(constants == 1);
    count = nvn_draw_words(NVN_DRAW_ELEMENTS_INSTANCED, 4u, 2u, 0x123u, address, 7u, 0u, 0u, 3u, out, &constants);
    check_bind_words(out, count, elements_instanced, 11);
    CHECK(constants == 0);
    count = nvn_draw_words(NVN_DRAW_ELEMENTS_INSTANCED, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants);
    check_bind_words(out, count, elements_instanced_constants, 12);
    CHECK(constants == 1);
    bind_sentinel(out, NVN_DRAW_WORDS_MAX);
    CHECK(nvn_draw_words(NVN_DRAW_KIND_COUNT, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants) == 0);
    check_bind_sentinel(out, NVN_DRAW_WORDS_MAX);
    CHECK(nvn_draw_words(NVN_DRAW_ELEMENTS, 4u, 0x2000u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, out, &constants) == 0);
    check_bind_sentinel(out, NVN_DRAW_WORDS_MAX);
    CHECK(nvn_draw_words(NVN_DRAW_ARRAYS, 4u, 2u, 0x123u, address, 7u, 0x11u, 0x22u, 3u, NULL, &constants) == 0);
}

static void test_report_counter_words(void) {
    const uint64_t address = 0x123456789Aull;
    /* Type 0: table row 0 (query 0x0000F002). */
    static const uint32_t type0[] = {0x200406C0u, 0x12u, 0x3456789Au, 0u, 0x0000F002u, 0x800003E0u};
    /* Type 14: table row 14 (query 0x05805002). */
    static const uint32_t type14[] = {0x200406C0u, 0x12u, 0x3456789Au, 0u, 0x05805002u, 0x800003E0u};
    /* Type 15: four packets at address, address+4, address+8 and address+0xC. */
    static const uint32_t type15[] = {
        0x200406C0u, 0x12u, 0x3456789Au, 0u, 0x15007002u, 0x200406C0u, 0x12u,
        0x3456789Eu, 0u, 0x16007002u, 0x200406C0u, 0x12u, 0x345678A2u, 0u,
        0x17007002u, 0x200406C0u, 0x12u, 0x345678A6u, 0u, 0x18007002u, 0x800003E0u,
    };
    /* Type 16: the timestamp packet at 0x004 with query 0x00100002. */
    static const uint32_t type16[] = {0x20040004u, 0x12u, 0x3456789Au, 0u, 0x00100002u, 0x800003E0u};
    uint32_t out[NVN_REPORT_WORDS_MAX];
    uint32_t count = nvn_report_counter_words(0, address, out);
    check_bind_words(out, count, type0, 6);
    count = nvn_report_counter_words(14, address, out);
    check_bind_words(out, count, type14, 6);
    count = nvn_report_counter_words(15, address, out);
    check_bind_words(out, count, type15, NVN_REPORT_WORDS_MAX);
    CHECK(count == NVN_REPORT_WORDS_MAX);
    count = nvn_report_counter_words(16, address, out);
    check_bind_words(out, count, type16, 6);
    bind_sentinel(out, NVN_REPORT_WORDS_MAX);
    CHECK(nvn_report_counter_words(17, address, out) == 0);
    check_bind_sentinel(out, NVN_REPORT_WORDS_MAX);
    CHECK(nvn_report_counter_words(0, address, NULL) == 0);
}

static void test_bind_image(void) {
    const uint64_t handles[2] = {0x123456789Aull, 0xFEDCBA9876543210ull};
    /* Stage 0 has table value 0; one handle at slot 5 is 0x120 + 5*8. */
    static const uint32_t stage0[] = {0x80000E0Eu, 0x200308E3u, 0x148u, 0x3456789Au, 0x12u};
    /* Stage 4 has table value 2. */
    static const uint32_t stage4[] = {0x80020E0Eu, 0x200308E3u, 0x148u, 0x3456789Au, 0x12u};
    /* The descriptor flag inserts the 0x68B word after the stage word. */
    static const uint32_t descriptor[] = {0x80000E0Eu, 0x8000068Bu, 0x200308E3u, 0x148u, 0x3456789Au, 0x12u};
    /* Two handles: the second packet uses slot first+1 (0x150) and its own handle words. */
    static const uint32_t two[] = {
        0x80000E0Eu, 0x200308E3u, 0x148u, 0x3456789Au, 0x12u,
        0x80000E0Eu, 0x200308E3u, 0x150u, 0x76543210u, 0xFEDCBA98u,
    };
    uint32_t out[NVN_BIND_IMAGE_WORDS_MAX];
    uint32_t count = nvn_bind_image_words(0, 5, 1, handles, 0, out);
    check_bind_words(out, count, stage0, 5);
    count = nvn_bind_image_words(4, 5, 1, handles, 0, out);
    check_bind_words(out, count, stage4, 5);
    count = nvn_bind_image_words(0, 5, 1, handles, 1, out);
    check_bind_words(out, count, descriptor, 6);
    count = nvn_bind_image_words(0, 5, 2, handles, 0, out);
    check_bind_words(out, count, two, 10);
    bind_sentinel(out, NVN_BIND_IMAGE_WORDS_MAX);
    CHECK(nvn_bind_image_words(5, 5, 1, handles, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_IMAGE_WORDS_MAX);
    CHECK(nvn_bind_image_words(0, 5, 0, handles, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_IMAGE_WORDS_MAX);
    CHECK(nvn_bind_image_words(0, 5, 17, handles, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_IMAGE_WORDS_MAX);
    CHECK(nvn_bind_image_words(0, 5, 1, NULL, 0, out) == 0);
    check_bind_sentinel(out, NVN_BIND_IMAGE_WORDS_MAX);
    CHECK(nvn_bind_image_words(0, 5, 1, handles, 0, NULL) == 0);
}

static void test_clear_depth_stencil_words(void) {
    /* Depth only: rows 1, 3 and 11, no stencil branch. */
    static const uint32_t depth_zero[] = {0x2001036Fu, 0x10001u, 0x20010364u, 0x00000000u, 0x80010E04u};
    static const uint32_t depth_quarter[] = {0x2001036Fu, 0x1u, 0x20010364u, 0x3E800000u, 0x80010E04u};
    static const uint32_t depth_half[] = {0x2001036Fu, 0x0u, 0x20010364u, 0x3F000000u, 0x80010E04u};
    static const uint32_t depth_one[] = {0x2001036Fu, 0x10000u, 0x20010364u, 0x3F800000u, 0x80010E04u};
    /* No zcull word: rows 3 and 11. */
    static const uint32_t no_zcull[] = {0x20010364u, 0x3F000000u, 0x80010E04u};
    /* The scratch macro follows the zcull word when it is on. */
    static const uint32_t scratch[] = {0x2001036Fu, 0x0u, 0x80000E26u, 0x20010364u, 0x3F000000u, 0x80010E04u};
    /* Stencil only: rows 4 to 10, with macro value 2. */
    static const uint32_t stencil_only[] = {0x80020049u, 0x800104E7u, 0x807F0368u, 0x80020E04u,
                                            0x80030049u, 0x800004E7u, 0x80010049u};
    /* Depth and stencil: rows 1, 3 and 4 to 10, with macro value 3. */
    static const uint32_t both[] = {0x2001036Fu,         0x0u,         0x20010364u, 0x3F000000u,
                                    0x80020049u,         0x800104E7u,   0x807F0368u, 0x80030E04u,
                                    0x80030049u,         0x800004E7u,   0x80010049u};
    uint32_t out[NVN_CLEAR_DEPTH_STENCIL_WORDS_MAX];
    uint32_t count;

    CHECK(nvn_clear_depth_stencil_nothing(0, 0));
    CHECK(!nvn_clear_depth_stencil_nothing(1, 0));
    CHECK(!nvn_clear_depth_stencil_nothing(0, 1));
    bind_sentinel(out, NVN_CLEAR_DEPTH_STENCIL_WORDS_MAX);
    CHECK(nvn_clear_depth_stencil_words(0x3F000000u, 0, 0, 0, 1, 0, out) == 0);
    check_bind_sentinel(out, NVN_CLEAR_DEPTH_STENCIL_WORDS_MAX);

    count = nvn_clear_depth_stencil_words(0x00000000u, 1, 0, 0, 1, 0, out);
    check_bind_words(out, count, depth_zero, 5);
    count = nvn_clear_depth_stencil_words(0x3E800000u, 1, 0, 0, 1, 0, out);
    check_bind_words(out, count, depth_quarter, 5);
    count = nvn_clear_depth_stencil_words(0x3F000000u, 1, 0, 0, 1, 0, out);
    check_bind_words(out, count, depth_half, 5);
    count = nvn_clear_depth_stencil_words(0x3F800000u, 1, 0, 0, 1, 0, out);
    check_bind_words(out, count, depth_one, 5);
    count = nvn_clear_depth_stencil_words(0x3F000000u, 1, 0, 0, 0, 0, out);
    check_bind_words(out, count, no_zcull, 3);
    count = nvn_clear_depth_stencil_words(0x3F000000u, 1, 0, 0, 1, 1, out);
    check_bind_words(out, count, scratch, 6);
    count = nvn_clear_depth_stencil_words(0x3F000000u, 0, 0x7F, 1, 1, 0, out);
    check_bind_words(out, count, stencil_only, 7);
    count = nvn_clear_depth_stencil_words(0x3F000000u, 1, 0x7F, 1, 1, 0, out);
    check_bind_words(out, count, both, 11);
    CHECK(nvn_clear_depth_stencil_words(0x3F000000u, 1, 0, 0, 1, 0, NULL) == 0);
}

static void test_highest_enabled_index(void) {
    const uint8_t none[4] = {0, 0, 0, 0};
    const uint8_t first[4] = {1, 0, 0, 0};
    const uint8_t last[4] = {0, 0, 0, 1};
    const uint8_t mixed[4] = {0, 1, 0, 1};
    CHECK(nvn_highest_enabled_index(none, 4) == 0);
    CHECK(nvn_highest_enabled_index(first, 4) == 1);
    CHECK(nvn_highest_enabled_index(last, 4) == 4);
    CHECK(nvn_highest_enabled_index(mixed, 4) == 4);
    CHECK(nvn_highest_enabled_index(last, 0) == 0);
    CHECK(nvn_highest_enabled_index(NULL, 4) == 0);
}

static void test_compute_qmd_build(void) {
    const NvnProgramComputeIn prog = {
        .code_offset = 0x2000u, .cb_offset = 0x1000u, .h6fc = 0x40u,
        .h718 = 8u, .h71c = 4u, .h720 = 2u, .h724 = 0x1001u,
        .h728 = 0x12345u, .h72c = 0x23456u, .h730 = 0x34567u, .h734 = 0x1Fu,
    };
    NvnComputeBinds binds = {0};
    binds.cbuf0_address = 0x400002000ull;
    binds.uniform[1] = (NvnComputeBindEntry){0x400003000ull, 0x200u, 1};
    uint32_t qmd[NVN_COMPUTE_QMD_WORDS];
    /* x, y, z = 3, 2, 1; block 8, 4, 2. */
    CHECK(nvn_compute_qmd_build(&prog, 3, 2, 1, &binds, qmd));
    CHECK(qmd[0x8] == 0x2000u);
    CHECK(qmd[0xC] == 3u);
    CHECK(qmd[0xD] == 0x00010002u);
    CHECK(qmd[0x11] == 0x1100u); /* ((0x1001 + 0xFF) >> 8) << 8 */
    CHECK(qmd[0x12] == 0x00080000u);
    CHECK(qmd[0x13] == 0x00020004u);
    CHECK(qmd[0x14] == 0x13u); /* cbuf 0, cbuf 1 and uniform slot 1 */
    CHECK(qmd[0x1D] == 0x2000u && qmd[0x1E] == 0x02800004u);
    CHECK(qmd[0x1F] == 0x1000u && qmd[0x20] == 0x009F8004u);
    CHECK(qmd[0x25] == 0x3000u && qmd[0x26] == 0x01000004u);
    CHECK(qmd[0x2D] == 0xF8012345u);
    CHECK(qmd[0x2E] == 0x00023456u);
    CHECK(qmd[0x2F] == 0x00034567u);
    CHECK(qmd[0] == 0u && qmd[NVN_COMPUTE_QMD_WORDS - 1] == 0u);
    /* Refusals: a grid field above its bits. */
    CHECK(!nvn_compute_qmd_build(&prog, 3, 0x10000u, 1, &binds, qmd));
    CHECK(!nvn_compute_qmd_build(&prog, 0x80000000u, 2, 1, &binds, qmd));
    NvnComputeBinds high = binds;
    high.uniform[5] = (NvnComputeBindEntry){0x400004000ull, 0x100u, 1};
    uint32_t detail = 99;
    CHECK(nvn_compute_qmd_check(&prog, 3, 2, 1, &binds, &detail) == NVN_QMD_OK && detail == 0);
    CHECK(nvn_compute_qmd_check(&prog, 0x80000000u, 2, 1, &binds, &detail) == NVN_QMD_GRID_X);
    CHECK(nvn_compute_qmd_check(&prog, 3, 0x10000u, 1, &binds, &detail) == NVN_QMD_GRID_Y);
    CHECK(nvn_compute_qmd_check(&prog, 3, 2, 0x10000u, &binds, &detail) == NVN_QMD_GRID_Z);
    /* Slot 5 and above only reach the driver block: no refusal, no enable bit. */
    CHECK(nvn_compute_qmd_check(&prog, 3, 2, 1, &high, &detail) == NVN_QMD_OK);
    CHECK(nvn_compute_qmd_build(&prog, 3, 2, 1, &high, qmd) && qmd[0x14] == 0x13u);
    /* A small bound slot 0 to 4 is enabled with its size rounded up to 0x100. */
    NvnComputeBinds small = binds;
    small.uniform[0] = (NvnComputeBindEntry){0x400007000ull, 0x20u, 1};
    CHECK(nvn_compute_qmd_build(&prog, 3, 2, 1, &small, qmd));
    CHECK(qmd[0x14] == 0x1Bu && qmd[0x23] == 0x7000u && qmd[0x24] == 0x00800004u);
    NvnComputeBinds huge = binds;
    huge.uniform[9] = (NvnComputeBindEntry){0x400008000ull, 0x100000000ull, 1};
    CHECK(nvn_compute_qmd_check(&prog, 3, 2, 1, &huge, &detail) == NVN_QMD_UNIFORM_SIZE && detail == 9);
    NvnProgramComputeIn big = prog;
    big.h6fc = 0xFFFFFFF0u;
    CHECK(nvn_compute_qmd_check(&big, 3, 2, 1, &binds, &detail) == NVN_QMD_CBUF1_SIZE);
    CHECK(nvn_compute_qmd_check(NULL, 3, 2, 1, &binds, &detail) == NVN_QMD_NULL_ARGUMENT);
    CHECK(strcmp(nvn_compute_qmd_reason_name(NVN_QMD_UNIFORM_SIZE), "uniform size above 32 bits") == 0);
    /* Without uniforms the enable word has constant buffers 0 and 1 only. */
    NvnComputeBinds none = {0};
    none.cbuf0_address = binds.cbuf0_address;
    CHECK(nvn_compute_qmd_build(&prog, 3, 2, 1, &none, qmd));
    CHECK(qmd[0x14] == 0x3u);
}

static void test_compute_qmd_size_limits(void) {
    NvnProgramComputeIn prog = {0};
    NvnComputeBinds binds = {0};
    uint32_t qmd[NVN_COMPUTE_QMD_WORDS], detail;
    const uint64_t refused[] = {0x1FF01u, 0x20000u, 0xFFFFFF01u, UINT32_MAX};
    CHECK(strcmp(nvn_compute_qmd_reason_name(NVN_QMD_UNIFORM_QBUF_SIZE),
                 "rounded uniform size above its QMD field") == 0);
    /* QMD sizes occupy bits 31:15. Uniform sizes round up to 256 bytes before packing. */
    for (uint32_t slot = 0; slot < NVN_COMPUTE_UNIFORM_QBUF_SLOTS; slot++) {
        binds.uniform[slot] = (NvnComputeBindEntry){0x400003000ull, 0x1FE01u, 1};
        CHECK(nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd));
        CHECK((qmd[0x1E + 2u * (slot + 3u)] >> 15) == 0x1FF00u);
        CHECK(qmd[0x14] == (1u | (1u << (slot + 3u))));
        binds.uniform[slot].size = 0x1FF00u;
        CHECK(nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd));
        CHECK((qmd[0x1E + 2u * (slot + 3u)] >> 15) == 0x1FF00u);
        for (uint32_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
            binds.uniform[slot].size = refused[i];
            CHECK(nvn_compute_qmd_check(&prog, 1, 1, 1, &binds, &detail) == NVN_QMD_UNIFORM_QBUF_SIZE);
            CHECK(detail == slot);
            bind_sentinel(qmd, NVN_COMPUTE_QMD_WORDS);
            CHECK(!nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd));
            check_bind_sentinel(qmd, NVN_COMPUTE_QMD_WORDS);
        }
        binds.uniform[slot].bound = 0;
        CHECK(nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd) && qmd[0x14] == 1u);
    }
    /* Driver-only slots keep the full 32-bit size, without a QMD enable bit. */
    uint32_t words[NVN_COMPUTE_CBUF0_WORDS];
    for (uint32_t slot = NVN_COMPUTE_UNIFORM_QBUF_SLOTS; slot < NVN_COMPUTE_DESCRIPTOR_SLOTS; slot++) {
        binds.uniform[slot] = (NvnComputeBindEntry){0x400003000ull, UINT32_MAX, 1};
        CHECK(nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd) && qmd[0x14] == 1u);
        CHECK(nvn_compute_cbuf0_build(binds.uniform, binds.storage, words));
        CHECK(words[NVN_COMPUTE_CBUF0_UNIFORM_OFFSET / 4u + slot * 4u + 2u] == UINT32_MAX);
    }
    prog.h6fc = 0x1FF00u;
    CHECK(nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd));
    CHECK((qmd[0x20] >> 15) == 0x1FFFFu);
    prog.h6fc++;
    CHECK(nvn_compute_qmd_check(&prog, 1, 1, 1, &binds, &detail) == NVN_QMD_CBUF1_SIZE && detail == 0);
    bind_sentinel(qmd, NVN_COMPUTE_QMD_WORDS);
    CHECK(!nvn_compute_qmd_build(&prog, 1, 1, 1, &binds, qmd));
    check_bind_sentinel(qmd, NVN_COMPUTE_QMD_WORDS);
}

static void test_compute_cbuf0_build(void) {
    NvnComputeBindEntry uniform[NVN_COMPUTE_UNIFORM_SLOTS] = {0};
    NvnComputeBindEntry storage[NVN_COMPUTE_STORAGE_SLOTS] = {0};
    uniform[0] = (NvnComputeBindEntry){0x400000100ull, 0x40u, 1};
    storage[2] = (NvnComputeBindEntry){0x400005000ull, 0x1000u, 1};
    uint32_t words[NVN_COMPUTE_CBUF0_WORDS];
    CHECK(nvn_compute_cbuf0_build(uniform, storage, words));
    CHECK(words[0x210u / 4u] == 0x100u && words[0x210u / 4u + 1u] == 4u);
    CHECK(words[0x210u / 4u + 2u] == 0x40u && words[0x210u / 4u + 3u] == 0u);
    CHECK(words[0x310u / 4u + 2u * 4u] == 0x5000u && words[0x310u / 4u + 2u * 4u + 1u] == 4u);
    CHECK(words[0x310u / 4u + 2u * 4u + 2u] == 0x1000u);
    /* An empty slot stays zero, and the rest of the block is zero. */
    CHECK(words[0x210u / 4u + 4u] == 0u && words[0x310u / 4u] == 0u);
    CHECK(words[0] == 0u && words[NVN_COMPUTE_CBUF0_WORDS - 1] == 0u);
    /* Slots 8 to 15 have descriptors too (the library does not limit the table to 8). */
    uniform[12] = (NvnComputeBindEntry){0x400009000ull, 0x30u, 1};
    storage[15] = (NvnComputeBindEntry){0x400006000ull, 0x100u, 1};
    CHECK(nvn_compute_cbuf0_build(uniform, storage, words));
    CHECK(words[0x210u / 4u + 12u * 4u] == 0x9000u && words[0x210u / 4u + 12u * 4u + 2u] == 0x30u);
    CHECK(words[0x310u / 4u + 15u * 4u] == 0x6000u);
    storage[16] = (NvnComputeBindEntry){0x400006000ull, 0x100u, 1};
    CHECK(!nvn_compute_cbuf0_build(uniform, storage, words));
    storage[16].bound = 0;
    uniform[16] = (NvnComputeBindEntry){0x40000A000ull, 0x100u, 1};
    CHECK(!nvn_compute_cbuf0_build(uniform, storage, words));
}

static void test_program_compute_round_trip(void) {
    const NvnProgramComputeIn compute = {
        .code_offset = 0x11u, .cb_offset = 0x22u, .h6fc = 0x33u, .h710 = 0x44u, .h70c = 0x55u,
        .h718 = 0x66u, .h71c = 0x77u, .h720 = 0x88u, .h724 = 0x99u, .h728 = 0xAAu,
        .h72c = 0xBBu, .h730 = 0xCCu, .h734 = 0xDDu, .h6f8 = 0xEEu,
        .h750_low = 0x12345678u, .h750_high = 0x9ABCDEF0u, .flags = 0x5u,
    };
    uint32_t words[NVN_PROGRAM_WORDS_MAX] = {0};
    uint32_t count = 0;
    NvnProgramComputeIn back = {0};
    CHECK(nvn_program_build_words(&compute, NULL, NULL, NULL, NULL, 0, words, &count));
    CHECK(nvn_program_compute_from_words(words, count, &back));
    CHECK(memcmp(&compute, &back, sizeof compute) == 0);
    CHECK(!nvn_program_compute_from_words(words, NVN_PROGRAM_COMPUTE_WORDS - 1u, &back));
    CHECK(!nvn_program_compute_from_words(NULL, NVN_PROGRAM_COMPUTE_WORDS, &back));
    CHECK(!nvn_program_compute_from_words(words, NVN_PROGRAM_COMPUTE_WORDS, NULL));
}

int main(int argc, char** argv) {
    test_compute_qmd_size_limits();
    test_window_params();
    test_window_sync_cycle_params();
    test_sync_wait_limit();
    test_window_present_args();
    test_sync_after_token();
    test_sampler();
    test_sampler_equal();
    test_packaged_tile();
    test_texture_descriptor();
    test_texture_descriptor_image();
    test_texture_descriptor_fields();
    test_snap_formula();
    test_texture_layout();
    test_texture_level();
    test_texture_write_rows();
    test_texture_write_rows_3d();
    if (argc == 2) test_texture_queries(argv[1]);
    test_device_integer();
    test_timestamp();
    test_queue_memory();
    test_queue_defaults();
    test_memory_pool_defaults();
    test_memory_pool_params();
    test_pool_gpu_allocate();
    test_descriptor_offset();
    test_pool_range_fits();
    test_buffer_check();
    test_texture_layout_wide_size();
    test_rt_format_word();
    test_program_header_stage();
    test_program_build_vertex();
    test_program_build_vertex_fragment();
    test_program_build_fragment_2004();
    test_program_build_fragment_values();
    test_program_build_compute();
    test_program_build_refusals();
    test_compute_qmd_build();
    test_compute_cbuf0_build();
    test_program_compute_round_trip();
    test_word_stream();
    test_pushbuf_decode();
    test_program_words_decode();
    test_program_disable();
    test_handles();
    test_bind_buffer_active();
    test_state_insert_and_flag();
    test_state_setters();
    test_state_defaults();
    test_state_getters();
    test_state_getters_extra();
    test_vertex_attrib_state();
    test_reset_counter_word();
    test_xfb_words();
    test_depth_range_methods();
    test_stencil_op_and_polygon_offset();
    test_raster_methods();
    test_bind_color();
    test_bind_channel_mask();
    test_bind_depth_stencil();
    test_bind_polygon();
    test_bind_blend();
    test_bind_vertex_attrib();
    test_bind_vertex_stream();
    test_bind_vertex_buffer();
    test_bind_uniform_buffer();
    test_bind_storage_buffer();
    test_bind_texture();
    test_draw_words();
    test_report_counter_words();
    test_bind_image();
    test_clear_depth_stencil_words();
    test_highest_enabled_index();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_params: ok\n");
    return 0;
}
