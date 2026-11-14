/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/diag/draw_record.h"
#include "nx/gpu/nvn/diag/draw_record_sig.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include "nx/gpu/maxwell/maxwell_3d_regs.h"
#include "nx/gpu/renderer.h"
#include "nx/gpu/shader/shader.h"
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/main.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "common/filesystem/host.h"
#include "common/utilities/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bytes shader_hash reads from a program's address. The window is SH_CODE (vk/pipeline.c:31): an SPH has no size,
   so shader_hash ends the program at its EXIT, and the window must reach past the largest program. */
#define DRAWREC_SHADER_WINDOW 0x20000u
/* cs_read_code's window (vk/compute.c:764): 80 zero bytes stand in for an NVN header, then 0x10000 bytes of code,
   read at most through the size in the header 0x100 bytes before the code. */
#define DRAWREC_COMPUTE_PREFIX 80u
#define DRAWREC_COMPUTE_WINDOW 0x10000u
#define DRAWREC_COMPUTE_HEADER_OFF 0x100u
#define DRAWREC_COMPUTE_MAGIC 0x12345679u
#define DRAWREC_COMPUTE_MIN 0x80u
/* programs whose hash the cache keeps: open addressing, keyed by the code's GPU address and its kind */
#define DRAWREC_SHADER_CACHE 4096u
/* presents between two writes of the signature file */
#define DRAWREC_TABLE_WRITE_FRAMES 600u
/* renderer.h's method_barrier kind that invalidates the shader instruction cache (engines.c:30, compute.c:20) */
#define DRAWREC_BARRIER_SHADER_INSTRUCTIONS 5

static const Renderer* rec_next; /* the backend the wrappers forward to */
static Renderer rec_renderer;    /* the copy that g_renderer points at */
static PMutex rec_lock = PMUTEX_INIT;     /* the frame state, the table and the shader cache; a leaf lock */
static PMutex rec_sig_lock = PMUTEX_INIT; /* only one writer of the signature file at a time */
static int rec_on;                        /* the record changed the renderer and its files are open */

static char rec_frames_path[sizeof g_exedir + sizeof g_exename + 64];
static char rec_sigs_path[sizeof g_exedir + sizeof g_exename + 64];
static char rec_sigs_tmp[sizeof g_exedir + sizeof g_exename + 72];
static FILE* rec_frames_file;

static DrawRecTable rec_table;
static DrawRecShaderCache rec_shader_cache; /* the programs' hashes, by GPU address and kind */
static DrawRecShaderEntry rec_shader_entries[DRAWREC_SHADER_CACHE];
static u64 rec_shader_uncached; /* programs hashed when the shader cache was full */
static u64 rec_shader_rehashed; /* cached programs hashed again to a new hash */
static u64 rec_shader_rehashed_logged; /* rehashed at the last log line */
/* counts the renderer's mem_may_have_changed calls, the points where the backend's epoch ends and its shader
   cache checks the code again (vk/renderer.c, vk/pipeline.c:546) */
static u64 rec_epoch;

static struct {
    u64 counts[DRAWREC_KIND_COUNT];
    u64 ordered; /* the frame's order-dependent hash */
    u64 sum;     /* and its order-independent one */
    u64 frames;  /* presents so far */
} rec_frame;

/* the frame line's call counts, in its order */
static const DrawRecKind rec_kind_order[] = {
    DRAWREC_KIND_DRAW,      DRAWREC_KIND_CLEAR,     DRAWREC_KIND_DISPATCH, DRAWREC_KIND_BLIT,
    DRAWREC_KIND_DMA_COPY,  DRAWREC_KIND_BLIT_MEM,  DRAWREC_KIND_DRAW_TEXTURE,
};

static void rec_turn_off(const char* path, const char* why) {
    WARN("draw record: cannot write %s (%s): the record is off, the game goes on\n", path, why && why[0] ? why : "an error");
    __atomic_store_n(&rec_on, 0, __ATOMIC_RELAXED);
}

/* The hash of the program at gva, read as the backend reads code (gmem_read_code: zeros past the last mapped page, as
   a program can end next to an unmapped one). len is the guest bytes of its code. rec_lock guards the buffer. Lock
   order: rec_lock, then g_xlate_lock and the guest memory hooks. */
static u64 rec_hash_code(u64 gva, u32* len) {
    static u8 code[DRAWREC_SHADER_WINDOW];
    gmem_read_code(gva, code, DRAWREC_SHADER_WINDOW);
    return vk_shader_hash(code, DRAWREC_SHADER_WINDOW, len, NULL);
}

/* Copies the compute program at gva as cs_read_code does (vk/compute.c:764): zeros in the NVN header's place, then
   the code, bounded by a valid header 0x100 bytes before it. Returns the bytes of code it read. rec_lock guards
   the buffer. */
static u32 rec_read_compute_code(u64 gva, u8* dst) {
    u32 size = DRAWREC_COMPUTE_WINDOW;
    if (gva >= DRAWREC_COMPUTE_HEADER_OFF) {
        const u8* header = gmem_ptr_untracked(gva - DRAWREC_COMPUTE_HEADER_OFF, 8);
        if (header) {
            u32 magic = snx_ld_le32(header), declared = snx_ld_le32(header + 4);
            if (magic == DRAWREC_COMPUTE_MAGIC && declared >= DRAWREC_COMPUTE_MIN &&
                declared <= DRAWREC_COMPUTE_WINDOW && !(declared & 7) && gmem_ptr_untracked(gva, declared))
                size = declared;
        }
    }
    memset(dst, 0, DRAWREC_COMPUTE_PREFIX + DRAWREC_COMPUTE_WINDOW);
    gmem_read_code(gva, dst + DRAWREC_COMPUTE_PREFIX, size);
    return size;
}

/* The hash of the compute program at gva, over the window that cs_pipe hashes (vk/compute.c:642). len is the guest
   bytes of its code, the buffer's zero prefix left out. rec_lock guards the buffer. */
static u64 rec_hash_compute(u64 gva, u32* len) {
    static u8 code[DRAWREC_COMPUTE_PREFIX + DRAWREC_COMPUTE_WINDOW];
    u32 size = rec_read_compute_code(gva, code), hashed = 0;
    u64 h = vk_shader_hash(code, DRAWREC_COMPUTE_PREFIX + DRAWREC_COMPUTE_WINDOW, &hashed, NULL);
    *len = hashed > DRAWREC_COMPUTE_PREFIX ? hashed - DRAWREC_COMPUTE_PREFIX : size;
    return h;
}

/* The backend's check of a program (vk/pipeline.c:399): 1 when no write on either side touched its code since the
   baselines. vk_written_since's -1 (no tracking or no host pointer) counts as a change. Called with rec_lock held. */
static int rec_shader_unchanged(u64 gva, u32 len, u64 gw, u32 wseen) {
    if (!len) return 0;
    if (gpu_mem_dirty_since(gw, gva, len)) return 0;
    return vk_written_since(gmem_ptr(gva, len), len, wseen) == 0;
}

/* The program's hash, from the cache when it has it. Called with rec_lock held: a check or a miss reads guest memory.
   An entry is checked at most once in each epoch, as the backend checks its programs. */
static u64 rec_shader_hash(u64 gva, int compute) {
    DrawRecShaderEntry* e = drawrec_shader_cache_find(&rec_shader_cache, gva, compute);
    u64 epoch = __atomic_load_n(&rec_epoch, __ATOMIC_RELAXED), gw, h;
    u32 wseen, len;
    if (e) {
        if (!drawrec_shader_entry_due(e, epoch)) return e->hash;
        wseen = vk_ww_now();
        gw = gpu_mem_generation();
        if (rec_shader_unchanged(gva, e->len, e->gw, e->wseen)) {
            e->checked = epoch;
            e->gw = gw;
            e->wseen = wseen;
            return e->hash;
        }
        h = compute ? rec_hash_compute(gva, &len) : rec_hash_code(gva, &len);
        if (h != e->hash) rec_shader_rehashed++;
        e->hash = h;
        e->len = len;
        e->gw = gw;
        e->wseen = wseen;
        e->checked = epoch;
        return h;
    }
    wseen = vk_ww_now();
    gw = gpu_mem_generation();
    h = compute ? rec_hash_compute(gva, &len) : rec_hash_code(gva, &len);
    if (!drawrec_shader_cache_insert(&rec_shader_cache, gva, compute, h, len, gw, wseen, epoch))
        rec_shader_uncached++;
    return h;
}

/* the hashes of the 6 programs that MAXWELL3D_PIPELINES names, 0 for each one that is not enabled */
static void rec_shader_of_programs(const u32* r3d, u64 shader[6]) {
    u64 addr[6];
    u32 s;
    drawrec_program_addrs(r3d, addr);
    for (s = 0; s < 6; s++) shader[s] = addr[s] ? rec_shader_hash(addr[s], 0) : 0;
}

static void rec_log_full(void) {
    WARN_LIMIT(1, "draw record: the signature table is full, further new signatures are counted as dropped\n");
}

/* counts one call in the frame and the table. Called with rec_lock held; 0 when the table took no new entry */
static int rec_add(DrawRecKind kind, u64 sig, const char* text) {
    rec_frame.counts[kind]++;
    rec_frame.ordered = drawrec_fold_ordered(rec_frame.ordered, sig);
    rec_frame.sum = drawrec_fold_sum(rec_frame.sum, sig);
    return drawrec_table_insert(&rec_table, sig, text);
}

typedef struct {
    u64 sig;
    u32 count;
    char text[DRAWREC_TEXT_MAX];
} RecSnapEntry;

/* writes the signature file through a temporary file and a rename. The record turns off when a write fails. */
static void rec_write_sigs(void) {
    RecSnapEntry* snap = NULL;
    u64 frames, dropped, uncached, rehashed;
    u32 used = 0, i, n = 0;
    char why[256] = "";
    int ok = 1;
    FILE* f;
    pm_lock(&rec_sig_lock);
    pm_lock(&rec_lock);
    frames = rec_frame.frames;
    dropped = rec_table.dropped;
    uncached = rec_shader_uncached;
    rehashed = rec_shader_rehashed;
    used = rec_table.used;
    if (used) {
        snap = malloc((size_t)used * sizeof *snap);
        if (snap) {
            for (i = 0; i < rec_table.size && n < used; i++) {
                const DrawRecEntry* e = &rec_table.entries[i];
                if (!e->used) continue;
                snap[n].sig = e->sig;
                snap[n].count = e->count;
                memcpy(snap[n].text, e->text, DRAWREC_TEXT_MAX);
                n++;
            }
            used = n;
        }
    }
    pm_unlock(&rec_lock);
    if (used && !snap) {
        WARN("draw record: no memory for the signature table copy: this write of %s is skipped\n", rec_sigs_path);
        pm_unlock(&rec_sig_lock);
        return;
    }
    f = fopen(rec_sigs_tmp, "w");
    if (!f) {
        snx_os_error(why, sizeof why);
        ok = 0;
    }
    if (ok && fprintf(f, "frames %llu dropped %llu shader_uncached %llu\n", (unsigned long long)frames,
                      (unsigned long long)dropped, (unsigned long long)uncached) < 0) {
        snx_os_error(why, sizeof why);
        ok = 0;
    }
    for (i = 0; ok && i < used; i++)
        if (fprintf(f, "%016llx %llu %s\n", (unsigned long long)snap[i].sig, (unsigned long long)snap[i].count,
                    snap[i].text) < 0) {
            snx_os_error(why, sizeof why);
            ok = 0;
        }
    if (f && fclose(f) && ok) {
        snx_os_error(why, sizeof why);
        ok = 0;
    }
    if (ok && plat_replace_file(rec_sigs_tmp, rec_sigs_path)) {
        snx_os_error(why, sizeof why);
        ok = 0;
    }
    if (!ok) remove(rec_sigs_tmp);
    if (ok && rehashed != rec_shader_rehashed_logged) {
        LOG("draw record: %llu shader programs hashed again with a new hash\n", (unsigned long long)rehashed);
        rec_shader_rehashed_logged = rehashed;
    }
    free(snap);
    pm_unlock(&rec_sig_lock);
    if (!ok) rec_turn_off(rec_sigs_path, why);
}

/* ---- the recorded calls: record under the lock, then forward with the same arguments ---- */

static void rec_clear(GpuChannel* ch, u32 v) {
    char text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        snprintf(text, sizeof text, "clear v=%08x", v);
        pm_lock(&rec_lock);
        sig = drawrec_sig_clear(ch->r3d, v);
        kept = rec_add(DRAWREC_KIND_CLEAR, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    rec_next->clear(ch, v);
}

static void rec_draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex,
                     u32 base_instance, u32 instances) {
    char text[DRAWREC_TEXT_MAX];
    u64 shader[6];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        pm_lock(&rec_lock);
        rec_shader_of_programs(ch->r3d, shader);
        sig = drawrec_sig_draw(ch->r3d, indexed, topology, count, instances, shader);
        drawrec_draw_text(ch->r3d, indexed, topology, count, instances, shader, text, sizeof text);
        kept = rec_add(DRAWREC_KIND_DRAW, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    rec_next->draw(ch, indexed, topology, first, count, base_vertex, base_instance, instances);
}

static void rec_draw_texture(GpuChannel* ch) {
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        pm_lock(&rec_lock);
        sig = drawrec_sig_draw_texture(ch->r3d);
        kept = rec_add(DRAWREC_KIND_DRAW_TEXTURE, sig, "draw_texture");
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    rec_next->draw_texture(ch);
}

static int rec_blit(const Surf2D* s, const Surf2D* d, int dx, int dy, int dw, int dh, double sx, double sy, double sdx,
                    double sdy) {
    char text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        snprintf(text, sizeof text, "blit %ux%u %dx%d at %d,%d", s->width, s->height, dw, dh, dx, dy);
        pm_lock(&rec_lock);
        sig = drawrec_sig_blit(s, d, dx, dy, dw, dh);
        kept = rec_add(DRAWREC_KIND_BLIT, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    return rec_next->blit(s, d, dx, dy, dw, dh, sx, sy, sdx, sdy);
}

static int rec_dma_copy(const GpuDmaOp* o) {
    char text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        snprintf(text, sizeof text, "dma_copy %ux%u", o->width, o->lines);
        pm_lock(&rec_lock);
        sig = drawrec_sig_dma_copy(o);
        kept = rec_add(DRAWREC_KIND_DMA_COPY, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    return rec_next->dma_copy(o);
}

static int rec_blit_mem(const GpuBlit* b) {
    char text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig;
        snprintf(text, sizeof text, "blit_mem bpp=%u %ux%u fmt=%u", b->bpp, b->x1 - b->x0, b->y1 - b->y0, b->fmt);
        pm_lock(&rec_lock);
        sig = drawrec_sig_blit_mem(b);
        kept = rec_add(DRAWREC_KIND_BLIT_MEM, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    return rec_next->blit_mem(b);
}

static void rec_dispatch(GpuChannel* ch, const u32* q, u64 code) {
    char text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept;
        u64 sig, shader, addr;
        pm_lock(&rec_lock);
        addr = drawrec_dispatch_addr(q, code);
        shader = rec_shader_hash(addr, 1);
        sig = drawrec_sig_dispatch(q, shader);
        snprintf(text, sizeof text, "dispatch grid=%ux%ux%u block=%ux%ux%u shm=%u sh=%016llx", qmd_grid_dim_x(q),
                 qmd_grid_dim_y(q), qmd_grid_dim_z(q), qmd_block_dim_x(q), qmd_block_dim_y(q), qmd_block_dim_z(q),
                 qmd_shared_alloc(q), (unsigned long long)shader);
        kept = rec_add(DRAWREC_KIND_DISPATCH, sig, text);
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
    }
    rec_next->dispatch(ch, q, code);
}

static void rec_present(u64 cpu_addr, u32 w, u32 h, u32 pitch, u32 layout, u32 bhl, u32 fmt, const i32 crop[4],
                        u32 transform) {
    char line[320], text[DRAWREC_TEXT_MAX];
    if (__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) {
        int kept = 1, write_sigs = 0, write_ok = 1;
        u64 sig, frame, ordered, sum;
        u32 i;
        char why[256] = "";
        sig = drawrec_sig_present(w, h, pitch, layout, fmt);
        snprintf(text, sizeof text, "present %ux%u pitch=%u layout=%u fmt=%u", w, h, pitch, layout, fmt);
        pm_lock(&rec_lock);
        rec_frame.frames++;
        frame = rec_frame.frames;
        ordered = rec_frame.ordered;
        sum = rec_frame.sum;
        {
            u32 at = 0;
            int wrote;
            wrote = snprintf(line, sizeof line, "%llu", (unsigned long long)frame);
            if (wrote > 0) at = (u32)wrote < sizeof line ? (u32)wrote : (u32)sizeof line - 1;
            for (i = 0; i < sizeof rec_kind_order / sizeof rec_kind_order[0]; i++) {
                wrote = snprintf(line + at, sizeof line - at, " %llu",
                                 (unsigned long long)rec_frame.counts[rec_kind_order[i]]);
                if (wrote > 0 && (u32)wrote < sizeof line - at) at += (u32)wrote;
                else at = (u32)sizeof line - 1;
            }
            snprintf(line + at, sizeof line - at, " %016llx %016llx %016llx\n", (unsigned long long)ordered,
                     (unsigned long long)sum, (unsigned long long)sig);
        }
        kept = drawrec_table_insert(&rec_table, sig, text);
        write_sigs = frame % DRAWREC_TABLE_WRITE_FRAMES == 0;
        memset(rec_frame.counts, 0, sizeof rec_frame.counts);
        rec_frame.ordered = 0;
        rec_frame.sum = 0;
        pm_unlock(&rec_lock);
        if (!kept) rec_log_full();
        if (fputs(line, rec_frames_file) < 0 || fflush(rec_frames_file)) {
            snx_os_error(why, sizeof why);
            write_ok = 0;
        }
        if (!write_ok) rec_turn_off(rec_frames_path, why);
        if (write_sigs && __atomic_load_n(&rec_on, __ATOMIC_RELAXED)) rec_write_sigs();
    }
    rec_next->present_buffer(cpu_addr, w, h, pitch, layout, bhl, fmt, crop, transform);
}

static void rec_method_barrier(int kind) {
    rec_next->method_barrier(kind);
    if (kind != DRAWREC_BARRIER_SHADER_INSTRUCTIONS) return;
    /* the backend dropped its address shortcuts: another program may be at every cached address now */
    pm_lock(&rec_lock);
    drawrec_shader_cache_clear(&rec_shader_cache);
    pm_unlock(&rec_lock);
}

/* The epoch advances after the backend's: a draw after this call sees both new. */
static void rec_mem_may_have_changed(void) {
    rec_next->mem_may_have_changed();
    __atomic_add_fetch(&rec_epoch, 1, __ATOMIC_RELAXED);
}

static void rec_reset(void) {
    rec_next->reset();
    pm_lock(&rec_lock);
    drawrec_shader_cache_clear(&rec_shader_cache);
    pm_unlock(&rec_lock);
}

void draw_record_init(void) {
    DrawRecEntry* entries;
    if (!g_settings.nvn_draw_record || !g_renderer) return;
    entries = malloc((size_t)DRAWREC_TABLE_SIZE * sizeof *entries);
    if (!entries) {
        WARN("draw record: no memory for %u signature entries: the record is off\n", DRAWREC_TABLE_SIZE);
        return;
    }
    drawrec_table_init(&rec_table, entries, DRAWREC_TABLE_SIZE);
    drawrec_shader_cache_init(&rec_shader_cache, rec_shader_entries, DRAWREC_SHADER_CACHE);
    snprintf(rec_frames_path, sizeof rec_frames_path, "%s%s.drawrec%u.frames.txt", g_exedir, g_exename,
             (unsigned)g_settings.nvn_native);
    snprintf(rec_sigs_path, sizeof rec_sigs_path, "%s%s.drawrec%u.sigs.txt", g_exedir, g_exename,
             (unsigned)g_settings.nvn_native);
    snprintf(rec_sigs_tmp, sizeof rec_sigs_tmp, "%s.tmp", rec_sigs_path);
    rec_frames_file = fopen(rec_frames_path, "w");
    if (!rec_frames_file) {
        char why[256] = "";
        snx_os_error(why, sizeof why);
        WARN("draw record: cannot open %s (%s): the record is off\n", rec_frames_path, why);
        free(entries);
        rec_table.entries = NULL;
        return;
    }
    rec_next = g_renderer;
    rec_renderer = *rec_next;
    if (rec_next->clear) rec_renderer.clear = rec_clear;
    if (rec_next->draw) rec_renderer.draw = rec_draw;
    if (rec_next->draw_texture) rec_renderer.draw_texture = rec_draw_texture;
    if (rec_next->blit) rec_renderer.blit = rec_blit;
    if (rec_next->dma_copy) rec_renderer.dma_copy = rec_dma_copy;
    if (rec_next->blit_mem) rec_renderer.blit_mem = rec_blit_mem;
    if (rec_next->dispatch) rec_renderer.dispatch = rec_dispatch;
    if (rec_next->present_buffer) rec_renderer.present_buffer = rec_present;
    if (rec_next->method_barrier) rec_renderer.method_barrier = rec_method_barrier;
    if (rec_next->reset) rec_renderer.reset = rec_reset;
    if (rec_next->mem_may_have_changed) rec_renderer.mem_may_have_changed = rec_mem_may_have_changed;
    __atomic_store_n(&rec_on, 1, __ATOMIC_RELAXED);
    g_renderer = &rec_renderer;
    LOG("draw record: on, %s and %s\n", rec_frames_path, rec_sigs_path);
    /* Write the first signature file now: a run killed before the first periodic write must not keep an older run's. */
    rec_write_sigs();
}

void draw_record_save(void) {
    if (!__atomic_load_n(&rec_on, __ATOMIC_RELAXED)) return;
    /* Stop new records first: a call that passed the check before may still add one line after the save. */
    __atomic_store_n(&rec_on, 0, __ATOMIC_RELAXED);
    rec_write_sigs();
    if (rec_frames_file) fflush(rec_frames_file);
}
