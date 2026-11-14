/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The comparer of the NVN selftest (gpu/nvn/test/nvn_selftest_cmp.c). The test of the test: a pair of records that differ in one
   return value, one byte, one state field, or the order of two sync operations must fail with the right kind, and equal
   records must pass. */
#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
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

static const uint8_t bytes_a[8] = {1, 2, 3, 4, 5, 6, 7, 8};

static void fill(NvnStRec* r, const uint8_t* bytes) {
    memset(r, 0, sizeof *r);
    nvn_st_add_ret(r, "size", 0x4000);
    nvn_st_add_ret(r, "align", 0x200);
    nvn_st_add_blob(r, "tic", bytes, NULL, 8);
    nvn_st_add_state(r, "viewport.w", 1280);
    nvn_st_add_state(r, "blend.src", 1);
    nvn_st_add_sync(r, "fence", 7);
    nvn_st_add_sync(r, "barrier", 0);
}

static void test_equal(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    CHECK(d.failed == 0);
}

static void test_return_value(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    b.ret[1].value = 0x100;
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_R);
    CHECK(strstr(d.first[0], "align") && strstr(d.first[0], "0x200") && strstr(d.first[0], "0x100"));
}

static void test_byte(void) {
    NvnStRec a, b;
    NvnStDiff d;
    uint8_t other[8];
    memcpy(other, bytes_a, 8);
    other[5] ^= 0x10;
    fill(&a, bytes_a);
    fill(&b, other);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_M);
    CHECK(strstr(d.first[1], "byte 5"));
}

static void test_ignore_mask(void) {
    NvnStRec a, b;
    NvnStDiff d;
    uint8_t other[8];
    const uint8_t mask[8] = {1, 1, 1, 1, 1, 0, 1, 1}; /* byte 5 is a private field of the library */
    memcpy(other, bytes_a, 8);
    other[5] ^= 0x10;
    fill(&a, bytes_a);
    fill(&b, other);
    a.blobs[0].mask = mask;
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    other[4] ^= 0x01; /* a byte that is not on the ignore list still fails */
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_M);
}

static void test_state(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    b.state[0].value = 1920;
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    CHECK(strstr(d.first[2], "viewport.w"));
}

static void test_sync_order(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    b.sync[0].op = "barrier"; /* the two operations swap places */
    b.sync[0].arg = 0;
    b.sync[1].op = "fence";
    b.sync[1].arg = 7;
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_Y);
    CHECK(strstr(d.first[3], "op 0"));
}

static void test_counts(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    b.n_sync--; /* the host lost one operation */
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_Y);
    fill(&b, bytes_a);
    b.n_ret--;
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_R);
    fill(&b, bytes_a);
    b.n_blobs = 0;
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_M);
}

static void test_several_kinds(void) {
    NvnStRec a, b;
    NvnStDiff d;
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    b.ret[0].value = 1;
    b.state[1].value = 2;
    CHECK(nvn_st_compare(&a, &b, &d) == (NVN_ST_R | NVN_ST_S));
}

static void test_overflow(void) {
    NvnStRec a, b;
    NvnStDiff d;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    for (int i = 0; i < NVN_ST_MAX_SYNC; i++) CHECK(nvn_st_add_sync(&a, "wait", (uint64_t)i));
    CHECK(!nvn_st_add_sync(&a, "wait", 99));
    b = a;
    /* equal tables, but both lost an entry: the record is not complete, so it cannot pass */
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_Y);
    CHECK(strstr(d.first[3], "full"));
}

static void test_format_line(void) {
    NvnStRec a, b;
    NvnStDiff d;
    char line[160];
    fill(&a, bytes_a);
    fill(&b, bytes_a);
    nvn_st_compare(&a, &b, &d);
    nvn_st_format_line(line, sizeof line, "BufferGetAddress", "(pool=1)", &a, &d);
    CHECK(strcmp(line, "selftest: BufferGetAddress (pool=1) R:ok M:ok S:ok Y:ok") == 0);
    b.ret[0].value = 0;
    nvn_st_compare(&a, &b, &d);
    nvn_st_format_line(line, sizeof line, "BufferGetAddress", "(pool=1)", &a, &d);
    CHECK(strstr(line, "R:FAIL M:ok"));
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    nvn_st_add_ret(&a, "v", 1);
    nvn_st_add_ret(&b, "v", 1);
    nvn_st_compare(&a, &b, &d);
    nvn_st_format_line(line, sizeof line, "GetFormat", "()", &a, &d);
    CHECK(strcmp(line, "selftest: GetFormat () R:ok M:- S:- Y:-") == 0);
}

/* The output word of a query sits between guard bytes: a write beyond the word is a kind M difference. */
static void test_guard_bytes(void) {
    uint8_t lib[24], host[24];
    NvnStRec a = {0}, b = {0};
    NvnStDiff d;
    memset(lib, 0xA5, sizeof lib);
    memset(host, 0xA5, sizeof host);
    memset(lib + 8, 7, 4);
    memset(host + 8, 7, 4);
    nvn_st_add_blob(&a, "out", lib, NULL, sizeof lib);
    nvn_st_add_blob(&b, "out", host, NULL, sizeof host);
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    memset(host + 12, 7, 4); /* an eight byte write */
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_M);
}

int main(void) {
    test_guard_bytes();
    test_equal();
    test_return_value();
    test_byte();
    test_ignore_mask();
    test_state();
    test_sync_order();
    test_counts();
    test_several_kinds();
    test_overflow();
    test_format_line();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_cmp: ok\n");
    return 0;
}
