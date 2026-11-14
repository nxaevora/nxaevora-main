/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/test/nvn_selftest_rec.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include <string.h>

static NvnStRec* rec;
static NvnStSnapshotFn snapshot_fn;
static unsigned calls;

/* The open write run: the front end may write adjacent ranges in several calls, and one run is one call. */
static u8 write_run[NVN_ST_WRITE_RUN_MAX];
static u64 write_run_start;
static u64 write_run_len;
static int write_run_open;
static u64 write_pool_gva; /* the texture pool address of the recording side, 0 for none */

#define FNV1A_OFFSET_BASIS 0xcbf29ce484222325ull
#define FNV1A_PRIME 0x100000001b3ull

static u64 fnv1a(const void* data, u64 len) {
    const u8* p = data;
    u64 h = FNV1A_OFFSET_BASIS;
    for (u64 i = 0; i < len; i++) h = (h ^ p[i]) * FNV1A_PRIME;
    return h;
}

/* Each side has its own pool memory, so the address is recorded relative to the side's pool, and a wrong address
   still differs. */
static void write_run_pool_relative(u64 start, u64 len) {
    if (!write_pool_gva) return;
    u64 off = (8u - (start % 8u)) % 8u;
    for (; off + 8u <= len; off += 8u) {
        u8* const b = write_run + off;
        u64 v = 0;
        for (unsigned i = 0; i < 8u; i++) v |= (u64)b[i] << (8u * i);
        if (v != write_pool_gva) continue;
        v -= write_pool_gva;
        for (unsigned i = 0; i < 8u; i++) b[i] = (u8)(v >> (8u * i));
    }
}

/* Closes the open run: its three fields are added to the record in its order. */
static void close_write_run(void) {
    if (!write_run_open) return;
    write_run_open = 0;
    if (rec) {
        write_run_pool_relative(write_run_start, write_run_len);
        nvn_st_add_state(rec, "write.address", write_run_start);
        nvn_st_add_state(rec, "write.len", write_run_len);
        nvn_st_add_state(rec, "write.hash", fnv1a(write_run, write_run_len));
    }
    write_run_len = 0;
}

static int start(void) { return 1; }
static void mem_may_have_changed(void) {}
static void stream_run(int on) { (void)on; }

/* a call that changes the picture (kind S): the snapshot first, then the arguments */
static void work(GpuChannel* ch, const char* op) {
    if (!rec) return;
    close_write_run();
    calls++;
    if (snapshot_fn) snapshot_fn(ch, rec);
    nvn_st_add_state(rec, op, 0);
}

static void clear(GpuChannel* ch, u32 v) {
    work(ch, "clear");
    if (rec) nvn_st_add_state(rec, "clear.value", v);
}

static void draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex, u32 base_instance,
                 u32 instances) {
    work(ch, "draw");
    if (!rec) return;
    nvn_st_add_state(rec, "draw.indexed", (uint64_t)indexed);
    nvn_st_add_state(rec, "draw.topology", topology);
    nvn_st_add_state(rec, "draw.first", first);
    nvn_st_add_state(rec, "draw.count", count);
    nvn_st_add_state(rec, "draw.base_vertex", base_vertex);
    nvn_st_add_state(rec, "draw.base_instance", base_instance);
    nvn_st_add_state(rec, "draw.instances", instances);
}

static void draw_texture(GpuChannel* ch) { work(ch, "draw_texture"); }

static void dispatch(GpuChannel* ch, const u32* q, u64 code) {
    work(ch, "dispatch");
    if (!rec) return;
    nvn_st_add_state(rec, "dispatch.code", code);
    if (!q) return;
    nvn_st_add_state(rec, "dispatch.grid_x", qmd_grid_dim_x(q));
    nvn_st_add_state(rec, "dispatch.grid_y", qmd_grid_dim_y(q));
    nvn_st_add_state(rec, "dispatch.grid_z", qmd_grid_dim_z(q));
    nvn_st_add_state(rec, "dispatch.block_x", qmd_block_dim_x(q));
    nvn_st_add_state(rec, "dispatch.block_y", qmd_block_dim_y(q));
    nvn_st_add_state(rec, "dispatch.block_z", qmd_block_dim_z(q));
    nvn_st_add_state(rec, "dispatch.shared", qmd_shared_alloc(q));
    nvn_st_add_state(rec, "dispatch.program_start", qmd_program_start(q));
    nvn_st_add_state(rec, "dispatch.cb_mask", q[QMD_CONST_BUFFER_ENABLE] & 0xFFu);
    for (u32 i = 0; i < QMD_NUM_CONST_BUFFERS; i++) {
        if (!qmd_const_buffer_enabled(q, i)) continue;
        nvn_st_add_state(rec, "dispatch.cb_address", qmd_const_buffer_address(q, i));
        nvn_st_add_state(rec, "dispatch.cb_size", qmd_const_buffer_size(q, i));
    }
}

/* kind Y: the order of the sync operations, not their timing */
static void method_barrier(int kind) {
    if (!rec) return;
    close_write_run();
    calls++;
    nvn_st_add_sync(rec, "barrier", (uint64_t)kind);
}
static void before_syncwait(void) {
    if (!rec) return;
    close_write_run();
    calls++;
    nvn_st_add_sync(rec, "wait", 0);
}
static void stream_incr(u32 id) {
    if (!rec) return;
    close_write_run();
    calls++;
    nvn_st_add_sync(rec, "fence", id);
}
static void release_write(u64 gva, const void* data, u32 len) {
    if (!rec) return;
    close_write_run();
    calls++;
    /* the payload (a counter or a timestamp) is a value of the run, so only the length is part of the order */
    (void)data;
    nvn_st_add_sync(rec, "semaphore", gva);
    nvn_st_add_sync(rec, "semaphore.len", len);
}
static void stream_end(void) {}

/* A front-end write to guest memory: adjacent writes merge into one run, whose address, length and hash are recorded.
   The memory is not written. */
static void engine_write(u64 gva, const void* data, u64 len) {
    if (!rec) return;
    if (write_run_open && gva == write_run_start + write_run_len) {
        if (len > NVN_ST_WRITE_RUN_MAX - write_run_len) {
            rec->overflow |= NVN_ST_S;
            write_run_open = 0;
            write_run_len = 0;
            return;
        }
        if (len) memcpy(write_run + write_run_len, data, len);
        write_run_len += len;
        return;
    }
    close_write_run();
    if (len > NVN_ST_WRITE_RUN_MAX) {
        rec->overflow |= NVN_ST_S;
        return;
    }
    write_run_start = gva;
    write_run_len = 0;
    write_run_open = 1;
    calls++;
    if (len) memcpy(write_run, data, len);
    write_run_len = len;
}

const Renderer nvn_st_renderer = {
    .name = "nvn-selftest-recorder",
    .start = start,
    .mem_may_have_changed = mem_may_have_changed,
    .stream_run = stream_run,
    .clear = clear,
    .draw = draw,
    .method_barrier = method_barrier,
    .draw_texture = draw_texture,
    .dispatch = dispatch,
    .before_syncwait = before_syncwait,
    .stream_incr = stream_incr,
    .release_write = release_write,
    .engine_write = engine_write,
    .stream_end = stream_end,
};

void nvn_st_renderer_begin(NvnStRec* r, NvnStSnapshotFn snapshot) {
    rec = r;
    snapshot_fn = snapshot;
    calls = 0;
    write_run_open = 0;
    write_run_len = 0;
    write_pool_gva = 0;
}

void nvn_st_renderer_set_texture_pool(u64 gva) { write_pool_gva = gva; }

unsigned nvn_st_renderer_end(void) {
    close_write_run();
    unsigned n = calls;
    rec = NULL;
    snapshot_fn = NULL;
    return n;
}
