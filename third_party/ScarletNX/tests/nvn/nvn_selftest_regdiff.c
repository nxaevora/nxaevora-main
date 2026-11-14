/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The register diff of the NVN selftest command buffer cases (gpu/nvn/test/nvn_selftest_regdiff.c): equal arrays, one change
   on one side, one change on each side in order, a change in both at one index, a value that returned to the base, and
   a cap that overflows. Each expected value comes from the rule of docs/graphics/nvn-unobserved.md, "The selftest". */
#include "nx/gpu/nvn/test/nvn_selftest_regdiff.h"
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

#define N 16u

static void fill(u32* v) {
    for (unsigned i = 0; i < N; i++) v[i] = 0x100u + i;
}

static void test_equal(void) {
    u32 base[N], a[N], b[N], out[N] = {0};
    fill(base);
    memcpy(a, base, sizeof base);
    memcpy(b, base, sizeof base);
    int overflow = 1;
    const unsigned n = nvn_st_regdiff(base, a, b, N, out, N, &overflow);
    CHECK(n == 0);
    CHECK(overflow == 0);
}

static void test_one_each_in_order(void) {
    u32 base[N], a[N], b[N], out[N] = {0};
    fill(base);
    memcpy(a, base, sizeof base);
    memcpy(b, base, sizeof base);
    a[5] = 0x555u;
    b[9] = 0x999u;
    int overflow = 1;
    const unsigned n = nvn_st_regdiff(base, a, b, N, out, N, &overflow);
    CHECK(n == 2);
    CHECK(overflow == 0);
    CHECK(out[0] == 5);
    CHECK(out[1] == 9);
}

static void test_both_at_one_index(void) {
    u32 base[N], a[N], b[N], out[N] = {0};
    fill(base);
    memcpy(a, base, sizeof base);
    memcpy(b, base, sizeof base);
    a[7] = 1;
    b[7] = 2;
    int overflow = 1;
    const unsigned n = nvn_st_regdiff(base, a, b, N, out, N, &overflow);
    CHECK(n == 1);
    CHECK(overflow == 0);
    CHECK(out[0] == 7);
}

static void test_returned_to_base(void) {
    u32 base[N], a[N], b[N], out[N] = {0};
    fill(base);
    memcpy(a, base, sizeof base);
    memcpy(b, base, sizeof base);
    a[3] = 0xDEADu;
    a[3] = base[3];
    b[11] = 0xBEEFu;
    b[11] = base[11];
    int overflow = 1;
    const unsigned n = nvn_st_regdiff(base, a, b, N, out, N, &overflow);
    CHECK(n == 0);
    CHECK(overflow == 0);
}

static void test_cap_overflow(void) {
    u32 base[N], a[N], b[N], out[N] = {0};
    fill(base);
    memcpy(a, base, sizeof base);
    memcpy(b, base, sizeof base);
    a[2] = 7;
    b[6] = 8;
    int overflow = 0;
    const unsigned n = nvn_st_regdiff(base, a, b, N, out, 1, &overflow);
    CHECK(n == 1);
    CHECK(overflow == 1);
    CHECK(out[0] == 2);
}

static void test_engine_names(void) {
    CHECK(!strcmp(nvn_st_engine_reg_name(NVN_ST_ENGINE_R3D, 0x47fu), "r3d.0x47f"));
    CHECK(!strcmp(nvn_st_engine_reg_name(NVN_ST_ENGINE_R2D, 0x080u), "r2d.0x080"));
    CHECK(!strcmp(nvn_st_engine_reg_name(NVN_ST_ENGINE_RDMA, 0x0c0u), "rdma.0x0c0"));
    CHECK(!strcmp(nvn_st_engine_reg_name(NVN_ST_ENGINE_RCOMP, 0x0b4u), "rcomp.0x0b4"));
}

static void test_engine_name_pointer(void) {
    CHECK(nvn_st_engine_reg_name(NVN_ST_ENGINE_R2D, 0x081u) ==
          nvn_st_engine_reg_name(NVN_ST_ENGINE_R2D, 0x081u));
}

static void test_engine_name_out_of_range(void) {
    CHECK(!strcmp(nvn_st_engine_reg_name(NVN_ST_ENGINE_COUNT, 0u), "reg.out_of_range"));
    for (unsigned e = 0; e < NVN_ST_ENGINE_COUNT; e++)
        CHECK(!strcmp(nvn_st_engine_reg_name(e, nvn_st_engines[e].count), "reg.out_of_range"));
}

static void test_reg_name_is_r3d(void) {
    for (unsigned r = 0; r < N; r++)
        CHECK(!strcmp(nvn_st_reg_name(r), nvn_st_engine_reg_name(NVN_ST_ENGINE_R3D, r)));
}

int main(void) {
    test_equal();
    test_one_each_in_order();
    test_both_at_one_index();
    test_returned_to_base();
    test_cap_overflow();
    test_engine_names();
    test_engine_name_pointer();
    test_engine_name_out_of_range();
    test_reg_name_is_r3d();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_regdiff: ok\n");
    return 0;
}
