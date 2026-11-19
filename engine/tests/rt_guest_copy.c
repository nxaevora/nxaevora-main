/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PASS_GUEST's copies in a render target's copy list (vk/texture.c rt_crop and vk_rt_guest), at render
   scale 2: a guest copy, a snapshot and a crop of one target are three entries with their own extents. A guest copy is
   one linear blit of a region kept inside the target, refreshed only when the target was drawn to, pruned as crops
   are, and says nothing where a smaller crop logs. Integer formats, no linear blits and no memory fall back */
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wmissing-field-initializers" /* texture.c builds without -Wextra */
#include "nx/gpu/vk/texture.c"

static unsigned checks;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

/* the kind fits in padding: the bytes after crop_gen up to next held no field before it */
_Static_assert(offsetof(Surf, guest_copy) == offsetof(Surf, crop_gen) + sizeof(u32), "guest_copy follows crop_gen");
_Static_assert(offsetof(Surf, next) >= offsetof(Surf, guest_copy) + 1, "guest_copy ends before next");
_Static_assert(offsetof(Surf, next) == (offsetof(Surf, crop_gen) + sizeof(u32) + _Alignof(Surf*) - 1) /
                                           _Alignof(Surf*) * _Alignof(Surf*),
               "next is where it was without guest_copy");

/* ------------------------------------------------ what the copy list records: blits and copies, images made */
static u32 nblits, ncopies, images, image_fails;
static VkImageBlit last_blit;
static VkImageCopy last_copy;
static VkFilter last_filter;
static VkImage last_blit_src, last_blit_dst;
static VkImageCreateInfo last_image;
void rec_CmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst, VkImageLayout dl, u32 n,
                      const VkImageBlit* r, VkFilter filter) {
    (void)cb;
    CHECK(n == 1 && sl == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && dl == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    last_blit = r[0];
    last_filter = filter;
    last_blit_src = src;
    last_blit_dst = dst;
    nblits++;
}
void rec_CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst, VkImageLayout dl, u32 n,
                      const VkImageCopy* r) {
    (void)cb, (void)src, (void)sl, (void)dst, (void)dl, (void)n;
    last_copy = r[0];
    ncopies++;
}
int vk_cache_image_create(const VkImageCreateInfo* ii, VkImage* img, VmaAllocation* mem, u64* bytes) {
    if (image_fails) return 0;
    last_image = *ii;
    *img = (VkImage)(uintptr_t)(0x1000 + ++images);
    *mem = NULL;
    *bytes = (u64)ii->extent.width * ii->extent.height * 8;
    return 1;
}
void vk_view_formats(VkImageCreateInfo* ii, VkViewFormats* vf) { (void)ii, (void)vf; }
/* format features: every format blits and filters but no_blit (neither) and no_filter (blits, no linear filter) */
static VkFormat no_blit = VK_FORMAT_UNDEFINED, no_filter = VK_FORMAT_UNDEFINED;
static u32 format_asks;
static void VKAPI_CALL format_props(VkPhysicalDevice pd, VkFormat f, VkFormatProperties* p) {
    (void)pd;
    format_asks++;
    p->optimalTilingFeatures = f == no_blit     ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                               : f == no_filter ? VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
                                                : VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
}
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = format_props;
static u32 blits_counted;
void pass_guest_blit(void) { blits_counted++; }

/* ------------------------------------------------ what texture.c reaches otherwise, unused here */
VkState g_vk;
_Atomic int g_cs_any;
u32 g_vk_present;
/* the log, kept: the crop's "only that region is used" line is checked */
static char g_log[8192];
void logf_(int lvl, const char* fmt, ...) {
    va_list ap;
    size_t at = strlen(g_log);
    (void)lvl;
    va_start(ap, fmt);
    if (at + 1 < sizeof g_log) vsnprintf(g_log + at, sizeof g_log - at, fmt, ap);
    va_end(ap);
}
int vk_ok_(VkResult r, const char* what, const char* file, int line) {
    (void)what, (void)file, (void)line;
    return r == VK_SUCCESS;
}
VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)(uintptr_t)0xCB; }
void vk_render_end(void) {}
void vk_surf_use(VkCommandBuffer cb, Surf* s, VkImageLayout layout, VkPipelineStageFlags2 stage_,
                 VkAccessFlags2 access) {
    (void)cb, (void)stage_, (void)access;
    s->layout = layout;
}
u32 vk_sc(u32 v) { return v ? v * 2 : 1; } /* render scale 2 */
void vk_defer_image(VkImage img, VmaAllocation a, VkImageView v1, VkImageView v2) {
    (void)img, (void)a, (void)v1, (void)v2;
}
void vk_defer_bytes(u64 bytes) { (void)bytes; }
void vk_surf_add(Surf* s) { (void)s; }
/* uploads, guest memory, the surface table and the rest texture.c calls on paths this test doesn't take */
int g_own_any;
u8* gmem_host(u64 gva, u64 size) { return (void)gva, (void)size, NULL; }
u8* gmem_ptr(u64 gva, u64 size) { return (void)gva, (void)size, NULL; }
int gpu_mem_dirty_since(u64 since, u64 a, u64 sz) { return (void)since, (void)a, (void)sz, 0; }
u64 gpu_mem_generation(void) { return 0; }
int vk_astc_upload(VkCommandBuffer cb, Surf* t, u32 w, u32 h, u32 depth, u32 level, u32 layer, VkAstcRows rows,
                   void* ctx) {
    return (void)cb, (void)t, (void)w, (void)h, (void)depth, (void)level, (void)layer, (void)rows, (void)ctx, 0;
}
void vk_bl_barrier(VkCommandBuffer cb, int before) { (void)cb, (void)before; }
int vk_bl_move(VkCommandBuffer cb, const VkBlMove* m, int to_bl, VkBuffer bl, u64 bl_at, VkBuffer lin, u64 lin_at) {
    return (void)cb, (void)m, (void)to_bl, (void)bl, (void)bl_at, (void)lin, (void)lin_at, 0;
}
void vk_bl_range(const VkBlMove* m, u64* lo, u64* hi) { (void)m, *lo = *hi = 0; }
VkBuffer vk_bl_scratch(u64 size, u64* cap) { return (void)size, *cap = 0, VK_NULL_HANDLE; }
void vk_guest_peek(u64 gva, void* dst, u64 size) { (void)gva, memset(dst, 0, size); }
void vk_guest_read(u64 gva, u64 size, u8* host, VkBuffer buf, u64 at) {
    (void)gva, (void)buf, (void)at, memset(host, 0, size);
}
u32 vk_present_before(u64 ago_ns, u32 presents) { return (void)ago_ns, (void)presents, 0; }
int vk_rt_drawn_fresh(Surf* s) { return (void)s, 0; }
int vk_rt_drawn_in(u64 lo, u64 hi, Surf** out, int max) { return (void)lo, (void)hi, (void)out, (void)max, 0; }
Surf* vk_rt_group_for(u64 gva, u64 stride, u32 layers, u32 w, u32 h, const VkFmt* f) {
    return (void)gva, (void)stride, (void)layers, (void)w, (void)h, (void)f, NULL;
}
Surf* vk_rt_find(u64 gva) { return (void)gva, NULL; }
void vk_rt_note_read(Surf* s) { (void)s; }
u64 vk_sample_hash(u64 gva, u64 len) { return (void)gva, len; }
u8* vk_stage(u64 size, u64 align, VkBuffer* buf, u64* off) {
    return (void)size, (void)align, (void)buf, (void)off, NULL;
}
u64 vk_stage_align(void) { return 64; }
Surf* vk_surf_first(u64 gva) { return (void)gva, NULL; }
u64 vk_surf_reclaim(int kind, u32 before, u64 want, int lossy) {
    return (void)kind, (void)before, (void)want, (void)lossy, 0;
}
u64 vk_rt_reclaim_superseded(u32 before, u64 want) { return (void)before, (void)want, 0; }
void vk_surf_touch(Surf* s) { (void)s; }
void vk_surf_relist(Surf* s) { (void)s; }
u32 vk_surf_gen(void) { return 1; }
int vk_tc_convert(VkCommandBuffer cb, const VkFmt* f, u32 w, u32 rows, u32 slices, VkBuffer in, u64 in_at,
                  u32 in_pitch, VkBuffer out, u64 out_at) {
    return (void)cb, (void)f, (void)w, (void)rows, (void)slices, (void)in, (void)in_at, (void)in_pitch, (void)out,
           (void)out_at, 0;
}
int vk_tc_converts(const VkFmt* f) { return (void)f, 0; }
u64 vk_tc_out_size(const VkTcShape* s, u32 rows, u32 slices) { return (void)s, (void)rows, (void)slices, 0; }
VkTcShape vk_tc_shape(const VkFmt* f, u32 w) {
    VkTcShape s;
    memset(&s, 0, sizeof s);
    return (void)f, (void)w, s;
}
VkFmt vk_tic_format(u32 w0, int srgb) {
    VkFmt f;
    memset(&f, 0, sizeof f);
    return (void)w0, (void)srgb, f;
}
VkFmt vk_tic_format_3d(u32 w0, int srgb) { return vk_tic_format(w0, srgb); }
VkImage vk_unscaled_image(const Surf* s, u32 layers, VmaAllocation* mem) {
    return (void)s, (void)layers, (void)mem, VK_NULL_HANDLE;
}
int vk_written_since(const u8* host, u64 len, u32 since) { return (void)host, (void)len, (void)since, 0; }
u32 vk_ww_epoch(void) { return 0; }
u32 vk_ww_now(void) { return 0; }
VkImageAspectFlags vk_aspect(const VkFmt* f) {
    return f->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}
u64 vk_hash(const void* p, u64 size) { return (void)p, size; }
VkFormat vk_srgb_as(VkFormat f, int srgb) { return (void)srgb, f; }
void vk_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkPipelineStageFlags2 src_stage,
                VkAccessFlags2 src_access, VkImageLayout from, VkPipelineStageFlags2 dst_stage,
                VkAccessFlags2 dst_access, VkImageLayout to) {
    (void)cb, (void)img, (void)aspect, (void)src_stage, (void)src_access, (void)from, (void)dst_stage, (void)dst_access,
        (void)to;
}
void vk_compute_mem_need(u64 a, u64 n) { (void)a, (void)n; }
void vk_defer_buffer(VkBuffer b, VmaAllocation a) { (void)b, (void)a; }
void vk_finish(void) {}
void rec_CmdClearColorImage(VkCommandBuffer cb, VkImage img, VkImageLayout l, const VkClearColorValue* c, u32 n,
                            const VkImageSubresourceRange* r) {
    (void)cb, (void)img, (void)l, (void)c, (void)n, (void)r;
}
void rec_CmdClearDepthStencilImage(VkCommandBuffer cb, VkImage img, VkImageLayout l, const VkClearDepthStencilValue* c,
                                   u32 n, const VkImageSubresourceRange* r) {
    (void)cb, (void)img, (void)l, (void)c, (void)n, (void)r;
}
void rec_CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, VkImageLayout dl, u32 n,
                              const VkBufferImageCopy* r) {
    (void)cb, (void)src, (void)dst, (void)dl, (void)n, (void)r;
}
void rec_CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkBuffer dst, u32 n,
                              const VkBufferImageCopy* r) {
    (void)cb, (void)src, (void)sl, (void)dst, (void)n, (void)r;
}
void rec_CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* d) { (void)cb, (void)d; }
PFN_vkCreateImageView vkCreateImageView;
PFN_vkCreateSampler vkCreateSampler;
VkResult vmaCreateBuffer(VmaAllocator a, const VkBufferCreateInfo* bi, const VmaAllocationCreateInfo* ai, VkBuffer* buf,
                         VmaAllocation* mem, VmaAllocationInfo* info) {
    return (void)a, (void)bi, (void)ai, (void)buf, (void)mem, (void)info, VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
VkResult vmaCreateImage(VmaAllocator a, const VkImageCreateInfo* ii, const VmaAllocationCreateInfo* ai, VkImage* img,
                        VmaAllocation* mem, VmaAllocationInfo* info) {
    return (void)a, (void)ii, (void)ai, (void)img, (void)mem, (void)info, VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
VkResult vmaInvalidateAllocation(VmaAllocator a, VmaAllocation m, VkDeviceSize offset, VkDeviceSize size) {
    return (void)a, (void)m, (void)offset, (void)size, VK_SUCCESS;
}

/* a scaled color target of guest size w x h, drawn once */
static Surf* target(u32 w, u32 h, VkFormat vk) {
    Surf* t = calloc(1, sizeof *t);
    CHECK(t);
    t->gva = 0x47306a000ull;
    t->w = w, t->h = h, t->sw = vk_sc(w), t->sh = vk_sc(h);
    t->layers = t->levels = t->depth3d = 1;
    t->f.vk = vk;
    t->f.bpp = 8;
    t->f.bw = t->f.bh = 1;
    t->is_rt = 1;
    t->img = (VkImage)(uintptr_t)0xA11;
    t->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    t->wgen = 7;
    return t;
}
static void drop(Surf* t) {
    for (Surf* x = t->crop; x;) {
        Surf* next = x->next;
        free(x);
        x = next;
    }
    free(t);
}
static u32 entries(const Surf* t) {
    u32 n = 0;
    for (const Surf* x = t->crop; x; x = x->next) n++;
    return n;
}

int main(void) {
    const VkFormat F = VK_FORMAT_R16G16B16A16_SFLOAT;
    int why = -1;
    g_vk_present = 100;

    /* a guest copy of T, a snapshot of T in the same frame, a crop at another size: three entries */
    Surf* t = target(960, 540, F);
    Surf* g = vk_rt_guest(t, 960, 540, &why);
    CHECK(g && g != t && g->guest_copy == 1 && g->w == 960 && g->h == 540 && g->sw == 960 && g->sh == 540);
    CHECK(last_image.extent.width == 960 && last_image.extent.height == 540 && last_image.format == F);
    CHECK(nblits == 1 && ncopies == 0 && blits_counted == 1 && last_filter == VK_FILTER_LINEAR);
    CHECK(last_blit_src == t->img && last_blit_dst == g->img);
    CHECK(last_blit.srcOffsets[0].x == 0 && last_blit.srcOffsets[0].y == 0 && last_blit.srcOffsets[1].x == 1920 &&
          last_blit.srcOffsets[1].y == 1080 && last_blit.srcOffsets[1].z == 1);
    CHECK(last_blit.dstOffsets[1].x == 960 && last_blit.dstOffsets[1].y == 540 && last_blit.dstOffsets[1].z == 1);
    CHECK(last_blit.srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && last_blit.srcSubresource.layerCount == 1);
    Surf* s = vk_rt_snapshot(t);
    CHECK(s && s != t && s != g && s->guest_copy == 0 && s->sw == 1920 && s->sh == 1080 && s->w == 960);
    CHECK(ncopies == 1 && nblits == 1 && last_copy.extent.width == 1920 && last_copy.extent.height == 1080);
    Surf* c = rt_crop(t, 480, 270, NULL, 0);
    CHECK(c && c != g && c != s && c->guest_copy == 0 && c->sw == 960 && c->sh == 540 && c->w == 480);
    CHECK(ncopies == 2 && nblits == 1 && last_copy.extent.width == 960 && last_copy.extent.height == 540);
    CHECK(entries(t) == 3);
    /* each found again as itself, none refreshed while the target is not drawn to */
    CHECK(vk_rt_guest(t, 960, 540, &why) == g && vk_rt_snapshot(t) == s && rt_crop(t, 480, 270, NULL, 0) == c);
    CHECK(nblits == 1 && ncopies == 2 && entries(t) == 3);
    /* drawn to: the guest copy is blitted again and the snapshot copied again, each only by its own kind */
    t->wgen++;
    CHECK(vk_rt_guest(t, 960, 540, &why) == g && nblits == 2 && ncopies == 2 && blits_counted == 2);
    CHECK(vk_rt_snapshot(t) == s && nblits == 2 && ncopies == 3);

    /* the region of a smaller header: a 60x33 guest copy of a 64x33 target reads its scaled 120x66, and logs nothing */
    Surf* u = target(64, 33, F);
    g_log[0] = 0;
    Surf* gu = vk_rt_guest(u, 60, 33, &why);
    CHECK(gu && gu->sw == 60 && gu->sh == 33 && last_blit.srcOffsets[1].x == 120 && last_blit.srcOffsets[1].y == 66);
    CHECK(last_blit.dstOffsets[1].x == 60 && last_blit.dstOffsets[1].y == 33 && !strstr(g_log, "only that region"));
    /* a guest copy and a crop of one size and format: two entries, the crop at its scaled size, and its line */
    Surf* cu = rt_crop(u, 60, 33, NULL, 0);
    CHECK(cu && cu != gu && cu->sw == 120 && cu->sh == 66 && entries(u) == 2);
    CHECK(strstr(g_log, "60x33 texture reads the 64x33 render target") != NULL);
    /* a target whose image is smaller than its scaled size (a rounded scale): the blit reads no further than it */
    Surf* r = target(60, 33, F);
    r->sw = 119, r->sh = 65;
    CHECK(vk_rt_guest(r, 60, 33, &why) && last_blit.srcOffsets[1].x == 119 && last_blit.srcOffsets[1].y == 65);
    CHECK(last_blit.dstOffsets[1].x == 60 && last_blit.dstOffsets[1].y == 33);

    /* fallbacks: an integer format, a format without linear blits (asked once), no memory */
    u32 made = images, before = nblits;
    Surf* ti = target(480, 270, VK_FORMAT_R32_UINT);
    ti->f.is_int = 1;
    why = -1;
    CHECK(!vk_rt_guest(ti, 480, 270, &why) && why == PASS_GUEST_INTEGER && entries(ti) == 0);
    no_blit = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    Surf* tf = target(480, 270, no_blit);
    u32 asks = format_asks;
    why = -1;
    CHECK(!vk_rt_guest(tf, 480, 270, &why) && why == PASS_GUEST_FORMAT && entries(tf) == 0);
    CHECK(!vk_rt_guest(tf, 480, 270, &why) && why == PASS_GUEST_FORMAT && format_asks == asks + 1);
    no_filter = VK_FORMAT_A2B10G10R10_UNORM_PACK32; /* blits, but not with a linear filter */
    Surf* tl = target(480, 270, no_filter);
    why = -1;
    CHECK(!vk_rt_guest(tl, 480, 270, &why) && why == PASS_GUEST_FORMAT && entries(tl) == 0);
    Surf* td = target(480, 270, VK_FORMAT_D32_SFLOAT);
    td->f.depth = 1;
    why = -1;
    CHECK(!vk_rt_guest(td, 480, 270, &why) && why == PASS_GUEST_FORMAT && entries(td) == 0);
    Surf* tm = target(480, 270, F);
    image_fails = 1;
    why = -1;
    CHECK(!vk_rt_guest(tm, 480, 270, &why) && why == PASS_GUEST_MEMORY && entries(tm) == 0);
    image_fails = 0;
    CHECK(images == made && nblits == before);

    /* pruned as crops are: unused for more than 60 presents, gone at the next request of another entry */
    g_vk_present = 160;
    CHECK(vk_rt_snapshot(t) == s && entries(t) == 3); /* 60 presents after their last use: not yet past 60 */
    g_vk_present = 161;
    CHECK(vk_rt_snapshot(t) == s && entries(t) == 1 && t->crop == s); /* the guest copy and the crop went */
    Surf* g2 = vk_rt_guest(t, 960, 540, &why);
    CHECK(g2 && g2->guest_copy && entries(t) == 2);

    drop(t), drop(u), drop(r), drop(ti), drop(tf), drop(tl), drop(td), drop(tm);
    printf("rt_guest_copy: %u checks passed\n", checks);
    return 0;
}
