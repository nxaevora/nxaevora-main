/* CANDIDATE for gpu/vk/texture.c (copy_layers). Not yet integrated: the current texture.c was not available to this
   review. Gate: SNX_SHADOW_DEPTH_RESAMPLE=1 (exactly). Off = byte-identical behaviour to today's nearest blit.

   Problem: a 1024-square guest depth target (shadow cascade) is rasterized 4/3-scaled into a 1365-square image with a
   1365.333 viewport, then copy_layers reduces it into the 1024-square sampled array with a NEAREST blit over
   vk_sc(w)=1365 -> 1024. Each stored texel then holds the depth of a point displaced by -0.125/-0.375/+0.125 shadow
   texels (period 3), so a sloped receiver's linearly-filtered PCF taps see a periodic depth error of up to
   0.375 * slope. When the game's slope bias (k*tan(theta)) is tuned close to what a native map needs, single PCF taps
   flip in a texel-locked pattern: stripes/bumps on steps, bricks, planks.

   Change: for float depth (D32_SFLOAT) only, reduce with a LINEAR blit over a region whose ratio is exactly the
   render scale (1364 -> 1023 at 4/3), so every stored texel is the bilinear reconstruction of the raster at that
   texel's nominal centre. Planar surfaces are then reproduced exactly (identical to a native 1024 raster).
   Vulkan forbids linear blits of depth formats, so the depth goes through R32_SFLOAT with transfer commands only:
     D32 depth aspect -> buffer -> R32F (sw x sh) -> LINEAR blit -> R32F (dw x dh) -> buffer -> D32 depth aspect.
   Values stay inside [0,1] (convex combinations of stored depths), as copies into a D32 depth aspect require.
   Integer tag layers (R8_UINT), D24/D16/stencil formats, unscaled targets and enlargements keep the existing path.

   Known cost: at caster silhouettes the bilinear reduction blends near and far depths, so shadow edges can move by
   up to one shadow texel (the receiver's own LINEAR depth filter already blends across one texel). Planar acne is
   what changes; silhouettes are what the visual test must look at. */

static int depth_resample_on(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("SNX_SHADOW_DEPTH_RESAMPLE");
        on = e && !strcmp(e, "1");
        if (on) LOG("vulkan: scaled float depth targets reduce to textures by exact-ratio linear resampling\n");
    }
    return on;
}

/* the largest destination span d <= dw (at most 16 less) whose scaled size d*scale is a whole number of source texels
   no larger than sw; 0 when none, the caller then keeps the existing blit. At 4/3: 1024 -> 1023 <- 1364 */
static u32 exact_span(u32 dw, u32 sw, float scale, u32* src_span) {
    for (u32 d = dw; d && d + 16 >= dw; d--) {
        double s = (double)d * (double)scale, r = floor(s + 0.5);
        if (fabs(s - r) < 1e-3 && r >= 1 && r <= (double)sw) {
            *src_span = (u32)r;
            return d;
        }
    }
    return 0;
}

static void transfer_barrier(VkCommandBuffer cb) {
    /* every transfer write before, ordered before every transfer read or write after (buffer and image memory) */
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                           .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                           .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    rec_CmdPipelineBarrier2(cb, &di);
}

static VkImage r32f_image(u32 w, u32 h, VmaAllocation* mem) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R32_SFLOAT,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkImage img = VK_NULL_HANDLE;
    return vk_image_create(&ii, &img, mem) ? img : VK_NULL_HANDLE;
}

static void blit_r32f(VkCommandBuffer cb, VkImage a, VkImage b, i32 sx0, i32 sy0, i32 sx1, i32 sy1, i32 dx0, i32 dy0,
                      i32 dx1, i32 dy1) {
    if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return;
    VkImageBlit r = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     .srcOffsets = {{sx0, sy0, 0}, {sx1, sy1, 1}},
                     .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     .dstOffsets = {{dx0, dy0, 0}, {dx1, dy1, 1}}};
    rec_CmdBlitImage(cb, a, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r,
                     VK_FILTER_LINEAR);
}

/* s: a scaled render target, its image in TRANSFER_SRC_OPTIMAL; dst: a texture image of the guest size (dw x dh), the
   layer in TRANSFER_DST_OPTIMAL, as copy_layers already arranges for its blit. 1 when done (skip the blit), 0 when this
   case isn't handled (record the existing blit). Leaves both images in their layouts, with transfer writes ordered
   before later transfer stages (ALL_TRANSFER includes BLIT, which copy_layers' tracked surface state names). */
static int depth_resample_layer(VkCommandBuffer cb, const Surf* s, u32 src_layer, VkImage dst, u32 dst_layer, u32 dw,
                                u32 dh) {
    u32 sw = s->sw, sh = s->sh, spx, spy;
    if (!depth_resample_on() || !s->f.depth || s->f.stencil || s->f.vk != VK_FORMAT_D32_SFLOAT) return 0;
    if (sw <= dw || sh <= dh || dw != s->w || dh != s->h) return 0; /* reductions of a whole scaled target only */
    if (!blit_ok(VK_FORMAT_R32_SFLOAT, VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        LOG_LIMIT(1, "vulkan: R32_SFLOAT can't be linearly blitted: scaled depth keeps the nearest reduction\n");
        return 0;
    }
    u32 ex = exact_span(dw, sw, g_vk_scale, &spx), ey = exact_span(dh, sh, g_vk_scale, &spy);
    if (!ex || !ey) return 0;
    u64 big = (u64)sw * sh * 4, small = (u64)dw * dh * 4, cap;
    VkBuffer buf = vk_bl_scratch(big + small, &cap);
    if (!buf) return 0;
    VmaAllocation m1, m2;
    VkImage t1 = r32f_image(sw, sh, &m1);
    if (!t1) return 0;
    VkImage t2 = r32f_image(dw, dh, &m2);
    if (!t2) {
        vk_defer_image(t1, m1, VK_NULL_HANDLE, VK_NULL_HANDLE);
        return 0;
    }
    transfer_barrier(cb); /* the caller's transitions (to BLIT) and any earlier scratch use, before the copies */
    VkBufferImageCopy c = {.bufferOffset = 0,
                           .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, src_layer, 1},
                           .imageExtent = {sw, sh, 1}};
    rec_CmdCopyImageToBuffer(cb, s->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &c);
    vk_barrier(cb, t1, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_barrier(cb, t2, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    transfer_barrier(cb);
    c.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    rec_CmdCopyBufferToImage(cb, buf, t1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    vk_barrier(cb, t1, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    /* exact-ratio body, then the remainder strips (a column/row of 1 at 4/3), each as its own ratio */
    blit_r32f(cb, t1, t2, 0, 0, (i32)spx, (i32)spy, 0, 0, (i32)ex, (i32)ey);
    blit_r32f(cb, t1, t2, (i32)spx, 0, (i32)sw, (i32)spy, (i32)ex, 0, (i32)dw, (i32)ey);
    blit_r32f(cb, t1, t2, 0, (i32)spy, (i32)spx, (i32)sh, 0, (i32)ey, (i32)ex, (i32)dh);
    blit_r32f(cb, t1, t2, (i32)spx, (i32)spy, (i32)sw, (i32)sh, (i32)ex, (i32)ey, (i32)dw, (i32)dh);
    vk_barrier(cb, t2, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy d = {.bufferOffset = big, /* a multiple of 4, as depth copies need */
                           .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           .imageExtent = {dw, dh, 1}};
    rec_CmdCopyImageToBuffer(cb, t2, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &d);
    transfer_barrier(cb);
    d.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_DEPTH_BIT, 0, dst_layer, 1};
    rec_CmdCopyBufferToImage(cb, buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &d);
    transfer_barrier(cb); /* the destination's write, and the scratch reads, before what the caller records next */
    vk_defer_image(t1, m1, VK_NULL_HANDLE, VK_NULL_HANDLE);
    vk_defer_image(t2, m2, VK_NULL_HANDLE, VK_NULL_HANDLE);
    return 1;
}

/* Integration into copy_layers (sketch: adapt to its variable names). Where it records, per layer, the scaled
   target's vk_sc(w) x vk_sc(h) blit into the native-size texture layer with VK_FILTER_NEAREST:

       if (!depth_resample_layer(cb, rt, rt_layer, tex->img, tex_layer, tex->w, tex->h))
           <existing rec_CmdBlitImage / scale_blit, unchanged>;

   Requirements to confirm in texture.c: rt->img is in TRANSFER_SRC_OPTIMAL and the texture layer in
   TRANSFER_DST_OPTIMAL at that point (vk_surf_use / vk_barrier before the existing blit); the copy covers the whole
   target (w == rt->w, h == rt->h); vk_bl_scratch isn't held by an open vk_bl_barrier pair across this call. */
