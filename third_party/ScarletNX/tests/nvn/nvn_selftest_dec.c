/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The decoder of the NVN selftest command buffer recordings (gpu/nvn/test/nvn_selftest_dec.c): the two handle forms, the record
   ops, the pool records, the refused op, a jump cycle, a bad entry, the range cap and an unreadable address. Each
   expected value comes from the record layout that the sdk disassembly of the interpreter (d839d20) gives. */
#include "nx/gpu/nvn/test/nvn_selftest_dec.h"
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

#define FAKE_BASE 0x10000ull
#define FAKE_SIZE 0x1000u

static uint8_t fake[FAKE_SIZE];

static void fake_clear(void) { memset(fake, 0, sizeof fake); }

static void put64(uint64_t off, uint64_t value) {
    for (unsigned i = 0; i < 8; i++) fake[off + i] = (uint8_t)(value >> (8u * i));
}

static void put32(uint64_t off, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) fake[off + i] = (uint8_t)(value >> (8u * i));
}

/* {u64 gpu_start, u64 cpu_start, u32 word_count, u8 type} */
static void put_entry(uint64_t off, uint64_t gpu_start, uint64_t cpu_start, uint32_t words) {
    put64(off, gpu_start);
    put64(off + 8, cpu_start);
    put32(off + 16, words);
    fake[off + 20] = 0;
}

/* {u8 op at +0, u64 pool object at +8} */
static void put_pool(uint64_t off, uint32_t op, uint64_t pool) {
    fake[off] = (uint8_t)op;
    put64(off + 8, pool);
}

static const void* fake_read(void* ctx, uint64_t addr, size_t size) {
    (void)ctx;
    if (addr < FAKE_BASE) return NULL;
    const uint64_t off = addr - FAKE_BASE;
    if (off > FAKE_SIZE || size > FAKE_SIZE - off) return NULL;
    return fake + off;
}

static int decode(uint64_t handle, unsigned cap, unsigned* n, NvnStDecodeFault* fault) {
    NvnStRange ranges[8];
    return nvn_st_decode_recording(handle, fake_read, NULL, ranges, cap, n, fault);
}

static void test_handle_entries(void) {
    fake_clear();
    put_entry(0x00, 0x2000, 0x400, 4);
    put_entry(0x18, 0x3000, 0x800, 8);
    const uint64_t handle = (FAKE_BASE | 1u) | (2ull << 48);
    NvnStRange ranges[4];
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = nvn_st_decode_recording(handle, fake_read, NULL, ranges, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_OK);
    CHECK(n == 2);
    CHECK(ranges[0].cpu_start == 0x400 && ranges[0].words == 4);
    CHECK(ranges[1].cpu_start == 0x800 && ranges[1].words == 8);
}

static void test_records_and_jump(void) {
    fake_clear();
    put64(0x00, (1ull << 32) | 3u); /* op 03, one entry */
    put_entry(0x08, 0x2000, 0x100, 1);
    put64(0x20, 4u); /* op 04 */
    put64(0x28, FAKE_BASE + 0x40);
    put64(0x40, (1ull << 32) | 3u);
    put_entry(0x48, 0x3000, 0x200, 2);
    put64(0x60, 2u); /* end */
    NvnStRange ranges[4];
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = nvn_st_decode_recording(FAKE_BASE, fake_read, NULL, ranges, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_OK);
    CHECK(n == 2);
    CHECK(ranges[0].cpu_start == 0x100 && ranges[0].words == 1);
    CHECK(ranges[1].cpu_start == 0x200 && ranges[1].words == 2);
}

static void test_pool_records(void) {
    fake_clear();
    put_pool(0x00, 0x13, 0x100003000ull);
    put64(0x10, (1ull << 32) | 3u); /* op 03, one entry */
    put_entry(0x18, 0x2000, 0x100, 1);
    put_pool(0x30, 0x14, 0x100004000ull);
    put64(0x40, 2u); /* end */
    NvnStRange ranges[4];
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = nvn_st_decode_recording(FAKE_BASE, fake_read, NULL, ranges, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_OK);
    CHECK(n == 3);
    CHECK(ranges[0].pool_op == 0x13u && ranges[0].pool == 0x100003000ull && ranges[0].words == 0);
    CHECK(ranges[1].pool_op == 0u && ranges[1].cpu_start == 0x100 && ranges[1].words == 1);
    CHECK(ranges[2].pool_op == 0x14u && ranges[2].pool == 0x100004000ull && ranges[2].words == 0);
}

static void test_pool_cap(void) {
    fake_clear();
    put_pool(0x00, 0x13, 0x1000);
    put_pool(0x10, 0x14, 0x2000);
    put64(0x20, 2u); /* end */
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(FAKE_BASE, 1, &n, &fault);
    CHECK(status == NVN_ST_DEC_TOO_MANY_RANGES);
    CHECK(n == 1);
    CHECK(fault.op == 0x14u);
    CHECK(fault.addr == FAKE_BASE + 0x10);
}

static void test_pool_words(void) {
    uint32_t out[12];
    static const uint32_t texture[] = {0x2003055Du, 0x12345u,     0x6789A000u, 0x1FFu,     0x2003255Du,
                                       0x12345u,     0x6789A000u, 0x1FFu,      0x80400E20u, 0x200208E4u,
                                       0x6789A000u,  0x12345u};
    CHECK(nvn_st_pool_words(0x13, 0x123456789A000ull, 0x200u, out, 12) == 12u);
    CHECK(!memcmp(out, texture, sizeof texture));
    static const uint32_t sampler[] = {0x20030557u, 0x1u, 0x4000u, 0x3FFFu,
                                       0x20032557u, 0x1u, 0x4000u, 0x3FFFu};
    memset(out, 0, sizeof out);
    CHECK(nvn_st_pool_words(0x14, 0x100004000ull, 0x4000u, out, 12) == 8u);
    CHECK(!memcmp(out, sampler, sizeof sampler));
    CHECK(nvn_st_pool_words(0x14, 0x100004000ull, 0, out, 12) == 8u && out[3] == 0xFFFFFFFFu && out[7] == 0xFFFFFFFFu);
    CHECK(nvn_st_pool_words(0x15, 0, 0, out, 12) == 0u);
    CHECK(nvn_st_pool_words(0x13, 0, 0, NULL, 12) == 0u);
    CHECK(nvn_st_pool_words(0x13, 0, 0, out, 11) == 0u);
}

static void test_refused_op(void) {
    fake_clear();
    put64(0x00, 5u);
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(FAKE_BASE, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_REFUSED_OP);
    CHECK(fault.op == 5);
    CHECK(fault.addr == FAKE_BASE);
    CHECK(n == 0);
}

static void test_jump_cycle(void) {
    fake_clear();
    put64(0x00, 4u);
    put64(0x08, FAKE_BASE);
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(FAKE_BASE, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_TOO_MANY_JUMPS);
    CHECK(fault.op == 4);
}

static void test_bad_entry(void) {
    fake_clear();
    put64(0x00, (1ull << 32) | 3u);
    put_entry(0x08, 0x2000, 0x100, 0);
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(FAKE_BASE, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_BAD_ENTRY);
    CHECK(n == 0);
}

static void test_cap(void) {
    fake_clear();
    put_entry(0x00, 0x2000, 0x100, 1);
    put_entry(0x18, 0x3000, 0x200, 1);
    const uint64_t handle = (FAKE_BASE | 1u) | (2ull << 48);
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(handle, 1, &n, &fault);
    CHECK(status == NVN_ST_DEC_TOO_MANY_RANGES);
    CHECK(n == 0);
}

static void test_unreadable(void) {
    unsigned n = 0;
    NvnStDecodeFault fault;
    const int status = decode(FAKE_BASE + FAKE_SIZE + 0x100, 4, &n, &fault);
    CHECK(status == NVN_ST_DEC_READ_FAILED);
    CHECK(fault.addr == FAKE_BASE + FAKE_SIZE + 0x100);
}

int main(void) {
    test_handle_entries();
    test_records_and_jump();
    test_pool_records();
    test_pool_cap();
    test_pool_words();
    test_refused_op();
    test_jump_cycle();
    test_bad_entry();
    test_cap();
    test_unreadable();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_dec: ok\n");
    return 0;
}
