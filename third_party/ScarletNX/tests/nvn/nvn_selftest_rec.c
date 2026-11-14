/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The recording Renderer of the NVN selftest (gpu/nvn/test/nvn_selftest_rec.c): the engine's calls land in a record in order,
   the same calls give equal records, a changed argument or a changed order is a fail of the right kind, and calls outside
   a recording are not kept. */
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include "nx/gpu/nvn/test/nvn_selftest_rec.h"
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

static int snapshots;
static void snapshot(GpuChannel* ch, NvnStRec* r) {
    (void)ch;
    snapshots++;
    nvn_st_add_state(r, "snap", 5);
}

/* a clear, a draw, a barrier and a fence (in one of two orders), a semaphore and a wait: the calls of a small frame */
static void script(NvnStRec* r, u32 count, int swap, NvnStSnapshotFn snap) {
    const Renderer* g = &nvn_st_renderer;
    nvn_st_renderer_begin(r, snap);
    g->clear(NULL, 0xff);
    g->draw(NULL, 0, 4, 0, count, 0, 0, 1);
    if (swap) {
        g->stream_incr(9);
        g->method_barrier(1);
    } else {
        g->method_barrier(1);
        g->stream_incr(9);
    }
    g->release_write(0x7000, NULL, 16);
    g->before_syncwait();
}

static void test_same_script(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    CHECK(nvn_st_renderer_end() == 6);
    script(&b, 3, 0, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    CHECK(a.n_state == 10); /* clear 2 entries, draw 8 */
    CHECK(a.n_sync == 5);   /* barrier, fence, semaphore 2, wait */
}

static void test_changed_argument(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    nvn_st_renderer_end();
    script(&b, 4, 0, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
}

static void test_changed_order(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    script(&a, 3, 0, NULL);
    nvn_st_renderer_end();
    script(&b, 3, 1, NULL);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_Y);
}

static void test_snapshot_and_idle(void) {
    static NvnStRec a;
    snapshots = 0;
    script(&a, 3, 0, snapshot);
    nvn_st_renderer_end();
    CHECK(snapshots == 2); /* the clear and the draw */
    CHECK(a.n_state == 12);
    nvn_st_renderer.draw(NULL, 0, 4, 0, 3, 0, 0, 1); /* no recording: nothing is kept, nothing crashes */
    nvn_st_renderer.stream_incr(1);
    CHECK(a.n_state == 12 && a.n_sync == 5);
}

static void dispatch_record(NvnStRec* r, const u32* q) {
    nvn_st_renderer_begin(r, NULL);
    nvn_st_renderer.dispatch(NULL, q, 0x1000);
    nvn_st_renderer_end();
}

static void test_dispatch_arguments(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    u32 qa[KEPLER_COMPUTE_QMD_WORDS] = {0}, qb[KEPLER_COMPUTE_QMD_WORDS] = {0};
    qa[QMD_GRID_DIM_X] = 4;
    qb[QMD_GRID_DIM_X] = 4;
    dispatch_record(&a, qa);
    dispatch_record(&b, qb);
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    qb[QMD_GRID_DIM_X] = 5;
    dispatch_record(&b, qb);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    qb[QMD_GRID_DIM_X] = 4;
    qb[QMD_BLOCK_DIM_YZ] = 8;
    dispatch_record(&b, qb);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    qb[QMD_BLOCK_DIM_YZ] = 0;
    qb[QMD_CONST_BUFFER_ENABLE] = 1;
    qb[QMD_CONST_BUFFER_CONFIG] = 0x1000;
    dispatch_record(&b, qb);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
}

static void write_record(NvnStRec* r, u64 gva, const void* data, u64 len) {
    memset(r, 0, sizeof *r); /* each compare must see one write, not the fields of the earlier ones */
    nvn_st_renderer_begin(r, NULL);
    nvn_st_renderer.engine_write(gva, data, len);
    nvn_st_renderer_end();
}

static void pool_record(NvnStRec* r, u64 pool, u64 gva, const void* data, u64 len) {
    memset(r, 0, sizeof *r);
    nvn_st_renderer_begin(r, NULL);
    nvn_st_renderer_set_texture_pool(pool);
    nvn_st_renderer.engine_write(gva, data, len);
    nvn_st_renderer_end();
}

static void fill(u8* buf, unsigned n) {
    for (unsigned i = 0; i < n; i++) buf[i] = (u8)(i * 11u + 5u);
}

static void test_engine_write(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    u8 buf[64], copy[64];
    unsigned i;
    for (i = 0; i < sizeof buf; i++) buf[i] = (u8)(i * 7 + 3);
    memcpy(copy, buf, sizeof buf);

    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, 64);
    CHECK(nvn_st_renderer_end() == 1); /* the call is counted */
    CHECK(a.n_state == 3);             /* address, length, hash */

    write_record(&b, 0x100007040, buf, 64);
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
    buf[17] ^= 0xff;
    write_record(&b, 0x100007040, buf, 64);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    buf[17] ^= 0xff;
    write_record(&b, 0x100007040, buf, 60);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    write_record(&b, 0x100007044, buf, 64);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);

    CHECK(memcmp(copy, buf, sizeof buf) == 0); /* the source is not changed */
    nvn_st_renderer.engine_write(0x100007040, buf, 64); /* no recording: nothing is kept, nothing crashes */
    CHECK(a.n_state == 3);
    CHECK(b.n_state == 3);
}

static void test_write_merge(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    u8 buf[32];
    fill(buf, sizeof buf);

    memset(&a, 0, sizeof a);
    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, 16);
    nvn_st_renderer.engine_write(0x100007050, buf + 16, 16);
    CHECK(nvn_st_renderer_end() == 1); /* one merged run is one call */
    write_record(&b, 0x100007040, buf, sizeof buf);
    CHECK(nvn_st_compare(&a, &b, &d) == 0);
}

static void test_write_gap(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    u8 buf[32];
    fill(buf, sizeof buf);

    memset(&a, 0, sizeof a);
    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, 16);
    nvn_st_renderer.engine_write(0x100007060, buf + 16, 16);
    nvn_st_renderer_end();
    write_record(&b, 0x100007040, buf, sizeof buf);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
    CHECK(a.n_state == 6); /* two runs */
}

static void test_write_call_between(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    u8 buf[32];
    fill(buf, sizeof buf);

    memset(&a, 0, sizeof a);
    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, 16);
    nvn_st_renderer.method_barrier(0);
    nvn_st_renderer.engine_write(0x100007050, buf + 16, 16);
    nvn_st_renderer_end();
    CHECK(a.n_state == 6); /* two runs */

    memset(&b, 0, sizeof b);
    nvn_st_renderer_begin(&b, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, sizeof buf);
    nvn_st_renderer.method_barrier(0);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
}

static void test_write_end_closes(void) {
    static NvnStRec a;
    u8 buf[16];
    fill(buf, sizeof buf);

    memset(&a, 0, sizeof a);
    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100007040, buf, sizeof buf);
    CHECK(nvn_st_renderer_end() == 1);
    CHECK(a.n_state == 3);
}

static void test_texture_pool(void) {
    static NvnStRec a, b, c;
    NvnStDiff d;
    static const u8 lib_bytes[8] = {0x00, 0x00, 0x6b, 0x59, 0x05, 0x00, 0x00, 0x00};
    static const u8 host_bytes[8] = {0x00, 0x00, 0x6d, 0x59, 0x05, 0x00, 0x00, 0x00};

    pool_record(&a, 0x5596b0000ull, 0x559660100ull, lib_bytes, sizeof lib_bytes);

    memset(&b, 0, sizeof b);
    nvn_st_renderer_begin(&b, NULL);
    nvn_st_renderer_set_texture_pool(0x5596d0000ull);
    nvn_st_renderer.engine_write(0x559660100ull, host_bytes, 4);
    nvn_st_renderer.engine_write(0x559660104ull, host_bytes + 4, 4);
    nvn_st_renderer_end();
    CHECK(nvn_st_compare(&a, &b, &d) == 0);

    pool_record(&c, 0x5596d0000ull, 0x559660100ull, lib_bytes, sizeof lib_bytes);
    CHECK(nvn_st_compare(&a, &c, &d) == NVN_ST_S);
}

static void test_texture_pool_none(void) {
    static NvnStRec a, b;
    NvnStDiff d;
    static const u8 lib_bytes[8] = {0x00, 0x00, 0x6b, 0x59, 0x05, 0x00, 0x00, 0x00};
    static const u8 host_bytes[8] = {0x00, 0x00, 0x6d, 0x59, 0x05, 0x00, 0x00, 0x00};

    pool_record(&a, 0, 0x559660100ull, lib_bytes, sizeof lib_bytes);
    pool_record(&b, 0, 0x559660100ull, host_bytes, sizeof host_bytes);
    CHECK(nvn_st_compare(&a, &b, &d) == NVN_ST_S);
}

static void test_write_overflow(void) {
    static NvnStRec a;
    static u8 big[NVN_ST_WRITE_RUN_MAX + 1u];

    memset(&a, 0, sizeof a);
    nvn_st_renderer_begin(&a, NULL);
    nvn_st_renderer.engine_write(0x100000000ull, big, sizeof big);
    nvn_st_renderer_end();
    CHECK((a.overflow & NVN_ST_S) != 0);
    CHECK(a.n_state == 0);
}

int main(void) {
    test_dispatch_arguments();
    test_same_script();
    test_changed_argument();
    test_changed_order();
    test_snapshot_and_idle();
    test_engine_write();
    test_write_merge();
    test_write_gap();
    test_write_call_between();
    test_write_end_closes();
    test_texture_pool();
    test_texture_pool_none();
    test_write_overflow();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("nvn_selftest_rec: ok\n");
    return 0;
}
