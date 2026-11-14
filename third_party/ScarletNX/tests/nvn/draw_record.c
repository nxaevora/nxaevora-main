/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the draw record (gpu/nvn/diag/draw_record_sig.c): each kind's signature from the register file, the leaving
   out of the address words, the frame's two hashes, the text's limit, the signature table and the shader hash
   cache. */
#include "nx/gpu/nvn/diag/draw_record_sig.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"
#include "nx/gpu/maxwell/maxwell_3d_regs.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

/* the color target words outside maxwell_3d_regs.h's names (render.c reads them: 0 address high, 1 low, 2 width,
   3 height, 6 the layers) */
enum { TEST_RT_ADDR_HI = 0, TEST_RT_ADDR_LO = 1, TEST_RT_WIDTH = 2, TEST_RT_HEIGHT = 3, TEST_RT_LAYERS = 6 };
/* the depth target's and the clear values' registers, which maxwell_3d_regs.h does not name */
enum {
    TEST_ZETA_ENABLE = 0x54E,
    TEST_ZETA_ADDR_HI = 0x3F8,
    TEST_ZETA_ADDR_LO = 0x3F9,
    TEST_ZETA_FORMAT = 0x3FA,
    TEST_ZETA_CLEAR = 0x364,
    TEST_CLEAR_COLOR = 0x360,
    TEST_CLEAR_STENCIL = 0x368
};

static u32 r3d[GPU_ENGINE_METHODS];

static void reset_regs(void) { memset(r3d, 0, sizeof r3d); }

/* one enabled color target in slot 0, with an address, a size, a format and a layer */
static void base_targets(void) {
    u32* t = &r3d[MAXWELL3D_RT];
    r3d[MAXWELL3D_RT_CONTROL] = 1;
    t[TEST_RT_ADDR_HI] = 0x00000001;
    t[TEST_RT_ADDR_LO] = 0x00002000;
    t[TEST_RT_WIDTH] = 1920;
    t[TEST_RT_HEIGHT] = 1080;
    t[MAXWELL3D_RT_FORMAT] = 0x1234;
    t[TEST_RT_LAYERS] = 1;
    r3d[MAXWELL3D_PROGRAM_REGION] = 0x00000001;
    r3d[MAXWELL3D_PROGRAM_REGION + 1] = 0x00010000;
    r3d[MAXWELL3D_PIPELINES + 1] = 0x40;
}

static void test_draw_signature(void) {
    u64 sh[6] = {1, 2, 3, 4, 5, 6}, other[6] = {1, 2, 3, 4, 5, 6};
    u64 s0;
    reset_regs();
    base_targets();
    s0 = drawrec_sig_draw(r3d, 0, 4, 120, 1, sh);
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) == s0);

    /* the address words of the color target and of the program base are not recorded */
    r3d[MAXWELL3D_RT + TEST_RT_ADDR_HI] ^= 0x100;
    r3d[MAXWELL3D_RT + TEST_RT_ADDR_LO] ^= 0x40;
    r3d[MAXWELL3D_PROGRAM_REGION] ^= 0xFF;
    r3d[MAXWELL3D_PROGRAM_REGION + 1] ^= 0x10;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) == s0);

    /* first, base_vertex and base_instance live in registers that the draw does not record */
    r3d[MAXWELL3D_VERTEX_BUFFER_FIRST] = 7;
    r3d[MAXWELL3D_INDEX_BUFFER_FIRST] = 9;
    r3d[MAXWELL3D_GLOBAL_BASE_VERTEX_INDEX] = 3;
    r3d[MAXWELL3D_GLOBAL_BASE_INSTANCE_INDEX] = 2;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) == s0);

    /* the depth target's address words are not recorded, but its enable word is */
    r3d[TEST_ZETA_ENABLE] = 1;
    r3d[TEST_ZETA_ADDR_HI] = 1;
    r3d[TEST_ZETA_ADDR_LO] = 0x3000;
    r3d[TEST_ZETA_FORMAT] = 0x22;
    s0 = drawrec_sig_draw(r3d, 0, 4, 120, 1, sh);
    r3d[TEST_ZETA_ADDR_HI] ^= 0x100;
    r3d[TEST_ZETA_ADDR_LO] ^= 0x40;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) == s0);
    r3d[TEST_ZETA_ENABLE] = 0;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) != s0);
    r3d[TEST_ZETA_ENABLE] = 1;

    /* a target's format and a shader hash are recorded */
    s0 = drawrec_sig_draw(r3d, 0, 4, 120, 1, sh);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) != s0);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    other[2]++;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, other) != s0);

    /* the change of a count or of the indexed bit is recorded */
    CHECK(drawrec_sig_draw(r3d, 1, 4, 120, 1, sh) != s0);
    CHECK(drawrec_sig_draw(r3d, 0, 3, 120, 1, sh) != s0);
    CHECK(drawrec_sig_draw(r3d, 0, 4, 121, 1, sh) != s0);
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 2, sh) != s0);
}

static void test_draw_target_words(void) {
    u64 sh[6] = {1, 2, 3, 4, 5, 6};
    u64 s0;
    reset_regs();
    base_targets();
    s0 = drawrec_sig_draw(r3d, 0, 4, 120, 1, sh);
    r3d[MAXWELL3D_RT + TEST_RT_WIDTH]++;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) != s0);
    r3d[MAXWELL3D_RT + TEST_RT_WIDTH]--;
    r3d[MAXWELL3D_RT + TEST_RT_HEIGHT]++;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) != s0);
    r3d[MAXWELL3D_RT + TEST_RT_HEIGHT]--;
    r3d[MAXWELL3D_RT + TEST_RT_LAYERS]++;
    CHECK(drawrec_sig_draw(r3d, 0, 4, 120, 1, sh) != s0);
}

static void test_program_addrs(void) {
    u64 out[6], moved[6], base;
    u32 s;
    reset_regs();
    r3d[MAXWELL3D_PROGRAM_REGION] = 0x00000001;
    r3d[MAXWELL3D_PROGRAM_REGION + 1] = 0x00010000;
    base = ((u64)r3d[MAXWELL3D_PROGRAM_REGION] << 32) | r3d[MAXWELL3D_PROGRAM_REGION + 1];
    /* VertexB (program 1) runs even with its enable bit clear (vk/render.c:2098, :2140) */
    r3d[MAXWELL3D_PIPELINES + 16 * 1 + 1] = 0x100;
    r3d[MAXWELL3D_PIPELINES + 16 * 0 + 1] = 0x200;
    r3d[MAXWELL3D_PIPELINES + 16 * 4 + 1] = 0x300;
    drawrec_program_addrs(r3d, out);
    CHECK(out[1] == base + 0x100);
    CHECK(out[0] == 0 && out[4] == 0);
    CHECK(out[2] == 0 && out[3] == 0 && out[5] == 0);
    r3d[MAXWELL3D_PIPELINES + 16 * 0] = 1;
    r3d[MAXWELL3D_PIPELINES + 16 * 4] = 1;
    drawrec_program_addrs(r3d, out);
    CHECK(out[0] == base + 0x200);
    CHECK(out[4] == base + 0x300);
    CHECK(out[1] == base + 0x100);
    /* the base words move every used address by the same amount */
    r3d[MAXWELL3D_PROGRAM_REGION + 1] += 0x1000;
    drawrec_program_addrs(r3d, moved);
    for (s = 0; s < 6; s++)
        if (out[s]) CHECK(moved[s] - out[s] == 0x1000);
}

static void test_dispatch_addr(void) {
    static u32 q[KEPLER_COMPUTE_QMD_WORDS];
    u64 a, b;
    memset(q, 0, sizeof q);
    q[QMD_PROGRAM_START] = 0x100;
    a = drawrec_dispatch_addr(q, 0x1000);
    q[QMD_PROGRAM_START] = 0x200;
    b = drawrec_dispatch_addr(q, 0x1000);
    CHECK(a == 0x1100);
    CHECK(b == 0x1200);
    CHECK(a != b);
}

static void test_clear_signature(void) {
    u32 v = (15u << 2) | (0u << 6), vd = 1u; /* a color clear of slot 0, then a depth clear */
    u64 s0;
    reset_regs();
    base_targets();
    r3d[TEST_CLEAR_COLOR + 0] = 0x3F800000;
    r3d[TEST_CLEAR_COLOR + 1] = 0x3F000000;
    s0 = drawrec_sig_clear(r3d, v);
    CHECK(drawrec_sig_clear(r3d, v) == s0);

    r3d[MAXWELL3D_RT + TEST_RT_ADDR_HI] ^= 0x100;
    r3d[MAXWELL3D_RT + TEST_RT_ADDR_LO] ^= 0x40;
    CHECK(drawrec_sig_clear(r3d, v) == s0);
    r3d[TEST_CLEAR_COLOR + 1] ^= 0x80;
    CHECK(drawrec_sig_clear(r3d, v) != s0);
    r3d[TEST_CLEAR_COLOR + 1] ^= 0x80;
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_clear(r3d, v) != s0);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_clear(r3d, v ^ (1u << 6)) != s0);

    r3d[TEST_ZETA_ENABLE] = 1;
    r3d[TEST_ZETA_FORMAT] = 0x22;
    r3d[TEST_ZETA_CLEAR] = 0x3F800000;
    s0 = drawrec_sig_clear(r3d, vd);
    r3d[TEST_ZETA_ADDR_HI] ^= 0x100;
    r3d[TEST_ZETA_ADDR_LO] ^= 0x40;
    CHECK(drawrec_sig_clear(r3d, vd) == s0);
    r3d[TEST_ZETA_CLEAR] = 0;
    CHECK(drawrec_sig_clear(r3d, vd) != s0);
}

static void test_clear_logical_slot(void) {
    u32 v = 1u << 2; /* a color clear with mask bit 0, logical slot 0 */
    u64 s0;
    reset_regs();
    base_targets();
    r3d[MAXWELL3D_RT_CONTROL] = 0x11; /* one target, logical 0 to physical 1 */
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] = 0x1111;                      /* physical 0: not used */
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_STRIDE + MAXWELL3D_RT_FORMAT] = 0x2222; /* physical 1: used */
    s0 = drawrec_sig_clear(r3d, v);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_STRIDE + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_clear(r3d, v) != s0);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_STRIDE + MAXWELL3D_RT_FORMAT] ^= 0x10;
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_clear(r3d, v) == s0);
}

static void test_clear_value_masks(void) {
    u64 s0;
    reset_regs();
    base_targets();
    r3d[TEST_CLEAR_COLOR] = 0x3F800000;
    r3d[TEST_CLEAR_STENCIL] = 0x0F;
    r3d[TEST_ZETA_ENABLE] = 1;
    r3d[TEST_ZETA_FORMAT] = 0x22;
    r3d[TEST_ZETA_CLEAR] = 0x3F000000;

    /* depth only (v = 1): the stencil value and the color words are not recorded */
    s0 = drawrec_sig_clear(r3d, 1);
    r3d[TEST_CLEAR_STENCIL] = 0xF0;
    CHECK(drawrec_sig_clear(r3d, 1) == s0);
    r3d[TEST_CLEAR_COLOR] ^= 0x80;
    CHECK(drawrec_sig_clear(r3d, 1) == s0);
    r3d[TEST_CLEAR_COLOR] ^= 0x80;

    /* a color clear with mask bit 0 only: 0x361 is not recorded, 0x360 is */
    r3d[TEST_CLEAR_COLOR] = 0x12345678;
    r3d[TEST_CLEAR_COLOR + 1] = 0xAAAAAAAA;
    s0 = drawrec_sig_clear(r3d, 1u << 2);
    r3d[TEST_CLEAR_COLOR + 1] ^= 1;
    CHECK(drawrec_sig_clear(r3d, 1u << 2) == s0);
    r3d[TEST_CLEAR_COLOR] ^= 1;
    CHECK(drawrec_sig_clear(r3d, 1u << 2) != s0);

    /* stencil only (v = 2): only the low byte of 0x368 is recorded */
    r3d[TEST_CLEAR_STENCIL] = 0x0000007F;
    s0 = drawrec_sig_clear(r3d, 2);
    r3d[TEST_CLEAR_STENCIL] = 0xFF00007F;
    CHECK(drawrec_sig_clear(r3d, 2) == s0);
}

static void test_draw_texture_signature(void) {
    u64 s0;
    u32 i;
    reset_regs();
    base_targets();
    for (i = 0; i < 8; i++) r3d[MAXWELL3D_DRAW_TEXTURE + i] = 0x100 + i;
    s0 = drawrec_sig_draw_texture(r3d);
    CHECK(drawrec_sig_draw_texture(r3d) == s0);
    r3d[MAXWELL3D_DRAW_TEXTURE + 7] ^= 1;
    CHECK(drawrec_sig_draw_texture(r3d) != s0);
    r3d[MAXWELL3D_DRAW_TEXTURE + 7] ^= 1;
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    CHECK(drawrec_sig_draw_texture(r3d) != s0);
    r3d[MAXWELL3D_RT + MAXWELL3D_RT_FORMAT] ^= 0x10;
    r3d[MAXWELL3D_RT + TEST_RT_ADDR_HI] ^= 0x100;
    r3d[MAXWELL3D_RT + TEST_RT_ADDR_LO] ^= 0x40;
    CHECK(drawrec_sig_draw_texture(r3d) == s0);
}

static void test_dispatch_signature(void) {
    static u32 q[KEPLER_COMPUTE_QMD_WORDS];
    u64 s0;
    memset(q, 0, sizeof q);
    q[QMD_GRID_DIM_X] = 4;
    q[QMD_GRID_DIM_YZ] = (2u << 16) | 3u;
    q[QMD_BLOCK_DIM_X] = 64u << 16;
    q[QMD_BLOCK_DIM_YZ] = (8u << 16) | 4u;
    q[QMD_SHARED_ALLOC] = 1024;
    s0 = drawrec_sig_dispatch(q, 0xABCD);
    CHECK(drawrec_sig_dispatch(q, 0xABCD) == s0);
    q[QMD_GRID_DIM_X]++;
    CHECK(drawrec_sig_dispatch(q, 0xABCD) != s0);
    q[QMD_GRID_DIM_X]--;
    q[QMD_BLOCK_DIM_YZ] ^= 1;
    CHECK(drawrec_sig_dispatch(q, 0xABCD) != s0);
    q[QMD_BLOCK_DIM_YZ] ^= 1;
    q[QMD_SHARED_ALLOC] = 2048;
    CHECK(drawrec_sig_dispatch(q, 0xABCD) != s0);
    q[QMD_SHARED_ALLOC] = 1024;
    CHECK(drawrec_sig_dispatch(q, 0xABCE) != s0);
}

static void fill_surf(Surf2D* s, u32 fmt, u32 addr) {
    memset(s, 0, sizeof *s);
    s->format = fmt;
    s->linear = 0;
    s->bh = 4;
    s->depth = 1;
    s->layer = 0;
    s->pitch = 64;
    s->width = 16;
    s->height = 16;
    s->addr = addr;
}

static void test_blit_signature(void) {
    Surf2D s, d;
    u64 s0;
    fill_surf(&s, 0x11, 0x1000);
    fill_surf(&d, 0x12, 0x2000);
    s0 = drawrec_sig_blit(&s, &d, 1, 2, 3, 4);
    CHECK(drawrec_sig_blit(&s, &d, 1, 2, 3, 4) == s0);
    s.format ^= 0x10;
    CHECK(drawrec_sig_blit(&s, &d, 1, 2, 3, 4) != s0);
    s.format ^= 0x10;
    d.linear = 1;
    CHECK(drawrec_sig_blit(&s, &d, 1, 2, 3, 4) != s0);
    d.linear = 0;
    CHECK(drawrec_sig_blit(&s, &d, 5, 2, 3, 4) != s0);
    /* the surfaces' addresses are not recorded */
    s.addr = 0x9000;
    d.addr = 0xA000;
    CHECK(drawrec_sig_blit(&s, &d, 1, 2, 3, 4) == s0);
}

static void test_dma_copy_signature(void) {
    GpuDmaOp o;
    static const u8 snap[8];
    u64 s0;
    memset(&o, 0, sizeof o);
    o.multi = 1;
    o.width = 4;
    o.lines = 2;
    o.line_in = 16;
    o.line_out = 16;
    o.src.bl = 1;
    o.src.pitch = 64;
    o.src.addr = 0x1000;
    o.dst.bl = 0;
    o.dst.pitch = 128;
    o.dst.addr = 0x2000;
    o.snap = snap;
    s0 = drawrec_sig_dma_copy(&o);
    CHECK(drawrec_sig_dma_copy(&o) == s0);
    o.width++;
    CHECK(drawrec_sig_dma_copy(&o) != s0);
    o.width--;
    o.src.pitch ^= 0x40;
    CHECK(drawrec_sig_dma_copy(&o) != s0);
    o.src.pitch ^= 0x40;
    o.dst.bl = 1;
    CHECK(drawrec_sig_dma_copy(&o) != s0);
    o.dst.bl = 0;
    /* the addresses and the submission snapshot are not recorded */
    o.src.addr = 0x9000;
    o.dst.addr = 0xA000;
    o.snap = NULL;
    CHECK(drawrec_sig_dma_copy(&o) == s0);
}

static void test_blit_mem_signature(void) {
    GpuBlit b;
    static const u32 scol[8], srow[8];
    u64 s0;
    memset(&b, 0, sizeof b);
    b.bpp = 4;
    b.x0 = 1;
    b.x1 = 9;
    b.y0 = 2;
    b.y1 = 10;
    b.fmt = 0;
    b.src.addr = 0x1000;
    b.dst.addr = 0x2000;
    b.scol = scol;
    b.srow = srow;
    s0 = drawrec_sig_blit_mem(&b);
    CHECK(drawrec_sig_blit_mem(&b) == s0);
    b.bpp = 2;
    CHECK(drawrec_sig_blit_mem(&b) != s0);
    b.bpp = 4;
    b.fmt = GPU_BLIT_FLOAT(4, 4, 4, 4, 0);
    CHECK(drawrec_sig_blit_mem(&b) != s0);
    b.fmt = 0;
    b.x1++;
    CHECK(drawrec_sig_blit_mem(&b) != s0);
    b.x1--;
    /* the surfaces and the sample tables are not recorded */
    b.src.addr = 0x9000;
    b.dst.addr = 0xA000;
    b.scol = NULL;
    b.srow = NULL;
    CHECK(drawrec_sig_blit_mem(&b) == s0);
}

static void test_present_signature(void) {
    u64 s0 = drawrec_sig_present(1920, 1080, 7680, 0, 0x1);
    CHECK(drawrec_sig_present(1920, 1080, 7680, 0, 0x1) == s0);
    CHECK(drawrec_sig_present(1280, 1080, 7680, 0, 0x1) != s0);
    CHECK(drawrec_sig_present(1920, 720, 7680, 0, 0x1) != s0);
    CHECK(drawrec_sig_present(1920, 1080, 3840, 0, 0x1) != s0);
    CHECK(drawrec_sig_present(1920, 1080, 7680, 1, 0x1) != s0);
    CHECK(drawrec_sig_present(1920, 1080, 7680, 0, 0x2) != s0);
    /* the four kinds do not meet by chance: their signatures differ */
    CHECK(s0 != drawrec_sig_clear(r3d, 0));
}

static void test_frame_hashes(void) {
    u64 a = 0x1111222233334444ull, b = 0x5555666677778888ull;
    u64 sum_ab = drawrec_fold_sum(drawrec_fold_sum(0, a), b);
    u64 sum_ba = drawrec_fold_sum(drawrec_fold_sum(0, b), a);
    u64 ord_ab = drawrec_fold_ordered(drawrec_fold_ordered(1, a), b);
    u64 ord_ba = drawrec_fold_ordered(drawrec_fold_ordered(1, b), a);
    CHECK(sum_ab == sum_ba);   /* addition commutes */
    CHECK(ord_ab != ord_ba);  /* the order hash does not */
    CHECK(drawrec_fold_ordered(1, a) != drawrec_fold_ordered(1, b));
    CHECK(drawrec_fold_sum(5, a) == 5 + a);
}

static void test_table(void) {
    DrawRecEntry entries[4];
    DrawRecTable t;
    const DrawRecEntry* e;
    drawrec_table_init(&t, entries, 4);
    CHECK(t.used == 0 && t.dropped == 0);
    CHECK(drawrec_table_insert(&t, 0xA, "a") == 1);
    e = drawrec_table_find(&t, 0xA);
    CHECK(e != NULL && e->count == 1 && strcmp(e->text, "a") == 0);
    CHECK(drawrec_table_insert(&t, 0xA, "a again") == 1); /* an entry that is there counts */
    e = drawrec_table_find(&t, 0xA);
    CHECK(e != NULL && e->count == 2 && strcmp(e->text, "a") == 0); /* the first text stays */
    CHECK(drawrec_table_insert(&t, 0xB, "b") == 1);
    CHECK(drawrec_table_insert(&t, 0xC, "c") == 1);
    CHECK(drawrec_table_insert(&t, 0xD, "d") == 1);
    CHECK(t.used == 4);
    CHECK(drawrec_table_insert(&t, 0xE, "e") == 0); /* the full table drops the new one */
    CHECK(t.dropped == 1);
    CHECK(drawrec_table_find(&t, 0xE) == NULL);
    CHECK(drawrec_table_insert(&t, 0xA, "a") == 1); /* an old signature still counts when full */
    e = drawrec_table_find(&t, 0xA);
    CHECK(e != NULL && e->count == 3);
}

static void test_text(void) {
    char small[16], big[DRAWREC_TEXT_MAX];
    u64 sh[6] = {1, 2, 3, 4, 5, 6};
    reset_regs();
    base_targets();
    drawrec_draw_text(r3d, 0, 4, 120, 1, sh, small, sizeof small);
    CHECK(strlen(small) == sizeof small - 1); /* cut to the buffer, NUL at its end */
    CHECK(strncmp(small, "draw ", 5) == 0);
    drawrec_draw_text(r3d, 0, 4, 120, 1, sh, big, sizeof big);
    CHECK(strlen(big) < sizeof big);
    CHECK(strstr(big, "count=120") != NULL);
    CHECK(strstr(big, "sh=0000000000000001") != NULL);
    CHECK(strstr(big, "c0:1234:1920x1080x1") != NULL);
    drawrec_draw_text(r3d, 0, 4, 120, 1, sh, small, 0); /* no room: nothing written, nothing crashes */
    small[0] = 'x';
    drawrec_draw_text(r3d, 0, 4, 120, 1, sh, small, 0);
    CHECK(small[0] == 'x');
}

static void test_shader_cache(void) {
    DrawRecShaderEntry entries[4];
    DrawRecShaderCache c;
    DrawRecShaderEntry* e;
    drawrec_shader_cache_init(&c, entries, 4);
    CHECK(drawrec_shader_cache_find(&c, 0x1000, 0) == NULL);
    e = drawrec_shader_cache_insert(&c, 0x1000, 0, 0xAA, 88, 1, 2, 1);
    CHECK(e != NULL && e->hash == 0xAA && e->len == 88 && e->gw == 1 && e->wseen == 2 && e->checked == 1);

    /* a compute program at the same address is another entry */
    e = drawrec_shader_cache_insert(&c, 0x1000, 1, 0xBB, 96, 3, 4, 1);
    CHECK(e != NULL);
    e = drawrec_shader_cache_find(&c, 0x1000, 0);
    CHECK(e != NULL && e->hash == 0xAA);
    e = drawrec_shader_cache_find(&c, 0x1000, 1);
    CHECK(e != NULL && e->hash == 0xBB);
    CHECK(drawrec_shader_cache_find(&c, 0x1004, 0) == NULL);

    /* two addresses with the same probe start are both found */
    drawrec_shader_cache_clear(&c);
    CHECK(drawrec_shader_cache_insert(&c, 0x1000, 0, 0x11, 88, 1, 2, 1) != NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x2000, 0, 0x22, 88, 1, 2, 1) != NULL);
    e = drawrec_shader_cache_find(&c, 0x2000, 0);
    CHECK(e != NULL && e->hash == 0x22);
    e = drawrec_shader_cache_find(&c, 0x1000, 0);
    CHECK(e != NULL && e->hash == 0x11);

    /* the fifth program has no room in a cache of four */
    drawrec_shader_cache_clear(&c);
    CHECK(drawrec_shader_cache_insert(&c, 0x0000, 0, 1, 88, 1, 2, 1) != NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x0100, 0, 2, 88, 1, 2, 1) != NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x0200, 0, 3, 88, 1, 2, 1) != NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x0300, 0, 4, 88, 1, 2, 1) != NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x0400, 0, 5, 88, 1, 2, 1) == NULL);
    CHECK(drawrec_shader_cache_find(&c, 0x0400, 0) == NULL);

    /* clear forgets every program and makes the cache usable again */
    drawrec_shader_cache_clear(&c);
    CHECK(drawrec_shader_cache_find(&c, 0x0000, 0) == NULL);
    CHECK(drawrec_shader_cache_find(&c, 0x0300, 0) == NULL);
    CHECK(drawrec_shader_cache_insert(&c, 0x0400, 0, 5, 88, 1, 2, 1) != NULL);
    e = drawrec_shader_cache_find(&c, 0x0400, 0);
    CHECK(e != NULL && e->hash == 5);
}

static void test_shader_entry_due(void) {
    DrawRecShaderEntry e;
    memset(&e, 0, sizeof e);
    e.checked = 7;
    CHECK(drawrec_shader_entry_due(&e, 8) == 1);
    CHECK(drawrec_shader_entry_due(&e, 7) == 0);
}

int main(void) {
    test_draw_signature();
    test_draw_target_words();
    test_program_addrs();
    test_dispatch_addr();
    test_clear_signature();
    test_clear_logical_slot();
    test_clear_value_masks();
    test_draw_texture_signature();
    test_dispatch_signature();
    test_blit_signature();
    test_dma_copy_signature();
    test_blit_mem_signature();
    test_present_signature();
    test_frame_hashes();
    test_table();
    test_text();
    test_shader_cache();
    test_shader_entry_due();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("draw_record: ok\n");
    return 0;
}
