/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Textures from the guest's texture headers (TIC, 32 bytes each), and samplers from its sampler headers (TSC). A
   texture whose address holds a render target is that render target (a copy of its top-left corner when the header
   is smaller). Others are loaded from guest memory, staged as they are and deswizzled and converted on the GPU
   (vk/blocklinear.c, vk/texconv.c, vk/astc.c), and checked for changes now and then: often at first, rarely once
   they have stayed the same. */
#include "common/utilities/bits.h"
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/renderer.h"
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"
#include "nx/memory/own.h"
#include "nx/runtime/platform/tracy.h"

/* the block height and depth (log2 GOBs) of a mip level of a block-linear texture. The hardware shrinks them for
   small levels: the height from the base level on, the depth only from level 1 on and by one step a level
   (Ryujinx's SizeCalculator) */
static void bl_level_blocks(u32 h, u32 d, const VkFmt* f, u32 bh, u32 bd, u32 level, u32* lbh, u32* lbd) {
    u32 lh = h >> level ? h >> level : 1, gy = ((lh + f->bh - 1) / f->bh + 7) / 8;
    while (bh > 0 && gy <= (1u << (bh - 1))) bh--;
    for (u32 l = 1; l <= level; l++) {
        u32 ld = d >> l ? d >> l : 1;
        if (bd > 0 && ld <= (1u << (bd - 1))) bd--;
    }
    *lbh = bh;
    *lbd = bd;
}
/* bytes of one mip level of a block-linear texture; lbh and lbd (may be NULL) get its block height and depth */
static u64 bl_level_size(u32 w, u32 h, u32 d, const VkFmt* f, u32 bh, u32 bd, u32 level, u32* lbh, u32* lbd) {
    u32 lw = w >> level ? w >> level : 1, lh = h >> level ? h >> level : 1, ld = d >> level ? d >> level : 1;
    u32 tw = (lw + f->bw - 1) / f->bw, th = (lh + f->bh - 1) / f->bh;
    u32 gx = (tw * f->bpp + 63) / 64, gy = (th + 7) / 8, b, bdd;
    bl_level_blocks(h, d, f, bh, bd, level, &b, &bdd);
    if (lbh) *lbh = b;
    if (lbd) *lbd = bdd;
    u64 gys = ((gy + (1u << b) - 1) >> b) << b;
    u64 lds = ((ld + (1u << bdd) - 1) >> bdd) << bdd;
    return (u64)gx * gys * lds * 512;
}
/* bytes from one layer of a block-linear texture to the next: all its levels, aligned to a block of GOBs that is
   shrunk for a small base level (Ryujinx's AlignLayerSize) */
static u64 bl_layer_size(u32 w, u32 h, u32 d, const VkFmt* f, u32 bh, u32 bd, u32 levels) {
    /* the sizes asked last, by shape: every lookup of a texture asks this, and it costs divisions by the block size
       over each level, the levels' block sizes each a loop of its own */
    static struct {
        u32 w, h, d, bh, bd, levels, fbw, fbh, fbpp;
        u64 sz;
    } memo[256];
    u32 m = (u32)(((((u64)w * 0x9E3779B1u) ^ h) * 0x85EBCA77u ^ ((u64)d << 20 | (u64)levels << 14 | bh << 8 | bd << 4) ^
                   (u64)f->bpp << 24 ^ (u64)f->bw << 28 ^ (u64)f->bh << 32) *
                      0x9E3779B97F4A7C15ull >>
                  56);
    if (memo[m].w == w && memo[m].h == h && memo[m].d == d && memo[m].bh == bh && memo[m].bd == bd &&
        memo[m].levels == levels && memo[m].fbw == f->bw && memo[m].fbh == f->bh && memo[m].fbpp == f->bpp && w)
        return memo[m].sz;
    u32 w0 = w, h0 = h, d0 = d, bh0 = bh, bd0 = bd;
    u64 sz = 0;
    for (u32 l = 0; l < levels; l++) sz += bl_level_size(w, h, d, f, bh, bd, l, NULL, NULL);
    u32 gy = ((h + f->bh - 1) / f->bh + 7) / 8;
    while (bh > 0 && gy <= (1u << (bh - 1))) bh--;
    while (bd > 0 && d <= (1u << (bd - 1))) bd--;
    u64 al = 512ull << (bh + bd);
    sz = (sz + al - 1) / al * al;
    memo[m].w = w0, memo[m].h = h0, memo[m].d = d0, memo[m].bh = bh0, memo[m].bd = bd0, memo[m].levels = levels;
    memo[m].fbw = f->bw, memo[m].fbh = f->bh, memo[m].fbpp = f->bpp, memo[m].sz = sz;
    return sz;
}

/* the guest bytes a move of a level reads (lsz bytes at gva) staged as they are (what the GPU holds of them copied on
   the GPU, vk_guest_read), zero past the level's end: buf and off hold the layout's first byte the move reads
   (vk_bl_range) */
static int stage_bl(const VkBlMove* m, u64 gva, u64 lsz, VkBuffer* buf, u64* off) {
    u64 lo, hi;
    vk_bl_range(m, &lo, &hi);
    u8* p = vk_stage(hi - lo, vk_stage_align(), buf, off);
    if (!p) return 0;
    u64 n = lo >= lsz ? 0 : hi <= lsz ? hi - lo : lsz - lo;
    vk_guest_read(gva + lo, n, p, *buf, *off);
    memset(p + n, 0, hi - lo - n);
    return 1;
}

/* one level of one layer in guest memory, for vk_astc_upload to ask for its block rows */
typedef struct {
    Surf* t;
    u64 gva;
    u64 lsz;
    u32 rowb, th, lbh, lbd;
} LevelSource;
/* A guest read while staging lands GPU results, and landing can submit the frame's command buffer: the upload's
   commands then belong to the one recording after it, with the image's transfer use established there again */
static VkCommandBuffer upload_cb(Surf* t, VkCommandBuffer cb) {
    VkCommandBuffer now = vk_cmd();
    if (now == cb) return cb;
    vk_surf_use(now, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
    return now;
}
/* block linear blocks staged as they are and deswizzled on the GPU into the scratch buffer; pitch linear ones staged
   a row at a time */
static int astc_rows(void* ctx, VkCommandBuffer* pcb, u32 z, u32 nz, u32 y, u32 n, VkBuffer* buf, u64* off) {
    LevelSource* l = ctx;
    if (l->t->bl) {
        VkBlMove m = {l->rowb, l->th, l->lbh, l->lbd, y, n, z, nz, l->rowb}; /* 16 byte blocks: rows of whole runs */
        VkBuffer staged;
        u64 at;
        u64 cap;
        *buf = vk_bl_scratch((u64)l->rowb * n * nz, &cap);
        *off = 0;
        if (!*buf || !stage_bl(&m, l->gva, l->lsz, &staged, &at)) return 0;
        VkCommandBuffer cb = *pcb = upload_cb(l->t, *pcb);
        vk_bl_barrier(cb, 1);
        int ok = vk_bl_move(cb, &m, 0, staged, at, *buf, 0);
        vk_bl_barrier(cb, 0);
        return ok;
    }
    u8* p = vk_stage((u64)l->rowb * n * nz, vk_stage_align(), buf, off);
    if (!p) return 0;
    for (u32 k = 0; k < nz; k++)
        for (u32 r = 0; r < n; r++) {
            u64 at = ((u64)(z + k) * l->th + y + r) * l->t->pitch;
            u64 to = ((u64)k * n + r) * l->rowb;
            vk_guest_read(l->gva + at, l->rowb, p + to, *buf, *off + to);
        }
    *pcb = upload_cb(l->t, *pcb);
    return 1;
}

/* A texture's upload, recorded in batches that share their barriers: a batch's block linear strips are deswizzled
   into the scratch buffer one after another, then the strips of a format the host can't take as it is are converted
   there too, then every strip is copied into the image. The CPU only copies guest memory into staging, in whole
   ranges: GOBs and rows are never taken apart on it */
typedef struct {
    VkBuffer in;
    u64 in_at, out_at;
    u32 in_pitch, w, rows, slices;
} UpConv;
typedef struct {
    VkCommandBuffer cb;
    Surf* t;
    VkBuffer scratch;
    u64 cap, used;
    int open;        /* the barrier before the batch's dispatches is recorded */
    u32 moves;       /* deswizzles recorded in the batch */
    UpConv conv[32]; /* conversions, recorded once the moves they read are done */
    u32 nconv;
    VkBufferImageCopy reg[64];
    VkBuffer src[64]; /* each copy's buffer: the scratch buffer, or staging */
    u32 nreg;
} Upload;
/* the buffer a batch records into now: staging can submit, which ends the one the batch was opened on */
static VkCommandBuffer up_cb(Upload* u) {
    VkCommandBuffer cb = upload_cb(u->t, u->cb);
    if (cb != u->cb) {
        u->cb = cb;
        u->open = 0; /* the batch's barriers ended with the buffer that held them */
    }
    return cb;
}
static void up_open(Upload* u) {
    up_cb(u);
    if (!u->open) vk_bl_barrier(u->cb, 1);
    u->open = 1;
}
static int up_flush(Upload* u) {
    int ok = 1;
    up_cb(u);
    if (u->moves) vk_bl_barrier(u->cb, 0);
    if (u->nconv) {
        up_open(u);
        for (u32 i = 0; i < u->nconv && ok; i++)
            ok = vk_tc_convert(u->cb, &u->t->f, u->conv[i].w, u->conv[i].rows, u->conv[i].slices, u->conv[i].in,
                               u->conv[i].in_at, u->conv[i].in_pitch, u->scratch, u->conv[i].out_at);
        vk_bl_barrier(u->cb, 0);
    }
    for (u32 i = 0, j; i < u->nreg; i = j) {
        for (j = i + 1; j < u->nreg && u->src[j] == u->src[i];) j++;
        rec_CmdCopyBufferToImage(u->cb, u->src[i], u->t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, j - i, u->reg + i);
    }
    u->open = 0;
    u->moves = u->nconv = u->nreg = 0;
    u->used = 0;
    return ok;
}
/* room in the batch for a strip: size bytes of the scratch buffer (its pieces each aligned to 16 or less), two
   copies and a conversion. The batch is flushed first when they don't fit, and the scratch buffer grown when even an
   empty one wouldn't hold them, so the strip's pieces stay in one batch and one buffer. 0 when memory ran out */
static int up_reserve(Upload* u, u64 size) {
    if ((!size || u->used + 64 + size <= u->cap) && u->nreg + 2 <= sizeof u->reg / sizeof u->reg[0] &&
        u->nconv < sizeof u->conv / sizeof u->conv[0])
        return 1;
    if (!up_flush(u)) return 0;
    return !size || size + 64 <= u->cap || (u->scratch = vk_bl_scratch(size + 64, &u->cap)) != VK_NULL_HANDLE;
}
/* size bytes of the scratch buffer at a multiple of align, within what up_reserve made room for */
static u64 up_space(Upload* u, u64 size, u32 align) {
    u64 at = (u->used + align - 1) / align * align;
    u->used = at + size;
    return at;
}
/* the least common multiple of 16 and a texel's (or block's) bytes */
static u32 bl_lin_align(u32 bpp) {
    u32 a = 16;
    while (a % bpp) a += 16;
    return a;
}

/* one level of one layer, what tex_upload's strips of it share */
typedef struct {
    u32 level, layer, lw, lh, ld, rowb, th;
    u32 conv;     /* converted on the GPU (vk_tc_converts) */
    VkTcShape tc; /* the conversion's shape */
    u32 pitch;    /* the linear rows' pitch: deswizzled, or repacked from a pitch linear level */
} UpLevel;
/* units rows y to y + n (rows of texels, or of blocks) of slices z to z + nz, in linear rows in_pitch bytes apart at
   in_at in in (staging, or the scratch buffer once the batch's moves are done), converted when they need to be and
   copied into the image. The caller has reserved the room (up_reserve) */
static int up_rows(Upload* u, const UpLevel* lv, VkBuffer in, u64 in_at, u32 in_pitch, u32 z, u32 nz, u32 y, u32 n) {
    const VkFmt* f = &u->t->f;
    u32 top = y * f->bh, height = lv->lh - top < n * f->bh ? lv->lh - top : n * f->bh;
    VkBufferImageCopy r = {.bufferRowLength = in_pitch / f->bpp * f->bw,
                           .bufferImageHeight = n * f->bh,
                           .imageSubresource = {f->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                                                lv->level, lv->layer, 1},
                           .imageOffset = {0, (i32)top, (i32)z},
                           .imageExtent = {lv->lw, height, nz}};
    if (!lv->conv) {
        r.bufferOffset = in_at;
        u->src[u->nreg] = in;
        u->reg[u->nreg++] = r;
        return 1;
    }
    u64 out = up_space(u, vk_tc_out_size(&lv->tc, n, nz), 16);
    u->conv[u->nconv++] = (UpConv){in, in_at, out, in_pitch, lv->lw, n, nz};
    r.bufferOffset = out;
    r.bufferRowLength = lv->tc.out_texels;
    u->src[u->nreg] = u->scratch;
    u->reg[u->nreg++] = r;
    if (lv->tc.stencil) {
        r.bufferOffset = out + (u64)lv->tc.out_texels * n * nz * 4;
        r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        u->src[u->nreg] = u->scratch;
        u->reg[u->nreg++] = r;
    }
    return 1;
}
/* a block linear strip: move m of bytes staged at staged_at deswizzled into the scratch buffer, then up_rows. Its
   rows land at row y, slice z of the level (the move's own rows and slices are a gathered layout's for a strip of
   GOB rows) */
static int up_move(Upload* u, const UpLevel* lv, const VkBlMove* m, VkBuffer staged, u64 staged_at, u32 z, u32 y) {
    u64 lin = (u64)m->slices * m->rows * m->lin_pitch;
    if (!up_reserve(u, lin + (lv->conv ? vk_tc_out_size(&lv->tc, m->rows, m->slices) : 0))) return 0;
    u64 at = up_space(u, lin, bl_lin_align(u->t->f.bpp));
    up_open(u);
    if (!vk_bl_move(u->cb, m, 0, staged, staged_at, u->scratch, at)) return 0;
    u->moves++;
    return up_rows(u, lv, u->scratch, at, m->lin_pitch, z, m->slices, y, m->rows);
}

/* a block linear level: its guest bytes staged as they are and deswizzled on the GPU. The whole level at once when
   it fits in band; else strips of whole rows of blocks of 2^lbd slices, contiguous in memory; else, when one of those
   is too big, strips of 2^k GOB rows of a slice, each block's piece of them (contiguous in it) staged after the last
   one's: the layout of a surface of one row of blocks 2^k GOBs high */
static int level_upload_bl(Upload* u, const UpLevel* lv, u64 gva, u64 lsz, u32 lbh, u32 lbd, u64 band) {
    u32 rowb = lv->rowb, th = lv->th, ld = lv->ld, pitch = lv->pitch;
    u32 gobs_x = (rowb + 63) >> 6, nz = ld < (1u << lbd) ? ld : 1u << lbd;
    u64 block = 512ull << (lbh + lbd), block_row = gobs_x * block;
    /* scratch bytes a row of units takes: deswizzled, and converted */
    u64 per_row = pitch + (lv->conv ? vk_tc_out_size(&lv->tc, 1, 1) : 0);
    VkBuffer staged;
    u64 staged_at;
    if (per_row * th * ld <= band && lsz <= band) {
        VkBlMove m = {rowb, th, lbh, lbd, 0, th, 0, ld, pitch};
        return stage_bl(&m, gva, lsz, &staged, &staged_at) && up_move(u, lv, &m, staged, staged_at, 0, 0);
    }
    u64 rows_cost = per_row * (8u << lbh) * nz;
    if (rows_cost <= band && block_row <= band) {
        u64 k = band / (rows_cost > block_row ? rows_cost : block_row);
        u32 n = (u32)k * (8u << lbh);
        for (u32 z = 0; z < ld; z += nz)
            for (u32 y = 0; y < th; y += n) {
                VkBlMove m = {rowb, th, lbh, lbd, y, th - y < n ? th - y : n, z, ld - z < nz ? ld - z : nz, pitch};
                if (!stage_bl(&m, gva, lsz, &staged, &staged_at) ||
                    !up_move(u, lv, &m, staged, staged_at, z, y))
                    return 0;
            }
        return 1;
    }
    u32 k = lbh;
    while (k && (per_row * (8u << k) > band || (u64)gobs_x * (512u << k) > band)) k--;
    if (per_row * 8 > band || (u64)gobs_x * 512 > band) {
        LOG_LIMIT(1, "vulkan: a %ux%u texture level's rows exceed the staging memory (left out)\n", lv->lw, lv->lh);
        return 0;
    }
    u64 rows_blocks = ((((u64)th + 7) >> 3) + (1u << lbh) - 1) >> lbh, piece = 512ull << k;
    for (u32 z = 0; z < ld; z++)
        for (u32 gy = 0; gy < (th + 7) >> 3; gy += 1u << k) {
            u8* p = vk_stage(gobs_x * piece, vk_stage_align(), &staged, &staged_at);
            if (!p) return 0;
            u64 base = (z >> lbd) * rows_blocks * block_row + (gy >> lbh) * block_row +
                       ((((u64)(z & ((1u << lbd) - 1)) << lbh) + (gy & ((1u << lbh) - 1))) << 9);
            for (u32 gx = 0; gx < gobs_x; gx++) {
                u64 o = base + gx * block, n = o >= lsz ? 0 : o + piece <= lsz ? piece : lsz - o;
                vk_guest_read(gva + o, n, p + gx * piece, staged, staged_at + gx * piece);
                memset(p + gx * piece + n, 0, piece - n);
            }
            u32 y = gy << 3;
            VkBlMove m = {rowb, 8u << k, k, 0, 0, th - y < (8u << k) ? th - y : 8u << k, 0, 1, pitch};
            if (!up_move(u, lv, &m, staged, staged_at, z, y)) return 0;
        }
    return 1;
}

/* a pitch linear level: its rows staged as they are, whole slices or strips of rows of a slice, when the guest's
   pitch suits the copies and the conversion; else each row into rows of lv->pitch */
static int level_upload_pitch(Upload* u, const UpLevel* lv, u64 gva, u64 lsz, u64 band) {
    const Surf* t = u->t;
    u32 th = lv->th, ld = lv->ld, gp = t->pitch;
    int as_is = gp % (lv->conv ? lv->tc.in_align : 4) == 0 && gp % t->f.bpp == 0;
    u32 pitch = as_is ? gp : lv->pitch;
    u64 out_row = lv->conv ? vk_tc_out_size(&lv->tc, 1, 1) : 0, per_row = pitch > out_row ? pitch : out_row;
    u32 n = per_row * th * ld <= band ? th : (u32)(band / per_row ? band / per_row : 1);
    u32 nz = n == th ? ld : 1;
    if (n > th) n = th;
    for (u32 z = 0; z < ld; z += nz)
        for (u32 y = 0; y < th; y += n) {
            u32 rows = th - y < n ? th - y : n, slices = ld - z < nz ? ld - z : nz;
            VkBuffer staged;
            u64 at, first = ((u64)z * th + y) * gp, size = (u64)rows * slices * pitch;
            u8* p = vk_stage(size, vk_stage_align(), &staged, &at);
            if (!p) return 0;
            if (as_is) { /* the rows, slices after another, are one range of guest memory */
                u64 have = first >= lsz ? 0 : first + size <= lsz ? size : lsz - first;
                vk_guest_read(gva + first, have, p, staged, at);
                memset(p + have, 0, size - have);
            } else
                for (u32 k = 0; k < slices; k++)
                    for (u32 r = 0; r < rows; r++) {
                        u64 o = ((u64)(z + k) * th + y + r) * gp;
                        u64 to = ((u64)k * rows + r) * pitch;
                        memset(p + to, 0, pitch);
                        vk_guest_read(gva + o, lv->rowb, p + to, staged, at + to);
                    }
            if (!up_reserve(u, lv->conv ? vk_tc_out_size(&lv->tc, rows, slices) : 0) ||
                !up_rows(u, lv, staged, at, pitch, z, slices, y, rows))
                return 0;
        }
    return 1;
}

/* the most staging one piece of a level takes: a bigger level is staged in strips, as no allocation may be bigger
   than g_vk.alloc_cap (memory.c) */
#define TEX_BAND (64ull << 20)

static u64 g_tex_uploads, g_tex_upload_bytes; /* for the stats line */
void vk_tex_stats(u64* uploads, u64* bytes) {
    *uploads = g_tex_uploads;
    *bytes = g_tex_upload_bytes;
}
/* 1 when every subresource was copied; 0 when memory or staging ran out, and upload_valid keeps it for a retry */
static int tex_upload_(Surf* t);
/* a texture's upload as a profiler zone (SNX_TRACY builds) */
static int tex_upload(Surf* t) {
    SNX_ZONE_V(z, "texture upload", t->size);
    int r = tex_upload_(t);
    SNX_ZONE_END(z);
    return r;
}
static int tex_upload_(Surf* t) {
    const VkFmt* f = &t->f;
    SNX_ZONE_T(z_up, "texture upload", "%ux%u", t->w, t->h);
    SNX_ZONE_VALUE(z_up, t->size);
    g_tex_uploads++;
    g_tex_upload_bytes += t->size;
    t->upload_valid = 0;
    t->wgen = vk_surf_gen();      /* even a new texture at a reused address invalidates an old target's copy */
    t->took_drawn = 0;            /* memory's own texels again, until tex_take_drawn copies targets in */
    vk_surf_relist(t);
    vk_render_end();              /* copies and clears happen outside rendering passes */
    VkCommandBuffer cb = vk_cmd();
    vk_surf_use(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
    int is3d = t->type == VK_IMAGE_VIEW_TYPE_3D;
    u32 layers = t->layers, d3 = is3d ? t->depth3d : 1;
    u64 band = g_vk.alloc_cap < TEX_BAND ? g_vk.alloc_cap : TEX_BAND;
    int complete = 1;
    Upload u = {.cb = cb, .t = t};
    for (u32 layer = 0; layer < layers && complete; layer++) {
        u64 at = layer * t->layer_size;
        for (u32 l = 0; l < t->levels && complete; l++) {
            u32 lw = t->w >> l, lh = t->h >> l, ld = d3 >> l;
            if (!lw) lw = 1;
            if (!lh) lh = 1;
            if (!ld) ld = 1;
            u32 tw = (lw + f->bw - 1) / f->bw, th = (lh + f->bh - 1) / f->bh;
            u32 rowb = tw * f->bpp, lbh = 0, lbd = 0;
            u64 lsz = t->bl ? bl_level_size(t->w, t->h, d3, f, t->bh, t->bd, l, &lbh, &lbd) : (u64)t->pitch * th * ld;
            if (f->conv == VKC_ASTC) { /* decoded on the GPU from its block rows, deswizzled on the GPU */
                complete = up_flush(&u);
                LevelSource ls = {t, t->gva + at, lsz, rowb, th, lbh, lbd};
                complete = complete && vk_astc_upload(u.cb, t, lw, lh, ld, l, layer, astc_rows, &ls);
                u.scratch = VK_NULL_HANDLE; /* astc_rows may have grown the scratch buffer: asked for again */
                u.cap = 0;
            } else {
                UpLevel lv = {l, layer, lw, lh, ld, rowb, th, (u32)vk_tc_converts(f)};
                u32 align = bl_lin_align(f->bpp);
                if (lv.conv) {
                    lv.tc = vk_tc_shape(f, lw);
                    if (lv.tc.in_align > align) align = lv.tc.in_align;
                }
                lv.pitch = (rowb + align - 1) / align * align;
                complete = t->bl ? level_upload_bl(&u, &lv, t->gva + at, lsz, lbh, lbd, band)
                                 : level_upload_pitch(&u, &lv, t->gva + at, lsz, band);
            }
            if (!complete)
                LOG_LIMIT(1, "vulkan: out of memory or staging uploading a %ux%u texture level (left out)\n", lw, lh);
            at += lsz;
        }
    }
    complete = up_flush(&u) && complete;
    return t->upload_valid = complete;
}

