/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The table of the host's NVN object records (gpu/nvn/nvn_objtab.c): records by guest address, replacement, removal with a
   new generation, growth past the first capacity, and the count of one kind. */
#include "nx/gpu/nvn/nvn_objtab.h"
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

static void test_put_find(void) {
    NvnObjTab t = {0};
    CHECK(!nvn_objtab_find(&t, 0x1000));
    NvnObj* o = nvn_objtab_put(&t, 0x1000, NVN_OBJ_QUEUE, 24);
    CHECK(o && o->kind == NVN_OBJ_QUEUE && o->data);
    for (int i = 0; o && i < 24; i++) CHECK(((unsigned char*)o->data)[i] == 0);
    CHECK(nvn_objtab_find(&t, 0x1000) == o);
    CHECK(!nvn_objtab_find(&t, 0x1004));
    CHECK(!nvn_objtab_put(&t, 0, NVN_OBJ_QUEUE, 8)); /* 0 marks an empty slot */
    CHECK(!nvn_objtab_put(&t, 0x2000, NVN_OBJ_NONE, 8));
    nvn_objtab_free(&t);
}

static void test_replace(void) {
    NvnObjTab t = {0};
    NvnObj* a = nvn_objtab_put(&t, 0x3000, NVN_OBJ_TEXTURE_BUILDER, 16);
    CHECK(a);
    uint32_t g1 = a->generation;
    memset(a->data, 0xAB, 16);
    NvnObj* b = nvn_objtab_put(&t, 0x3000, NVN_OBJ_TEXTURE, 32); /* the address is reused by another object */
    CHECK(b && b->kind == NVN_OBJ_TEXTURE);
    CHECK(b->generation != g1);
    CHECK(((unsigned char*)b->data)[0] == 0); /* the new data is zero, not the old bytes */
    CHECK(!nvn_objtab_valid(&t, 0x3000, g1));
    CHECK(nvn_objtab_valid(&t, 0x3000, b->generation));
    CHECK(nvn_objtab_count(&t, NVN_OBJ_TEXTURE_BUILDER) == 0);
    CHECK(nvn_objtab_count(&t, NVN_OBJ_TEXTURE) == 1);
    nvn_objtab_free(&t);
}

static void test_remove(void) {
    NvnObjTab t = {0};
    NvnObj* a = nvn_objtab_put(&t, 0x4000, NVN_OBJ_SYNC, 8);
    CHECK(a);
    uint32_t g = a->generation;
    CHECK(nvn_objtab_remove(&t, 0x4000));
    CHECK(!nvn_objtab_remove(&t, 0x4000)); /* already gone */
    CHECK(!nvn_objtab_find(&t, 0x4000));
    CHECK(!nvn_objtab_valid(&t, 0x4000, g));
    NvnObj* c = nvn_objtab_put(&t, 0x4000, NVN_OBJ_SYNC, 8); /* the same address again: a new record */
    CHECK(c && c->generation != g);
    CHECK(!nvn_objtab_valid(&t, 0x4000, g));
    nvn_objtab_free(&t);
}

static void test_growth(void) {
    NvnObjTab t = {0};
    enum { N = 5000 };
    for (uint64_t i = 1; i <= N; i++) {
        NvnObj* o = nvn_objtab_put(&t, i * 0x40, NVN_OBJ_MEMORY_POOL, 8);
        CHECK(o);
        if (!o) return;
        *(uint64_t*)o->data = i;
    }
    for (uint64_t i = 1; i <= N; i++) {
        NvnObj* o = nvn_objtab_find(&t, i * 0x40);
        CHECK(o && *(uint64_t*)o->data == i);
    }
    for (uint64_t i = 1; i <= N; i += 2) CHECK(nvn_objtab_remove(&t, i * 0x40));
    for (uint64_t i = 1; i <= N; i++) CHECK((nvn_objtab_find(&t, i * 0x40) != NULL) == (i % 2 == 0));
    /* removed slots stay in the probe chains, and a rebuild drops them while the live records stay */
    for (uint64_t i = N + 1; i <= N + 3000; i++) CHECK(nvn_objtab_put(&t, i * 0x40, NVN_OBJ_SAMPLER_POOL, 8));
    for (uint64_t i = 2; i <= N; i += 2) {
        NvnObj* o = nvn_objtab_find(&t, i * 0x40);
        CHECK(o && *(uint64_t*)o->data == i);
    }
    CHECK(nvn_objtab_count(&t, NVN_OBJ_SAMPLER_POOL) == 3000);
    nvn_objtab_free(&t);
}

int main(void) {
    test_put_find();
    test_replace();
    test_remove();
    test_growth();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_objtab: ok\n");
    return 0;
}
