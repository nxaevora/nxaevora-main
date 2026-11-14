/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/draw_record_sig.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include "nx/gpu/maxwell/maxwell_3d_regs.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A kind tag starts each signature, so two kinds cannot meet by chance. Each is four ASCII bytes. */
enum {
    DRAWREC_TAG_DRAW = 0x44524157u,
    DRAWREC_TAG_CLEAR = 0x434C5231u,
    DRAWREC_TAG_DISPATCH = 0x44535031u,
    DRAWREC_TAG_BLIT = 0x424C4931u,
    DRAWREC_TAG_DMA_COPY = 0x444D4131u,
    DRAWREC_TAG_BLIT_MEM = 0x424D4D31u,
    DRAWREC_TAG_DRAW_TEXTURE = 0x44545831u,
    DRAWREC_TAG_PRESENT = 0x50525331u
};

/* The depth target's registers. maxwell_3d_regs.h does not name them; render.c's read_targets reads them there:
   0x54E enables the target, 0x3F8/0x3F9 its address (not recorded), 0x48A/0x48B its size, 0x48C its layers, 0x3FA
   its format. The clear values are the render.c clear's 0x360 (color), 0x364 (depth) and 0x368 (stencil). */
enum {
    DRAWREC_R3D_ZETA_ENABLE = 0x54E,
    DRAWREC_R3D_ZETA_SIZE_W = 0x48A,
    DRAWREC_R3D_ZETA_SIZE_H = 0x48B,
    DRAWREC_R3D_ZETA_LAYERS = 0x48C,
    DRAWREC_R3D_ZETA_FORMAT = 0x3FA,
    DRAWREC_R3D_CLEAR_COLOR = 0x360,
    DRAWREC_R3D_CLEAR_DEPTH = 0x364,
    DRAWREC_R3D_CLEAR_STENCIL = 0x368
};

/* A color target's words that render.c reads, beyond MAXWELL3D_RT_FORMAT: width, height and the layer count. Words 0
   and 1 are the address, which no signature takes. */
enum {
    DRAWREC_RT_WIDTH = 2,
    DRAWREC_RT_HEIGHT = 3,
    DRAWREC_RT_LAYERS = 6
};

/* the golden constant of a splitmix step, and the multiplier of the frame's order hash */
#define DRAWREC_MIX_GAMMA 0x9E3779B97F4A7C15ull
#define DRAWREC_FOLD_PRIME 0x9E3779B97F4A7C15ull

static u64 mix64(u64 x) {
    x += DRAWREC_MIX_GAMMA;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static u64 sig_start(u64 tag) { return mix64(tag); }
static u64 sig_add(u64 h, u64 word) { return mix64(h ^ (word + DRAWREC_MIX_GAMMA)); }

/* the color target named by slot (0..7): 16 words each from MAXWELL3D_RT */
static const u32* rt_words(const u32* r3d, u32 slot) {
    return &r3d[MAXWELL3D_RT + slot * MAXWELL3D_RT_STRIDE];
}

/* Folds one target's format and size words. The caller has checked the slot's range. */
static u64 sig_add_target(u64 h, const u32* t) {
    h = sig_add(h, t[MAXWELL3D_RT_FORMAT]);
    h = sig_add(h, t[DRAWREC_RT_WIDTH]);
    h = sig_add(h, t[DRAWREC_RT_HEIGHT]);
    h = sig_add(h, t[DRAWREC_RT_LAYERS]);
    return h;
}

/* the enabled color targets of the 3D engine's file, in RT_CONTROL's drawing order */
static u64 sig_add_color_targets(u64 h, const u32* r3d) {
    u32 ctl = r3d[MAXWELL3D_RT_CONTROL], count = ctl & 15u, i;
    if (count > MAXWELL3D_NUM_RENDER_TARGETS) count = MAXWELL3D_NUM_RENDER_TARGETS;
    h = sig_add(h, count);
    for (i = 0; i < count; i++) {
        u32 slot = (ctl >> (4 + i * 3)) & 7u;
        h = sig_add(h, i);
        h = sig_add(h, slot);
        h = sig_add_target(h, rt_words(r3d, slot));
    }
    return h;
}

/* the depth target's format and size, when 0x54E enables it */
static u64 sig_add_depth_target(u64 h, const u32* r3d) {
    h = sig_add(h, r3d[DRAWREC_R3D_ZETA_ENABLE] ? 1u : 0u);
    if (!r3d[DRAWREC_R3D_ZETA_ENABLE]) return h;
    h = sig_add(h, r3d[DRAWREC_R3D_ZETA_FORMAT]);
    h = sig_add(h, r3d[DRAWREC_R3D_ZETA_SIZE_W]);
    h = sig_add(h, r3d[DRAWREC_R3D_ZETA_SIZE_H]);
    h = sig_add(h, r3d[DRAWREC_R3D_ZETA_LAYERS]);
    return h;
}

/* MAXWELL3D_PIPELINES holds 6 programs of 16 words. Program 1 (VertexB) always runs; the others only when bit 0 of
   their first word is set (vk/render.c:2098, :2140). */
#define DRAWREC_PROGRAM_COUNT 6u
#define DRAWREC_PROGRAM_STRIDE 16u
#define DRAWREC_PROGRAM_VERTEX_B 1u

void drawrec_program_addrs(const u32* r3d, u64 out[6]) {
    u64 base = ((u64)r3d[MAXWELL3D_PROGRAM_REGION] << 32) | r3d[MAXWELL3D_PROGRAM_REGION + 1];
    u32 s;
    for (s = 0; s < DRAWREC_PROGRAM_COUNT; s++) {
        const u32* p = &r3d[MAXWELL3D_PIPELINES + DRAWREC_PROGRAM_STRIDE * s];
        out[s] = (s == DRAWREC_PROGRAM_VERTEX_B || (p[0] & 1u)) ? base + p[1] : 0;
    }
}

u64 drawrec_dispatch_addr(const u32* q, u64 code) { return code + qmd_program_start(q); }

u64 drawrec_sig_draw(const u32* r3d, int indexed, u32 topology, u32 count, u32 instances, const u64 shader[6]) {
    u64 h = sig_start(DRAWREC_TAG_DRAW);
    u32 i;
    h = sig_add(h, (u64)(u32)indexed);
    h = sig_add(h, topology);
    h = sig_add(h, count);
    h = sig_add(h, instances);
    for (i = 0; i < 6; i++) h = sig_add(h, shader[i]);
    h = sig_add_color_targets(h, r3d);
    h = sig_add_depth_target(h, r3d);
    return h;
}

u64 drawrec_sig_clear(const u32* r3d, u32 v) {
    u64 h = sig_start(DRAWREC_TAG_CLEAR);
    u32 ctl = r3d[MAXWELL3D_RT_CONTROL];
    u32 mask = (v >> 2) & 15u, slot = (v >> 6) & 15u, i;
    h = sig_add(h, v);
    if (mask) {
        h = sig_add(h, 1u);
        for (i = 0; i < 4; i++)
            if (mask & (1u << i)) h = sig_add(h, r3d[DRAWREC_R3D_CLEAR_COLOR + i]);
        /* the slot is logical: RT_CONTROL maps it to a physical target only below its count (vk/render.c:57, :463) */
        if (slot < (ctl & 15u))
            h = sig_add_target(h, rt_words(r3d, (ctl >> (4 + slot * 3)) & 7u));
        else
            h = sig_add(h, 0xFFFFFFFFu);
    }
    if (v & 3u) {
        h = sig_add(h, 2u);
        if (v & 1u) h = sig_add(h, r3d[DRAWREC_R3D_CLEAR_DEPTH]);
        if (v & 2u) h = sig_add(h, r3d[DRAWREC_R3D_CLEAR_STENCIL] & 0xFFu);
        h = sig_add(h, r3d[DRAWREC_R3D_ZETA_FORMAT]);
        h = sig_add(h, r3d[DRAWREC_R3D_ZETA_SIZE_W]);
        h = sig_add(h, r3d[DRAWREC_R3D_ZETA_SIZE_H]);
        h = sig_add(h, r3d[DRAWREC_R3D_ZETA_LAYERS]);
    }
    return h;
}

u64 drawrec_sig_dispatch(const u32* q, u64 shader) {
    u64 h = sig_start(DRAWREC_TAG_DISPATCH);
    h = sig_add(h, shader);
    h = sig_add(h, qmd_grid_dim_x(q));
    h = sig_add(h, qmd_grid_dim_y(q));
    h = sig_add(h, qmd_grid_dim_z(q));
    h = sig_add(h, qmd_block_dim_x(q));
    h = sig_add(h, qmd_block_dim_y(q));
    h = sig_add(h, qmd_block_dim_z(q));
    h = sig_add(h, qmd_shared_alloc(q));
    return h;
}

/* one 2D engine surface: its format, layout and size; the address is not recorded */
static u64 sig_add_surf2d(u64 h, const Surf2D* s) {
    h = sig_add(h, s->format);
    h = sig_add(h, s->linear);
    h = sig_add(h, s->bh);
    h = sig_add(h, s->width);
    h = sig_add(h, s->height);
    return h;
}

u64 drawrec_sig_blit(const Surf2D* s, const Surf2D* d, int dx, int dy, int dw, int dh) {
    u64 h = sig_start(DRAWREC_TAG_BLIT);
    h = sig_add_surf2d(h, s);
    h = sig_add_surf2d(h, d);
    h = sig_add(h, (u64)(u32)dx);
    h = sig_add(h, (u64)(u32)dy);
    h = sig_add(h, (u64)(u32)dw);
    h = sig_add(h, (u64)(u32)dh);
    return h;
}

u64 drawrec_sig_dma_copy(const GpuDmaOp* o) {
    u64 h = sig_start(DRAWREC_TAG_DMA_COPY);
    h = sig_add(h, (u64)(u32)o->multi);
    h = sig_add(h, (u64)(u32)o->remap);
    h = sig_add(h, (u64)(u32)o->fill);
    h = sig_add(h, (u64)(u32)o->shuffle);
    h = sig_add(h, (u64)(u32)o->keep);
    h = sig_add(h, o->width);
    h = sig_add(h, o->lines);
    h = sig_add(h, o->line_in);
    h = sig_add(h, o->line_out);
    h = sig_add(h, (u64)(u32)o->src.bl);
    h = sig_add(h, o->src.pitch);
    h = sig_add(h, (u64)(u32)o->dst.bl);
    h = sig_add(h, o->dst.pitch);
    return h;
}

u64 drawrec_sig_blit_mem(const GpuBlit* b) {
    u64 h = sig_start(DRAWREC_TAG_BLIT_MEM);
    h = sig_add(h, b->bpp);
    h = sig_add(h, b->x0);
    h = sig_add(h, b->x1);
    h = sig_add(h, b->y0);
    h = sig_add(h, b->y1);
    h = sig_add(h, b->fmt);
    return h;
}

u64 drawrec_sig_draw_texture(const u32* r3d) {
    u64 h = sig_start(DRAWREC_TAG_DRAW_TEXTURE);
    u32 ctl = r3d[MAXWELL3D_RT_CONTROL], slot = (ctl >> 4) & 7u, i;
    for (i = 0; i < 8; i++) h = sig_add(h, r3d[MAXWELL3D_DRAW_TEXTURE + i]);
    h = sig_add(h, rt_words(r3d, slot)[MAXWELL3D_RT_FORMAT]);
    return h;
}

u64 drawrec_sig_present(u32 w, u32 h, u32 pitch, u32 layout, u32 fmt) {
    u64 s = sig_start(DRAWREC_TAG_PRESENT);
    s = sig_add(s, w);
    s = sig_add(s, h);
    s = sig_add(s, pitch);
    s = sig_add(s, layout);
    s = sig_add(s, fmt);
    return s;
}

u64 drawrec_fold_ordered(u64 prev, u64 sig) { return mix64(prev * DRAWREC_FOLD_PRIME + sig); }

u64 drawrec_fold_sum(u64 sum, u64 sig) { return sum + sig; }

/* appends to out with a limit, always with a NUL at or before cap - 1 */
static void text_add(char* out, u32 cap, u32* n, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void text_add(char* out, u32 cap, u32* n, const char* fmt, ...) {
    va_list ap;
    int w;
    if (!out || !cap || *n >= cap) return;
    va_start(ap, fmt);
    w = vsnprintf(out + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *n += (u32)w;
    if (*n >= cap) *n = cap - 1;
}

void drawrec_draw_text(const u32* r3d, int indexed, u32 topology, u32 count, u32 instances, const u64 shader[6],
                       char* out, u32 cap) {
    u32 n = 0, i, ctl, targets;
    if (!out || !cap) return;
    out[0] = 0;
    text_add(out, cap, &n, "draw indexed=%d topo=0x%x count=%u inst=%u sh=", indexed, topology, count, instances);
    for (i = 0; i < 6; i++) text_add(out, cap, &n, i ? ",%016llx" : "%016llx", (unsigned long long)shader[i]);
    ctl = r3d[MAXWELL3D_RT_CONTROL];
    targets = ctl & 15u;
    if (targets > MAXWELL3D_NUM_RENDER_TARGETS) targets = MAXWELL3D_NUM_RENDER_TARGETS;
    for (i = 0; i < targets; i++) {
        u32 slot = (ctl >> (4 + i * 3)) & 7u;
        const u32* t = rt_words(r3d, slot);
        text_add(out, cap, &n, " c%u:%x:%ux%ux%u", i, t[MAXWELL3D_RT_FORMAT], t[DRAWREC_RT_WIDTH],
                 t[DRAWREC_RT_HEIGHT], t[DRAWREC_RT_LAYERS]);
    }
    if (r3d[DRAWREC_R3D_ZETA_ENABLE])
        text_add(out, cap, &n, " z:%x:%ux%ux%u", r3d[DRAWREC_R3D_ZETA_FORMAT], r3d[DRAWREC_R3D_ZETA_SIZE_W],
                 r3d[DRAWREC_R3D_ZETA_SIZE_H], r3d[DRAWREC_R3D_ZETA_LAYERS]);
}

static void text_copy(char* dst, const char* src, u32 cap) {
    u32 i = 0;
    if (!cap) return;
    if (src)
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            i++;
        }
    dst[i] = 0;
}

static u32 table_slot(const DrawRecTable* t, u64 sig) { return (u32)(mix64(sig) & (t->size - 1)); }

void drawrec_table_init(DrawRecTable* t, DrawRecEntry* entries, u32 size) {
    t->entries = entries;
    t->size = size;
    t->used = 0;
    t->dropped = 0;
    if (entries) memset(entries, 0, (size_t)size * sizeof *entries);
}

int drawrec_table_insert(DrawRecTable* t, u64 sig, const char* text) {
    u32 first, probe;
    if (!t || !t->entries || !t->size) return 0;
    first = table_slot(t, sig);
    for (probe = 0; probe < t->size; probe++) {
        DrawRecEntry* e = &t->entries[(first + probe) & (t->size - 1)];
        if (!e->used) {
            e->used = 1;
            e->sig = sig;
            e->count = 1;
            text_copy(e->text, text, DRAWREC_TEXT_MAX);
            t->used++;
            return 1;
        }
        if (e->sig == sig) {
            e->count++;
            return 1;
        }
    }
    t->dropped++;
    return 0;
}

const DrawRecEntry* drawrec_table_find(const DrawRecTable* t, u64 sig) {
    u32 first, probe;
    if (!t || !t->entries || !t->size) return NULL;
    first = table_slot(t, sig);
    for (probe = 0; probe < t->size; probe++) {
        const DrawRecEntry* e = &t->entries[(first + probe) & (t->size - 1)];
        if (!e->used) return NULL;
        if (e->sig == sig) return e;
    }
    return NULL;
}
