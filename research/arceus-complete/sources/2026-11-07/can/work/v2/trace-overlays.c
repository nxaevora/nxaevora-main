#include <math.h>
/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The render target set, clears and draws. The 3D registers name up to 8 color targets (RT_CONTROL at 0x487 maps
   the slots, each target's address, size, format and tiling at 0x200 + 0x10 * index) and a depth target (enabled
   at 0x54E, address at 0x3F8, size at 0x48A, format and tiling at 0x3FA). A dynamic rendering pass stays open while
   draws and clears keep the same set, and ends before anything that copies or transitions images.

   A draw reads the 3D engine's registers for its state. Signed viewport scales and coordinate swizzles map the
   guest's position. Window-origin bit 4 controls triangle facing independently of that mapping. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/runtime/options.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/renderer.h"
#include "nx/gpu/shader/shader.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/pending_pages.h"
#include "nx/gpu/vk/internal_spv.h"
#include "nx/runtime/platform/tracy.h"

static struct {
    Surf* rt[8];
    Surf* zt;
    u32 n; /* color slots up to the last bound one */
    VkExtent2D ext;
    int open;
    int srgb_write;
    int zt_ro; /* the depth target is read-only in this pass: draws also sample it */
    /* feedback loops (feedback_order): the color slots in ATTACHMENT_FEEDBACK_LOOP_OPTIMAL, those of them written
       since the pass began or its last barrier, and those sampled since then */
    u32 fb, fb_written, fb_read;
} g_rp;
u64 g_vk_fb_draws, g_vk_fb_barriers, g_vk_fb_restarts, g_vk_rt_copies;

void vk_render_end(void) {
    if (!g_rp.open) return;
    g_rp.open = 0;
    VkCommandBuffer cb = vk_cmd();
    vk_query_pass_end(cb); /* a query begun in the pass ends in it */
    rec_CmdEndRendering(cb);
    vk_gpuprof_pass_end(cb);
}

/* the targets the registers name, without opening anything */
static void read_targets(GpuChannel* ch, Surf** rt, Surf** zt, u32* n) {
    u32* r = ch->r3d;
    u32 ctl = r[0x487], count = ctl & 15;
    if (count > 8) count = 8;
    *n = 0;
    for (u32 i = 0; i < 8; i++) {
        rt[i] = NULL;
        if (i >= count) continue;
        u32* t = &r[0x200 + ((ctl >> (4 + i * 3)) & 7) * 0x10];
        u64 a = ((u64)t[0] << 32) | t[1];
        if (a && t[2] && t[3] && t[4]) {
            /* a pitch-linear target (bit 12 of the memory layout) holds its row pitch in bytes where the width is */
            u32 bpp = vk_rt_format(t[4]).bpp, linear = (t[5] >> 12) & 1, w = linear ? t[2] / bpp : t[2];
            /* COLOR_TARGET_THIRD_DIMENSION (bits 0-27) names the number of renderable array slices, or a volume's
               depth when the memory word's THIRD_DIMENSION_CONTROL (bit 16, NVIDIA's clb197.h) says so: its slices
               are then GOB slices of a 3D surface, and the array pitch says nothing. One game's 64x64x64 light
               volume taken as 64 layers its 1 MiB array pitch apart covered 63 MiB of other memory. Vertex shaders
               may select one with their layer output (the color LUT passes render all slices in one draw). */
            u32 layers = t[6] & 0x0fffffff;
            if (!layers || layers > g_vk.props.limits.maxImageArrayLayers) layers = 1;
            if (w)
                rt[i] = vk_rt_array(a, w, t[3], t[4], t[5], linear ? t[2] : w * bpp, layers, (u64)t[7] * 4,
                                    (t[5] >> 16) & 1);
            if (rt[i]) *n = i + 1;
        }
    }
    *zt = NULL;
    if (r[0x54E]) {
        u64 za = ((u64)r[0x3F8] << 32) | r[0x3F9];
        if (za && r[0x48A] && r[0x48B]) {
            /* ZETA_SIZE's dimension control can force one layer; otherwise its depth is the array size. */
            u32 layers = r[0x48C] & (1u << 16) ? 1 : r[0x48C] & 0xffff;
            if (!layers || layers > g_vk.props.limits.maxImageArrayLayers) layers = 1;
            *zt = vk_zt_array(za, r[0x48A], r[0x48B], r[0x3FA], r[0x3FB], layers, (u64)r[0x3FC] * 4);
        }
    }
}

/* opens a rendering pass on these targets, or keeps the open one if it is the same set. zt_ro: the depth target is
   bound read-only, so draws can sample it too (soft particles test against depth they also read). fb: color slots the
   draw samples, which the pass needs in ATTACHMENT_FEEDBACK_LOOP_OPTIMAL (feedback_order) */
static int render_begin(Surf** rt, Surf* zt, u32 n, int zt_ro, int srgb_write, u32 fb) {
    srgb_write = !!srgb_write;
    if (!n && !zt) return 0;
    if (!zt) zt_ro = 0;
    int same = g_rp.open && g_rp.n == n && g_rp.zt == zt && g_rp.zt_ro == zt_ro && g_rp.srgb_write == srgb_write &&
               !memcmp(g_rp.rt, rt, sizeof g_rp.rt);
    if (same && !(fb & ~g_rp.fb)) return 1;
    if (same) g_vk_fb_restarts++; /* only a target's layout changes */
    vk_render_end();
    VkCommandBuffer cb = vk_cmd();
    u32 loops = 0;
    VkRenderingAttachmentInfo ca[8];
    VkExtent2D ext = {~0u, ~0u};
    for (u32 i = 0; i < n; i++) {
        ca[i] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                            .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        if (!rt[i]) continue;
        /* a target left in the feedback-loop layout stays in it: draws keep sampling the same targets pass after pass,
           and moving it back would end the next such pass at its first feedback draw */
        int loop = g_vk.has_feedback &&
                   ((fb >> i & 1) || rt[i]->layout == VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT);
        VkImageLayout layout =
            loop ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        /* the fragment shader's reads are in the barrier's second scope, so the pass begins with every earlier write
           visible to them */
        vk_surf_use(cb, rt[i], layout,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        (loop ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : 0),
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                        (loop ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0));
        ca[i].imageView = vk_rt_attachment(rt[i], srgb_write);
        if (!ca[i].imageView) return 0;
        ca[i].imageLayout = layout;
        if (loop) loops |= 1u << i;
        if (rt[i]->sw < ext.width) ext.width = rt[i]->sw;
        if (rt[i]->sh < ext.height) ext.height = rt[i]->sh;
    }
    VkRenderingAttachmentInfo za = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkImageLayout zl =
        zt_ro ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    /* a read-only depth target keeps its contents: a store would write it at the end of the pass, which the read
       access vk_surf_use records doesn't cover, and the next barrier wouldn't wait for it */
    if (zt_ro) za.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
    if (zt) {
        if (zt_ro)
            vk_surf_use(cb, zt, zl,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                            VK_SAMPLED_STAGES,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        else
            vk_surf_use(cb, zt, zl,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        za.imageView = zt->att;
        za.imageLayout = zl;
        if (zt->sw < ext.width) ext.width = zt->sw;
        if (zt->sh < ext.height) ext.height = zt->sh;
    }
    u32 layers = ~0u;
    for (u32 i = 0; i < n; i++)
        if (rt[i] && rt[i]->layers < layers) layers = rt[i]->layers;
    if (zt && zt->layers < layers) layers = zt->layers;
    if (layers == ~0u) layers = 1;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, ext},
                          .layerCount = layers,
                          .colorAttachmentCount = n,
                          .pColorAttachments = ca,
                          .pDepthAttachment = zt ? &za : NULL,
                          .pStencilAttachment = zt && zt->f.stencil ? &za : NULL};
    vk_gpuprof_pass_begin(cb, n ? rt[0] : NULL, zt);
    rec_CmdBeginRendering(cb, &ri);
    memcpy(g_rp.rt, rt, sizeof g_rp.rt);
    g_rp.zt = zt;
    g_rp.n = n;
    g_rp.ext = ext;
    g_rp.zt_ro = zt_ro;
    g_rp.srgb_write = srgb_write;
    g_rp.fb = loops;
    g_rp.fb_written = g_rp.fb_read = 0;
    g_rp.open = 1;
    return 1;
}
static void target_written(Surf* s) {
    vk_surf_wrote(s);
    vk_rt_drawn(s);
}
/* a draw or clear wrote the depth target, which was undefined (its layout, before the pass, or still) when that is
   pred_undefined. One predicated on the GPU may not have run, and depth is never loaded from memory: until a write
   that surely runs, the target holds nothing a texture may take (tex_take_drawn), as before its first pass. 1 when
   it counts as written */
static int zt_written(Surf* zt, u32 pred, int undefined) {
    if (pred && (undefined || zt->pred_undefined)) {
        zt->pred_undefined = 1;
        return 0;
    }
    zt->pred_undefined = 0;
    return 1;
}

/* A pass with no attachments, for a draw that only captures its vertices (transform feedback with no target): its
   area is as large as a framebuffer may be, so the draw's viewport and scissor fit in it */
static int render_begin_empty(void) {
    if (g_rp.open && !g_rp.n && !g_rp.zt) return 1;
    vk_render_end();
    VkExtent2D ext = {g_vk.props.limits.maxFramebufferWidth, g_vk.props.limits.maxFramebufferHeight};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, ext}, .layerCount = 1};
    vk_gpuprof_pass_begin(vk_cmd(), NULL, NULL);
    rec_CmdBeginRendering(vk_cmd(), &ri);
    memset(g_rp.rt, 0, sizeof g_rp.rt);
    g_rp.zt = NULL;
    g_rp.n = 0;
    g_rp.ext = ext;
    g_rp.zt_ro = 0;
    g_rp.srgb_write = 0;
    g_rp.fb = g_rp.fb_written = g_rp.fb_read = 0;
    g_rp.open = 1;
    return 1;
}

/* Transform feedback's byte counters, a word for each of the 4 buffers: where a capture's end left each buffer, which
   the next draw's capture resumes from (3D 0x1D1; made at the first capture) */
static struct {
    VkBuffer buf;
    VmaAllocation mem;
} g_xfb;
static int xfb_counters(void) {
    if (g_xfb.buf) return 1;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = 16,
                              .usage = VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &aci, &g_xfb.buf, &g_xfb.mem, NULL))) return 0;
    vk_mem_track(g_xfb.mem);
    return 1;
}

/* Feedback loops. A Maxwell draw may sample a color target it renders to: a game may blend in its shaders,
   reading the pixel under the fragment and writing it back, in hundreds of draws a frame. With
   VK_EXT_attachment_feedback_loop_layout the target stays the pass's attachment, in ATTACHMENT_FEEDBACK_LOOP_OPTIMAL,
   which the draw's descriptor names too, and the draw enables the loop for color (dynamic state, or a pipeline flag).
   Each draw still sees the target as all earlier draws left it: before a draw samples a slot written since the last
   barrier, or writes a slot sampled since then, a barrier orders the color attachment writes and fragment shader
   reads before it against those after it, both ways.

   Within a vkCmdBeginRendering pass a barrier is allowed only with dynamicRenderingLocalRead
   (VUID-vkCmdPipelineBarrier2-None-09553), with framebuffer-space stages only (-srcStageMask-09556), by region
   (-dependencyFlags-07891), and with no image barrier on an attachment outside RENDERING_LOCAL_READ or GENERAL
   (-image-09555): so it is a memory barrier, and the layout stays. By region, the specification orders a write at
   (x, y, layer) only before reads by fragments at the same (x, y, layer) ("Framebuffer Region Dependencies"). That is
   all a fragment reading the pixel it covers needs, which is what blending in a shader does. A fragment reading other
   pixels (a blur of the target) isn't ordered by it in the specification: an immediate-mode desktop GPU drains and
   flushes for the barrier as a whole and orders those reads too in practice, while a tiler may order only within its
   tile. Without dynamicRenderingLocalRead, the pass ends and begins again instead, and render_begin's image barrier,
   outside the pass, orders every pixel.

   Eden ends its render pass for each such draw and samples the image in GENERAL (TextureCache::CheckFeedbackLoop,
   BarrierFeedbackLoop). Before this, the target was copied for each draw (vk_rt_snapshot), which stays for devices
   without the extension, for stages before the fragment shader, and for a depth target the draw writes.

   Orders a command of the open pass that samples the color slots in read and writes those in write. 0 when the pass
   could not be begun again */
static int feedback_order(u32 read, u32 write) {
    read &= g_rp.fb;
    write &= g_rp.fb;
    if (!(read & g_rp.fb_written) && !(write & g_rp.fb_read)) return 1;
    if (!g_vk.has_local_read) {
        Surf* rt[8];
        SNX_ZONE(z, "vk pass restart");
        memcpy(rt, g_rp.rt, sizeof rt);
        vk_render_end();
        g_vk_fb_restarts++;
        return render_begin(rt, g_rp.zt, g_rp.n, g_rp.zt_ro, g_rp.srgb_write, g_rp.fb);
    }
    VkMemoryBarrier2 mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    /* FEEDBACK_LOOP says what the barrier is for, to drivers that flush differently for it */
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                           .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT | VK_DEPENDENCY_FEEDBACK_LOOP_BIT_EXT,
                           .memoryBarrierCount = 1,
                           .pMemoryBarriers = &mb};
    rec_CmdPipelineBarrier2(vk_cmd(), &di);
    g_rp.fb_written = g_rp.fb_read = 0;
    g_vk_fb_barriers++;
    return 1;
}
/* a command of the open pass sampled the color slots in read and wrote those in write */
static void feedback_done(u32 read, u32 write) {
    g_rp.fb_read |= read & g_rp.fb;
    g_rp.fb_written |= write & g_rp.fb;
}

/* every dynamic state at a neutral value, for draws of our own */
static void plain_state(VkCommandBuffer cb, VkRect2D sc) {
    vk_render_forget_state(); /* every state set here, differently from a draw */
    VkViewport vp = {0, 0, (float)g_rp.ext.width, (float)g_rp.ext.height, 0, 1};
    rec_CmdSetViewportWithCount(cb, 1, &vp);
    rec_CmdSetScissorWithCount(cb, 1, &sc);
    rec_CmdSetCullMode(cb, VK_CULL_MODE_NONE);
    rec_CmdSetFrontFace(cb, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    rec_CmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    rec_CmdSetPrimitiveRestartEnable(cb, VK_FALSE);
    rec_CmdSetRasterizerDiscardEnable(cb, VK_FALSE);
    rec_CmdSetLineWidth(cb, 1.0f);
    rec_CmdSetDepthBiasEnable(cb, VK_FALSE);
    rec_CmdSetDepthBias(cb, 0, 0, 0);
    float bc[4] = {0, 0, 0, 0};
    rec_CmdSetBlendConstants(cb, bc);
    rec_CmdSetDepthTestEnable(cb, VK_FALSE);
    rec_CmdSetDepthWriteEnable(cb, VK_FALSE);
    rec_CmdSetDepthCompareOp(cb, VK_COMPARE_OP_ALWAYS);
    rec_CmdSetDepthBoundsTestEnable(cb, VK_FALSE);
    rec_CmdSetDepthBounds(cb, 0.0f, 1.0f);
    rec_CmdSetStencilTestEnable(cb, VK_FALSE);
    rec_CmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                      VK_COMPARE_OP_ALWAYS);
    rec_CmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    rec_CmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    rec_CmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    if (g_vk.has_feedback_dynamic) rec_CmdSetAttachmentFeedbackLoopEnableEXT(cb, VK_IMAGE_ASPECT_NONE);
}

/* a channel's render enable predicate (gpu.h render_pred, 0 none) around one of its draws or clears, inside the
   rendering pass they are in: on the GPU, the comparison it holds decides whether they run */
static void pred_begin(VkCommandBuffer cb, u32 pred) {
    if (pred)
        rec_CmdBeginConditionalRenderingEXT(cb, vk_query_pred_buffer(),
                                            (VkDeviceSize)((pred & ~VK_PRED_EQUAL) - 1) * 4,
                                            (pred & VK_PRED_EQUAL) ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0);
}
static void pred_end(VkCommandBuffer cb, u32 pred) {
    if (pred) rec_CmdEndConditionalRenderingEXT(cb);
}

/* a clear of some color components only: a full-screen triangle with that write mask, whose fragment shader writes
   the cleared slot alone, in the target's number type, run on pred */
static int clear_masked(u32 slot, u32 mask, const float* cc, VkRect2D rect, u32 layer, u32 pred) {
    static VkShaderModule vs, fs[8][3];
    const VkFmt* f = &g_rp.rt[slot]->f;
    int num = f->is_int ? (f->sint ? 2 : 1) : 0;
    if (!vs) vs = vk_module_spv(snx_clear_vs_spv, sizeof snx_clear_vs_spv / sizeof *snx_clear_vs_spv);
    if (!fs[slot][num]) {
        static const u32* words[3] = {snx_clear_fs_float_spv, snx_clear_fs_uint_spv, snx_clear_fs_sint_spv};
        static const size_t count[3] = {sizeof snx_clear_fs_float_spv / sizeof *snx_clear_fs_float_spv,
                                        sizeof snx_clear_fs_uint_spv / sizeof *snx_clear_fs_uint_spv,
                                        sizeof snx_clear_fs_sint_spv / sizeof *snx_clear_fs_sint_spv};
        fs[slot][num] = vk_module_spv_location(words[num], count[num], slot);
    }
    if (!vs || !fs[slot][num]) return 0;
    PipeKey k;
    memset(&k, 0, sizeof k);
    k.vs = vs;
    k.fs = fs[slot][num];
    k.topo_class = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    k.polygon = VK_POLYGON_MODE_FILL;
    k.ncolor = g_rp.n;
    for (u32 i = 0; i < g_rp.n; i++)
        k.color[i] =
            g_rp.rt[i] ? (g_rp.srgb_write ? g_rp.rt[i]->f.vk : vk_srgb_as(g_rp.rt[i]->f.vk, 0)) : VK_FORMAT_UNDEFINED;
    k.blend[slot].mask = ((mask & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) | ((mask & 2) ? VK_COLOR_COMPONENT_G_BIT : 0) |
                         ((mask & 4) ? VK_COLOR_COMPONENT_B_BIT : 0) | ((mask & 8) ? VK_COLOR_COMPONENT_A_BIT : 0);
    if (g_rp.zt) {
        k.depth = g_rp.zt->f.vk;
        k.stencil = g_rp.zt->f.stencil ? g_rp.zt->f.vk : VK_FORMAT_UNDEFINED;
    }
    VkPipeline p = vk_pipeline_get(&k);
    if (!p) return 0;
    VkCommandBuffer cb = vk_cmd();
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    plain_state(cb, rect);
    u32 pc[8] = {0};
    pc[0] = layer;
    memcpy(&pc[4], cc, 16); /* the register bits, read as the shader's vec4, uvec4 or ivec4 */
    rec_CmdPushConstants(cb, g_vk_playout, VK_PUSH_STAGES, 0, 32, pc);
    vk_query_pass_end(cb); /* a clear passes no samples on the console: its draw here isn't counted */
    pred_begin(cb, pred);
    rec_CmdDraw(cb, 3, 1, 0, 0);
    pred_end(cb, pred);
    return 1;
}

/* gpu_trace_target=address[:bytes]: log the clears and draws that write a render target starting in [address,
   address + bytes) (one byte without a size), to find what produced a wrong target. Read on first use: draws and
   clears run on the GPU thread */
static int trace_target(const Surf* s) {
    static int read;
    static u64 lo, bytes;
    if (!read) {
        read = 1;
        const char* v = g_settings.gpu_trace_target;
        if (*v) {
            char *end, *size_end = NULL;
            lo = strtoull(v, &end, 0);
            bytes = *end == ':' ? strtoull(end + 1, &size_end, 0) : 1;
            if (end == v || (*end && *end != ':') || (size_end && (*size_end || size_end == end + 1)) || !bytes) {
                WARN("gpu_trace_target=%s isn't address[:bytes] with a nonzero size (0x for hex): no trace\n", v);
                bytes = 0;
            }
        }
    }
    return bytes && s && s->gva >= lo && s->gva - lo < bytes;
}


/* Explicit Arceus experiment: the observed 1024-square D32/tag shadow pair.
   Disabled by default; no scene color/depth format is selected. */
static int native_shadow_shape(u32 w, u32 h, u32 fkey) {
    const char* flag = getenv("SNX_ARCEUS_NATIVE_SHADOWS");
    return flag && !strcmp(flag, "1") && w == 1024 && h == 1024 &&
           (fkey == 0x1000a || fkey == 0xf6);
}

static float target_raster_scale(Surf** rt, Surf* zt, u32 n) {
    float scale=0;for(u32 i=0;i<=n;i++){Surf *t=i==n?zt:rt[i];if(!t)continue;
        float q=(float)t->sw/t->w;if(scale && fabsf(scale-q)>.001f){LOG_LIMIT(8,"arceus-upscaler: mixed-scale attachments\n");return 0;}scale=q;}return scale?scale:1;
}
/* v: bit 0 depth, 1 stencil, 2-5 the RGBA mask, 6-9 the color slot, 10-20 the layer */
void vk_render_clear(GpuChannel* ch, u32 v) {
    u32* r = ch->r3d;
    u32 slot = (v >> 6) & 15, mask = (v >> 2) & 15, layer = (v >> 10) & 0x7ff;
    float cc[4], dz;
    memcpy(cc, &r[0x360], 16);
    memcpy(&dz, &r[0x364], 4);
    dz = dz > 1 ? 1 : dz >= 0 ? dz : 0; /* Vulkan takes 0..1 only */
    Surf* rt[8];
    Surf* zt;
    u32 n;
    read_targets(ch, rt, &zt, &n);
    float raster_scale = target_raster_scale(rt, zt, n);
    if (!raster_scale) return;      /* may record uploads: before anything is opened */
    for (u32 i = 0; mask && i < n; i++) /* the slot the clear takes is resolved below: every bound one is listed */
        if (trace_target(rt[i]))
            LOG_LIMIT(64,
                      "gpu-trace-target: color clear of slot %u, bound slot %u target=%llx %ux%u mask=%x "
                      "value=%08x,%08x,%08x,%08x\n",
                      slot, i, (unsigned long long)rt[i]->gva, rt[i]->w, rt[i]->h, mask, r[0x360], r[0x361], r[0x362],
                      r[0x363]);
    if ((v & 3) && trace_target(zt))
        LOG_LIMIT(64, "gpu-trace-target: clear depth target=%llx %ux%u depth=%d stencil=%d value=%g\n",
                  (unsigned long long)zt->gva, zt->w, zt->h, v & 1, (v >> 1) & 1, dz);
    if (mask) {
        /* the slot is taken through RT_CONTROL's count and permutation, like draws do. Whether the hardware takes the
           target registers of that index directly isn't settled: the first clear where the two differ is logged */
        u32 ctl = r[0x487];
        if (slot >= (ctl & 15) || ((ctl >> (4 + slot * 3)) & 7) != slot)
            LOG_LIMIT(1, "vulkan: a clear of color slot %u under RT_CONTROL %08x\n", slot, ctl);
    }
    int zt_undefined = zt && zt->layout == VK_IMAGE_LAYOUT_UNDEFINED;
    if (!render_begin(rt, zt, n, 0, r[0x56E] & 1, 0)) return;
    u32 layers = ~0u;
    for (u32 i = 0; i < g_rp.n; i++)
        if (g_rp.rt[i] && g_rp.rt[i]->layers < layers) layers = g_rp.rt[i]->layers;
    if (g_rp.zt && g_rp.zt->layers < layers) layers = g_rp.zt->layers;
    if (layer >= layers) return;
    VkClearRect rect = {{{0, 0}, g_rp.ext}, layer, 1};
    if (r3d_clear_scissored(r)) {
        u32 xs = r[0x381], ys = r[0x382];
        i32 x0 = (i32)((float)(xs & 0xFFFF) * raster_scale), x1 = (i32)((float)(xs >> 16) * raster_scale), y0 = (i32)((float)(ys & 0xFFFF) * raster_scale),
            y1 = (i32)((float)(ys >> 16) * raster_scale);
        if (r[0x4EB] & 1) {
            i32 clip_height = (i32)((float)((float)(r[0x3FE] >> 16)) * raster_scale);
            i32 old_y0 = y0;
            y0 = clip_height - y1;
            y1 = clip_height - old_y0;
        }
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (i32)g_rp.ext.width) x1 = (i32)g_rp.ext.width;
        if (y1 > (i32)g_rp.ext.height) y1 = (i32)g_rp.ext.height;
        if (x1 <= x0 || y1 <= y0) return;
        rect.rect = (VkRect2D){{x0, y0}, {(u32)(x1 - x0), (u32)(y1 - y0)}};
    }
    /* the color slot it writes, after the pass's draws that sampled it */
    u32 written = mask && slot < g_rp.n && g_rp.rt[slot] ? 1u << slot : 0;
    if (!feedback_order(0, written)) return;
    feedback_done(0, written);
    VkClearAttachment at[2];
    u32 na = 0;
    if (mask && slot < g_rp.n && g_rp.rt[slot]) {
        if (mask == 15) {
            at[na] = (VkClearAttachment){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = slot};
            /* an integer target's clear color registers hold the integers themselves, not floats to convert: an
               R8_Uint clear of 185 arrives as 000000b9 */
            if (g_rp.rt[slot]->f.is_int)
                memcpy(at[na].clearValue.color.uint32, &r[0x360], 16);
            else
                memcpy(at[na].clearValue.color.float32, cc, 16);
            na++;
        } else {
            if (clear_masked(slot, mask, cc, rect.rect, layer, ch->render_pred)) target_written(g_rp.rt[slot]);
        }
    }
    if ((v & 3) && zt) {
        VkImageAspectFlags a = 0;
        if (v & 1) a |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if ((v & 2) && zt->f.stencil) a |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (a) {
            at[na] = (VkClearAttachment){.aspectMask = a};
            at[na].clearValue.depthStencil = (VkClearDepthStencilValue){dz, r[0x368] & 0xFF};
            na++;
        }
    }
    if (na) {
        int color_clear = mask == 15 && slot < g_rp.n && g_rp.rt[slot];
        pred_begin(vk_cmd(), ch->render_pred);
        rec_CmdClearAttachments(vk_cmd(), na, at, 1, &rect);
        pred_end(vk_cmd(), ch->render_pred);
        /* a predicated clear may not run: it covers no target whole */
        if (color_clear) {
            Surf* s = g_rp.rt[slot];
            vk_surf_wrote(s);
            if (!ch->render_pred && s->layers == 1 && rect.rect.offset.x == 0 && rect.rect.offset.y == 0 &&
                rect.rect.extent.width == s->sw && rect.rect.extent.height == s->sh)
                vk_rt_drawn_full(s);
            else
                vk_rt_drawn(s);
        }
        if (na > (u32)color_clear && zt && zt_written(zt, ch->render_pred, zt_undefined)) {
            vk_surf_wrote(zt);
            int all_aspects = (v & 1) && (!zt->f.stencil || (v & 2));
            if (!ch->render_pred && all_aspects && zt->layers == 1 && rect.rect.offset.x == 0 &&
                rect.rect.offset.y == 0 && rect.rect.extent.width == zt->sw && rect.rect.extent.height == zt->sh)
                vk_rt_drawn_full(zt);
            else
                vk_rt_drawn(zt);
        }
    }
}

/* ------------------------------------------------------------ register values -> Vulkan */
static VkBlendFactor blend_factor(u32 v) {
    if (v & 0x4000)
        v &= ~0x4000u; /* a GL enum */
    else
        switch (v) { /* the D3D-style numbering */
            case 1: return VK_BLEND_FACTOR_ZERO;
            case 2: return VK_BLEND_FACTOR_ONE;
            case 3: return VK_BLEND_FACTOR_SRC_COLOR;
            case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
            case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case 7: return VK_BLEND_FACTOR_DST_ALPHA;
            case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case 9: return VK_BLEND_FACTOR_DST_COLOR;
            case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            case 14: return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case 16: return VK_BLEND_FACTOR_SRC1_COLOR;
            case 17: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            case 18: return VK_BLEND_FACTOR_SRC1_ALPHA;
            case 19: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            default: return VK_BLEND_FACTOR_ONE;
        }
    switch (v) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x300: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return VK_BLEND_FACTOR_DST_COLOR;
        case 0x307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        /* the dual-source factors as the hardware numbers them (0xC900-0xC903), not by their GL values */
        case 0x8900: return VK_BLEND_FACTOR_SRC1_COLOR;
        case 0x8901: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case 0x8902: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case 0x8903: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        case 0x88F9: return VK_BLEND_FACTOR_SRC1_COLOR;
        case 0x88FA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case 0x8589: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case 0x88FB: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}
static VkBlendOp blend_op(u32 v) {
    switch (v) {
        case 2:
        case 0x800A: return VK_BLEND_OP_SUBTRACT;
        case 3:
        case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 4:
        case 0x8007: return VK_BLEND_OP_MIN;
        case 5:
        case 0x8008: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}
static VkCompareOp cmp_op(u32 v) {
    if (v >= 0x200 && v <= 0x207) return (VkCompareOp)(v - 0x200);
    if (v >= 1 && v <= 8) return (VkCompareOp)(v - 1);
    return VK_COMPARE_OP_ALWAYS;
}
static VkStencilOp stencil_op(u32 v) {
    switch (v) {
        case 1:
        case 0x1E00: return VK_STENCIL_OP_KEEP;
        case 3:
        case 0x1E01: return VK_STENCIL_OP_REPLACE;
        case 4:
        case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 5:
        case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 6:
        case 0x150A: return VK_STENCIL_OP_INVERT;
        case 7:
        case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 8:
        case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_ZERO;
}

/* vertex attribute format from the size code (bits 21-26) and component type (27-29): 1 snorm, 2 unorm, 3 sint,
   4 uint, 5 uscaled, 6 sscaled, 7 float */
static VkFormat attr_format(u32 size, u32 ty, int bgra) {
#define BY(sn, un, si, ui, us, ss, fl)                                                                                 \
    (ty == 1 ? sn : ty == 2 ? un : ty == 3 ? si : ty == 4 ? ui : ty == 5 ? us : ty == 6 ? ss : fl)
    /* Vulkan has no 32-bit scaled formats, and an integer format would hand the float input the integers' bits */
    if ((ty == 5 || ty == 6) && (size == 0x01 || size == 0x02 || size == 0x04 || size == 0x12)) {
        LOG_LIMIT(1, "vulkan: 32-bit scaled vertex attributes aren't done yet (read as 0,0,0,1)\n");
        return VK_FORMAT_UNDEFINED;
    }
    switch (size) {
        case 0x01:
            return BY(VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32G32B32A32_SINT,
                      VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED,
                      VK_FORMAT_R32G32B32A32_SFLOAT);
        case 0x02:
            return BY(VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_UINT,
                      VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32G32B32_SFLOAT);
        case 0x04:
            return BY(VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_UINT,
                      VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32G32_SFLOAT);
        case 0x12:
            return BY(VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SINT, VK_FORMAT_R32_UINT,
                      VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SFLOAT);
        case 0x03:
            return BY(VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SINT,
                      VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_USCALED, VK_FORMAT_R16G16B16A16_SSCALED,
                      VK_FORMAT_R16G16B16A16_SFLOAT);
        case 0x05:
            return BY(VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16_SINT,
                      VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16_USCALED, VK_FORMAT_R16G16B16_SSCALED,
                      VK_FORMAT_R16G16B16_SFLOAT);
        case 0x0F:
            return BY(VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_UINT,
                      VK_FORMAT_R16G16_USCALED, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R16G16_SFLOAT);
        case 0x1B:
            return BY(VK_FORMAT_R16_SNORM, VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SINT, VK_FORMAT_R16_UINT,
                      VK_FORMAT_R16_USCALED, VK_FORMAT_R16_SSCALED, VK_FORMAT_R16_SFLOAT);
        case 0x0A:
            if (bgra && (ty == 2 || ty == 7)) return VK_FORMAT_B8G8R8A8_UNORM;
            return BY(VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SINT,
                      VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_USCALED, VK_FORMAT_R8G8B8A8_SSCALED,
                      VK_FORMAT_R8G8B8A8_UNORM);
        case 0x13:
            return BY(VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8_UINT,
                      VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8_SSCALED, VK_FORMAT_R8G8B8_UNORM);
        case 0x18:
            return BY(VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8_UINT,
                      VK_FORMAT_R8G8_USCALED, VK_FORMAT_R8G8_SSCALED, VK_FORMAT_R8G8_UNORM);
        case 0x1D:
            return BY(VK_FORMAT_R8_SNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SINT, VK_FORMAT_R8_UINT,
                      VK_FORMAT_R8_USCALED, VK_FORMAT_R8_SSCALED, VK_FORMAT_R8_UNORM);
        case 0x30: /* the type decides the shader's input type too (integer or float): each gets its own format */
            return BY(VK_FORMAT_A2B10G10R10_SNORM_PACK32, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                      VK_FORMAT_A2B10G10R10_SINT_PACK32, VK_FORMAT_A2B10G10R10_UINT_PACK32,
                      VK_FORMAT_A2B10G10R10_USCALED_PACK32, VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
                      VK_FORMAT_A2B10G10R10_UNORM_PACK32);
        case 0x31: return ty == 3 || ty == 4 ? VK_FORMAT_UNDEFINED : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    }
#undef BY
    return VK_FORMAT_UNDEFINED;
}
static int vertex_format_ok(VkFormat f) {
    static VkFormat ok[64], bad[64];
    static int nok, nbad;
    if (f == VK_FORMAT_UNDEFINED) return 0;
    for (int i = 0; i < nok; i++)
        if (ok[i] == f) return 1;
    for (int i = 0; i < nbad; i++)
        if (bad[i] == f) return 0;
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
    int r = (p.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0;
    if (r && nok < 64) ok[nok++] = f;
    if (!r && nbad < 64) {
        bad[nbad++] = f;
        WARN("vulkan: the GPU can't read vertex format %d (those attributes read as 0,0,0,1)\n", (int)f);
    }
    return r;
}

/* bytes an attribute of a size code (bits 21-26) reads */
static u32 attr_bytes(u32 size) {
    switch (size) {
        case 0x01: return 16;
        case 0x02: return 12;
        case 0x03:
        case 0x04: return 8;
        case 0x05: return 6;
        case 0x13: return 3;
        case 0x18:
        case 0x1B: return 2;
        case 0x1D: return 1;
    }
    return 4; /* 0x0A, 0x0F, 0x12, 0x30, 0x31 */
}
/* the bytes of a component of a vertex attribute of that size, which Vulkan fetches it only from multiples of: a packed
   format's word as one */
static u32 attr_component_bytes(u32 size) {
    switch (size) {
        case 0x03:
        case 0x05:
        case 0x0F:
        case 0x1B: return 2;
        case 0x0A:
        case 0x13:
        case 0x18:
        case 0x1D: return 1;
    }
    return 4;
}

/* ------------------------------------------------------------ resources of a draw */
static u32 tex_handle(const GpuChannel* ch, int stage, u32 tcb, const ShTex* st) {
    u32 ci = (st->bindless ? st->bl_cbuf : tcb) & 31, c2i = st->bl2_cbuf & 31;
    u64 cba = ci < 18 ? ch->cbind[stage][ci] : 0;
    u32 h = cba ? gmem_r32(cba + st->cbuf_off * 4) : 0;
    if (st->bindless && st->bl2) {
        u64 c2 = c2i < 18 ? ch->cbind[stage][c2i] : 0;
        if (c2) h |= gmem_r32(c2 + st->bl2_off * 4);
    }
    return h;
}

/* a texture binding of a draw: the headers its handle names, and the texture */
typedef struct {
    u32 handle, have;
    u32 tic[8], tsc[8];
    Surf* t;
} TexRes;
/* What a texture handle resolved to, kept while nothing it came from can have changed: the pools' addresses, the
   surface table (g_vk_surf_epoch), the CPU's writes the stream can see (vk_ww_epoch), the results pending over guest
   memory (none was added: g_pend_any.adds) and the GPU's writes to the two headers. A draw resolved each of its
   textures again, reading both headers and finding the texture by them: a tenth of the GPU thread in one game, for
   the same few hundred handles every draw. A kept texture is still checked against its memory
   (vk_tex_recheck). A render target read whole as the texture is kept too (rt): its Surf stays the target while the
   surface table doesn't change, and a hit does what tex_from_tic does for it each draw, the read check
   (vk_rt_note_read) and whether its contents are defined. Those were a twentieth of the GPU thread in one game,
   which samples its targets in most draws. A crop of a target, or a texture over a target's memory read from memory,
   isn't kept (their contents change with drawing) */
typedef struct {
    const GpuChannel* ch;
    u64 tic_base, tsc_base, gw, adds;
    u32 handle, surf_epoch, ww_epoch;
    u32 tic[8], tsc[8];
    Surf* t;
    int rt; /* t is the render target itself, read whole */
} TexMemo;
#define TEX_MEMO 1024
static TexMemo g_tex_memo[TEX_MEMO];
static void tex_resolve(GpuChannel* ch, u32 handle, TexRes* x) {
    u32* r = ch->r3d;
    if (r[0x48D]) {
        /* samplers are taken by the handle's sampler index. Ryujinx reads this register as the sampler index mode,
           where one mode takes the texture's index instead: whether a game uses it isn't known yet */
        LOG_LIMIT(1, "vulkan: sampler index mode %u\n", r[0x48D]);
    }
    u64 tic_base = ((u64)r[0x55D] << 32) | r[0x55E], tsc_base = ((u64)r[0x557] << 32) | r[0x558];
    u64 tic_at = tic_base + (handle & 0xFFFFF) * 32ull, tsc_at = tsc_base + ((handle >> 20) & 0xFFF) * 32ull;
    TexMemo* m = &g_tex_memo[(handle * 0x9E3779B1u) >> 22];
    u64 gw = gpu_mem_generation();
    x->handle = handle;
    x->have = 1;
    /* an index past its pool's limit (0x55F the headers', 0x559 the samplers') names no header: no texture, or the
       default sampler, as Eden's VisitImageView and the sampler table have it. One game's movie overlay hands
       texture indices 0x2000 past a 2048-entry pool, where shader code lies */
    if ((handle & 0xFFFFF) > r[0x55F] || ((handle >> 20) & 0xFFF) > r[0x559]) {
        if ((handle & 0xFFFFF) > r[0x55F]) {
            memset(x->tic, 0, sizeof x->tic);
            x->t = NULL;
        } else {
            vk_gmem_words(tic_at, x->tic, 8);
            x->t = vk_tex_from_tic(x->tic);
        }
        if (((handle >> 20) & 0xFFF) > r[0x559])
            memset(x->tsc, 0, sizeof x->tsc);
        else
            vk_gmem_words(tsc_at, x->tsc, 8);
        LOG_LIMIT(1, "vulkan: a texture handle %08x names headers past the pools' limits (%x, %x): no texture there\n",
                  handle, r[0x55F], r[0x559]);
        return;
    }
    if (m->t && m->ch == ch && m->handle == handle && m->tic_base == tic_base && m->tsc_base == tsc_base &&
        m->surf_epoch == g_vk_surf_epoch && m->ww_epoch == vk_ww_epoch() && !g_gfx_img_any &&
        m->adds == __atomic_load_n(&g_pend_any.adds, __ATOMIC_RELAXED) &&
        (m->gw == gw || (!gpu_mem_dirty_since(m->gw, tic_at, 32) && !gpu_mem_dirty_since(m->gw, tsc_at, 32)))) {
        m->gw = gw;
        memcpy(x->tic, m->tic, sizeof x->tic);
        memcpy(x->tsc, m->tsc, sizeof x->tsc);
        if (m->rt) {
            if (m->t->layout != VK_IMAGE_LAYOUT_UNDEFINED) vk_rt_note_read(m->t);
            if (m->t->layout != VK_IMAGE_LAYOUT_UNDEFINED) {
                x->t = m->t;
                return;
            }
            m->t = NULL; /* undefined: memory has its texels, resolved below */
        } else {
            x->t = vk_tex_recheck(m->t);
            if (!x->t) m->t = NULL;
            return;
        }
    }
    vk_gmem_words(tic_at, x->tic, 8);
    vk_gmem_words(tsc_at, x->tsc, 8);
    x->t = vk_tex_from_tic(x->tic);
    m->t = NULL;
    Surf* at = x->t ? vk_rt_find(x->t->gva) : NULL;
    if (x->t && (x->t->is_rt ? x->t == at : !at)) {
        m->rt = x->t->is_rt;
        m->ch = ch;
        m->handle = handle;
        m->tic_base = tic_base;
        m->tsc_base = tsc_base;
        m->gw = gpu_mem_generation();
        m->adds = __atomic_load_n(&g_pend_any.adds, __ATOMIC_RELAXED);
        m->surf_epoch = g_vk_surf_epoch;
        m->ww_epoch = vk_ww_epoch();
        memcpy(m->tic, x->tic, sizeof m->tic);
        memcpy(m->tsc, x->tsc, sizeof m->tsc);
        m->t = x->t;
    }
}

int vk_tex_num(const u32* tic, const Surf* t) {
    VkFmt f;
    if (vk_tic_reads_stencil(tic)) return 1; /* a depth target's stencil is unsigned, whatever the surface holds */
    f = t ? t->f : vk_tic_format(tic[0], 0);
    return f.is_int ? (f.sint ? 2 : 1) : 0;
}

int vk_image_keyable(const ShaderInfo* si, int i) {
    return (si->img[i].type == 3 || si->img[i].type == 4 || si->img[i].type == 5) && si->img[i].kind != 1;
}

int vk_image_key(const ShaderInfo* si, int i, const u32* tic) {
    return !!vk_tic_format(tic[0], 0).is_int != !!si->img[i].bindless;
}

VkImageView vk_storage_image_view(Surf* t, const ShaderInfo* si, int i, u64* bytes) {
    int volume = si->img[i].type == 5;
    int kind = si->img[i].kind;
    *bytes = vk_surf_guest_size(t);
    /* a 3D surface is a block linear 3D texture at its own size, written back as one volume (vk_surf_flush) */
    if (!t || !t->storage_capable || !vk_surf_can_writeback(t) || !t->levels ||
        (volume ? t->type != VK_IMAGE_VIEW_TYPE_3D || !t->bl || t->sw != t->w || t->sh != t->h
                : t->depth3d != 1 || (si->img[i].type != 3 && si->img[i].type != 4)) ||
        !*bytes || *bytes > UINT32_MAX)
        return VK_NULL_HANDLE;
    if (kind == 1 ? t->f.vk != VK_FORMAT_R32_UINT
                  : !vk_format_storage_write(t->f.vk) || kind != (t->f.is_int ? t->f.sint ? 3 : 2 : 0))
        return VK_NULL_HANDLE;
    /* a 2D array surface sees every layer of the image, a single layer one too */
    return vk_tex_storage_view(t, volume ? VK_IMAGE_VIEW_TYPE_3D
                                   : si->img[i].type == 4 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
}

u32 vk_gbuf_desc(int stage, u32 off, const u32* v, u64* addr) {
    u32 size = stage == ST_VS && off == 0xb0 ? 16 : v[2];
    *addr = ((u64)v[1] << 32) | v[0];
    if (!shader_gbuf_sized(stage, off) && off != 0xb0) {
        /* a bare address in the program's own constant buffer has no size beside it: the memory mapped from it on,
           whole words, to the cap below (Eden binds such a buffer to the end of its mapping, at most 8 MiB) */
        u64 cpu = 0;
        size = *addr ? (u32)gmmu_run(*addr, 16u << 20, &cpu) & ~3u : 0;
    }
    if (!size) size = 16;
    if (size > (16u << 20)) size = 16u << 20;
    return (size + 3) & ~3u;
}

/* the images of s, stage's program, whose bound texture is integer where the program's handle form stores floats, or
   the other way (vk_image_key): a bit each, which bits 8 on of the program's rt_int_mask take to pick its variant */
static u32 image_variant(const GpuChannel* ch, int stage, const VkSh* s, u32* signed_images) {
    const u32* r = ch->r3d;
    u64 tic_base = ((u64)r[0x55D] << 32) | r[0x55E];
    u32 bits = 0;
    *signed_images = 0;
    for (int i = 0; i < s->si.nimg; i++) {
        if (!vk_image_keyable(&s->si, i)) continue;
        ShTex handle_info = {
            .cbuf_off = s->si.img[i].cbuf_off, .bindless = s->si.img[i].bindless, .bl_cbuf = s->si.img[i].bl_cbuf};
        u32 handle = tex_handle(ch, stage, r[0x982], &handle_info), tic[8];
        vk_gmem_words(tic_base + (handle & 0xFFFFF) * 32ull, tic, 8);
        if (vk_image_key(&s->si, i, tic)) bits |= 1u << i;
        if (vk_tex_num(tic, NULL) == 2) *signed_images |= 1u << i;
    }
    return bits;
}

/* What picks the fragment shader's variant: which of its textures and outputs are integer (signed), its color textures
   compared as depth and their functions, and its inputs the stage before doesn't write. 0s when the program itself
   is drawn */
typedef struct {
    u64 tm, ts, dm;
    u32 rm, rs, df, unfed, dual;
} FsKey;
/* the integer color targets (rm, signed rs) the registers name, which key the fragment shader's outputs */
static void rt_int_targets(const u32* r, u32* rm, u32* rs) {
    u32 ctl = r[0x487], count = ctl & 15;
    if (count > 8) count = 8;
    *rm = *rs = 0;
    for (u32 i = 0; i < count; i++) {
        const u32* t = &r[0x200 + ((ctl >> (4 + i * 3)) & 7) * 0x10];
        if (!t[4]) continue;
        VkFmt f = vk_rt_format(t[4]);
        if (f.is_int) {
            *rm |= 1u << i;
            if (f.sint) *rs |= 1u << i;
        }
    }
}
/* resolves the textures of s0, the fragment program, into res (SH_MAXTEX entries, for stage_set), and fills fk with
   the variant they and the targets (rt_int, rt_sint) key. last_out: the generic outputs the stage before rasterization
   writes */
static void fs_key(GpuChannel* ch, const VkSh* s0, TexRes* res, u32 last_out, u32 rt_int, u32 rt_sint, FsKey* fk) {
    u32* r = ch->r3d;
    u32 nd = 0;
    *fk = (FsKey){.rm = rt_int, .rs = rt_sint, .unfed = s0->si.in_mask & ~last_out};
    // With one render target the guest's second color can feed the blend unit.
    u32 ctl=r[0x487];
    const u32 *second=r+0x200+((ctl>>7)&7)*0x10;
    bool secondTarget=(ctl&15)>=2 && (second[0] || second[1]) && second[2] && second[3] && second[4];
    if(g_vk.feat.dualSrcBlend && !secondTarget && r[0x4d8] &&
       (s0->si.sph[18]&0xf0) && !(s0->si.sph[18]&~255u)){
        const u32 *b=r[0x4b9]?r+0x780:r+0x4cf;
        u32 factors[]={b[2],b[3],b[5],b[r[0x4b9]?6:7]};
        for(unsigned j=0;j<4;j++){
            VkBlendFactor f=blend_factor(factors[j]);
            if(f>=VK_BLEND_FACTOR_SRC1_COLOR && f<=VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA)fk->dual=1;
        }
    }
    int nt = s0->si.ntex > SH_MAXTEX ? SH_MAXTEX : s0->si.ntex;
    for (int i = 0; i < nt; i++) {
        const ShTex* st = &s0->si.tex[i];
        tex_resolve(ch, tex_handle(ch, 4, r[0x982], st), &res[i]);
        if (st->shadow) {
            if (st->flags & SNX_TEX_INDEXED) continue; /* the selected entry's function comes from the table */
            /* a depth compare of a color texture compares its first component, which Vulkan's compare samplers
               can't do on a color image: the shader samples it and compares (the TSC's function, LEQUAL when the
               TSC has none, as vk_tex_bind_sampler sets) */
            Surf* t = res[i].t;
            if (t && !t->f.depth && !t->f.is_int) {
                u32 func = (res[i].tsc[0] >> 9) & 1 ? (res[i].tsc[0] >> 10) & 7 : 3;
                fk->dm |= 1ull << i;
                if (nd < SH_DC_COLOR_MAX) fk->df |= func << (3 * nd);
                nd++;
            }
            continue;
        }
        int num = vk_tex_num(res[i].tic, res[i].t);
        if (num) fk->tm |= 1ull << i;
        if (num == 2) fk->ts |= 1ull << i;
    }
    fk->rm &= s0->si.sph[18] ? 0xFF : 0;
    fk->rs &= fk->rm;
    u32 signed_images;
    fk->rm |= image_variant(ch, 4, s0, &signed_images) << 8;
    fk->rs |= signed_images << 8;
}
/* the fragment shader at addr in the variant fk names, s0 when it names none */
static VkSh* fs_variant(GpuChannel* ch, u64 addr, VkSh* s0, const FsKey* fk) {
    if (!fk->tm && !fk->rm && !fk->rs && !fk->dm && !fk->unfed && !fk->dual) return s0;
    return vk_shader_get(ch, addr, 0, ST_FS, fk->tm, fk->ts, fk->rm, fk->rs, fk->dual, fk->dm, fk->df, fk->unfed, NULL);
}

VkImageViewType vk_tex_view_type(const ShTex* st) {
    return st->type == 2   ? VK_IMAGE_VIEW_TYPE_3D
           : st->type == 3 ? (st->array ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE)
           : st->array     ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                           : VK_IMAGE_VIEW_TYPE_2D;
}

/* The TIC selects an sRGB-capable format; the TSC controls whether sampling unpacks it to linear. */
static int tex_srgb(const u32* tic, const u32* tsc) { return ((tic[4] >> 22) & 1) && ((tsc[0] >> 13) & 1); }

/* the component mapping of t's view for the header tic */
static VkComponentMapping tex_map(const Surf* t, const u32* tic) {
    static const VkComponentSwizzle sw[8] = {
        VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_R,   VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,    VK_COMPONENT_SWIZZLE_A,    VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ONE};
    VkComponentSwizzle m[4] = {sw[(tic[0] >> 19) & 7], sw[(tic[0] >> 22) & 7], sw[(tic[0] >> 25) & 7],
                               sw[(tic[0] >> 28) & 7]};
    /* the header's swizzle is in memory component order: B, G, R, A targets swap R and B */
    if (t->f.bgra)
        for (int k = 0; k < 4; k++)
            m[k] = m[k] == VK_COMPONENT_SWIZZLE_R   ? VK_COMPONENT_SWIZZLE_B
                   : m[k] == VK_COMPONENT_SWIZZLE_B ? VK_COMPONENT_SWIZZLE_R
                                                    : m[k];
    if (t->f.depth) { /* depth in r, g and b, alpha 1 */
        m[0] = m[1] = m[2] = VK_COMPONENT_SWIZZLE_R;
        m[3] = VK_COMPONENT_SWIZZLE_ONE;
    }
    return t->fkey & (1u << 30) ? (VkComponentMapping){VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ZERO,
                                                       VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE}
                                : (VkComponentMapping){m[0], m[1], m[2], m[3]};
}

VkImageView vk_tex_bind_view(Surf* t, VkImageViewType want, const u32* tic, const u32* tsc, int num) {
    int stencil = t && t->f.stencil && vk_tic_reads_stencil(tic);
    if (!t || (stencil ? 1 : t->f.is_int ? (t->f.sint ? 2 : 1) : 0) != num) return VK_NULL_HANDLE;
    VkComponentMapping map = tex_map(t, tic);
    return vk_tex_view(t, want, tex_srgb(tic, tsc), &map, stencil);
}

/* each component of map is its own: what a min/max sampler reads where the device takes no other swizzle */
static int tex_map_identity(VkComponentMapping map) {
    return (map.r == VK_COMPONENT_SWIZZLE_IDENTITY || map.r == VK_COMPONENT_SWIZZLE_R) &&
           (map.g == VK_COMPONENT_SWIZZLE_IDENTITY || map.g == VK_COMPONENT_SWIZZLE_G) &&
           (map.b == VK_COMPONENT_SWIZZLE_IDENTITY || map.b == VK_COMPONENT_SWIZZLE_B) &&
           (map.a == VK_COMPONENT_SWIZZLE_IDENTITY || map.a == VK_COMPONENT_SWIZZLE_A);
}

/* the texture (NULL for none) and the view type say whether anisotropy= may raise its filtering: 2D textures with
   mipmaps only, as the yuzu family and Ryujinx do (a cube, a 3D LUT or a copied target keeps the game's) */
VkSampler vk_tex_bind_sampler(const u32* tsc, const u32* tic, const ShTex* st, int num, const Surf* tex,
                              VkImageViewType want) {
    u32 tc[8];
    memcpy(tc, tsc, sizeof tc);
    /* the compare mode is what the shader declares */
    if (st->shadow) {
        if (!((tc[0] >> 9) & 1)) tc[0] = (tc[0] & ~(7u << 10)) | 1u << 9 | 3u << 10; /* LEQUAL */
    } else
        tc[0] &= ~(1u << 9);
    /* integer textures can't be filtered: nearest, and a linear mip filter becomes nearest */
    if (num) {
        u32 mip = (tc[1] >> 6) & 3;
        tc[1] = (tc[1] & ~0xF3u) | 0x11u | (mip == 3 ? 2u : mip) << 6;
    }
    /* a min/max reduction (tc[1] bits 10-11) stays where it can change what the filter reads and Vulkan takes it: a
       linear filter (magnification, minification or mip) of a view whose format has the min/max filter on this device,
       without a compare, which Vulkan's samplers only average. Where the device takes no swizzle with it
       (filterMinmaxImageComponentMapping), the view's mapping must be the identity too: Vulkan needs r to be, and the
       other components are poison, which a shader that reads them (a depth view's g, b and a) would get. Elsewhere the
       sampler averages: the null texture's texels are all alike, and nearest filters read one */
    u32 reduction = (tc[1] >> 10) & 3;
    if (reduction == 1 || reduction == 2) {
        int linear = (tc[1] & 3) == 2 || ((tc[1] >> 4) & 3) == 2 || ((tc[1] >> 6) & 3) == 3;
        const char* which = reduction == 1 ? "least" : "greatest";
        if (!tex || num || !linear)
            tc[1] &= ~(3u << 10);
        else if ((tc[0] >> 9) & 1) {
            LOG_LIMIT(1,
                      "vulkan: a depth compare sampler asks for the %s of its texels, which Vulkan's compare samplers "
                      "don't take: they average them\n",
                      which);
            tc[1] &= ~(3u << 10);
        } else if (!g_vk.feat12.samplerFilterMinmax) {
            LOG_LIMIT(1,
                      "vulkan: the device has no min/max filter: a sampler asking for the %s of its texels "
                      "averages them\n",
                      which);
            tc[1] &= ~(3u << 10);
        } else {
            VkFormat f = vk_tex_view_format(tex, tex_srgb(tic, tsc));
            if (!vk_format_minmax_filter(f)) {
                LOG_LIMIT(1,
                          "vulkan: VkFormat %d has no min/max filter on this device: a sampler asking for the %s of "
                          "its texels averages them\n",
                          (int)f, which);
                tc[1] &= ~(3u << 10);
            } else if (!g_vk.has_minmax_swizzle && !tex_map_identity(tex_map(tex, tic))) {
                LOG_LIMIT(1,
                          "vulkan: the device's min/max filter takes no swizzle: a sampler asking for the %s of the "
                          "texels of a swizzled view averages them\n",
                          which);
                tc[1] &= ~(3u << 10);
            }
        }
    }
    /* raised only with anisotropy= set: without it the cache keeps one sampler per TSC */
    int raise = g_vk_anisotropy > 1 && tex && tex->levels > 1 &&
                (want == VK_IMAGE_VIEW_TYPE_2D || want == VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    return vk_sampler(tc, raise, num != 0);
}

/* a constant buffer's bytes for this draw, reused while the guest can't have changed them */
static int cb_get(const GpuChannel* ch, int stage, int i, u32 sz, VkBuffer* buf, u64* off) {
    static struct {
        u64 a, gw, frame, cs_adds, gfx_adds;
        u32 sz, epoch;
        VkBuffer buf;
        u64 off;
    } c[5][18];
    u64 a = ch->cbind[stage][i];
    int pending;
    /* checked as it is cached, with no compute or graphics result added since: none can cover it, and only a write of
       its memory leaves the copy stale. The pending checks were the most of cb_get's time in one game */
    if (a && c[stage][i].a == a && c[stage][i].epoch == g_vk_epoch && c[stage][i].frame == vk_submit_number() &&
        c[stage][i].sz >= sz && c[stage][i].cs_adds == vk_compute_adds() && c[stage][i].gfx_adds == vk_buf_adds() &&
        !gpu_mem_dirty_since(c[stage][i].gw, a, sz)) {
        *buf = c[stage][i].buf;
        *off = c[stage][i].off;
        return 1;
    }
    /* Pending compute results there land first. A pending graphics result is newer than the guest memory the checks
       below see: the buffer then comes from vk_buf_get_strict, which reads it on the GPU, and isn't cached */
    if (g_cs_any && a) vk_compute_mem_need(a, sz);
    pending = g_gfx_any && a && vk_buf_pending(a, sz);
    if (!pending && a && c[stage][i].a == a && c[stage][i].epoch == g_vk_epoch &&
        c[stage][i].frame == vk_submit_number() && c[stage][i].sz >= sz &&
        !gpu_mem_dirty_since(c[stage][i].gw, a, sz)) {
        *buf = c[stage][i].buf;
        *off = c[stage][i].off;
        return 1;
    }
    if (!(a ? vk_buf_get_strict(a, sz, buf, off) : vk_buf_copy(a, sz, buf, off))) return 0;
    if (pending) {
        c[stage][i].a = 0;
        return 1;
    }
    c[stage][i].a = a;
    c[stage][i].cs_adds = vk_compute_adds();
    c[stage][i].gfx_adds = vk_buf_adds();
    c[stage][i].gw = gpu_mem_generation();
    c[stage][i].frame = vk_submit_number();
    c[stage][i].sz = sz;
    c[stage][i].epoch = g_vk_epoch;
    c[stage][i].buf = *buf;
    c[stage][i].off = *off;
    return 1;
}

/* the bytes of constant buffer i that stage s reads, as its descriptor binds them */
static u32 cb_range(const GpuChannel* ch, int stage, const VkSh* s, int i) {
    u32 sz = ch->cbind_size[stage][i];
    if (sz > 65536 || !sz) sz = 65536;
    if (s->si.cbuf_max[i] && sz > s->si.cbuf_max[i]) sz = s->si.cbuf_max[i]; /* only the bytes the shader reads */
    sz = (sz + 15) & ~15u;
    if (sz > g_vk.props.limits.maxUniformBufferRange) sz = g_vk.props.limits.maxUniformBufferRange;
    return sz;
}
/* A third of a draw's constant buffers miss cb_get's entries, and each of those looks its range up in the buffer
   cache: two cache misses in a row, the index's slot and then the entry it names, which were most of buf_get's time
   in one game. A draw fetches every slot early (step 0) and the entries they name once the slots are in (step 1), as
   it does for its vertex streams (vk_buf_prefetch) */
static void cb_prefetch(const GpuChannel* ch, int stage, const VkSh* s, int step) {
    for (u32 m = s->si.cbuf_mask & 0x3FFFF; m; m &= m - 1) {
        int i = __builtin_ctz(m);
        if (ch->cbind[stage][i]) vk_buf_prefetch(ch->cbind[stage][i], cb_range(ch, stage, s, i), step);
    }
}

/* small constants in this frame's staging: 1/resolution scale (SNX_RS) at 0, the 0,0,0,1 of attributes without
   data at 16 (float) and 32 (integer) */
static int frame_consts(VkBuffer* buf, u64* off, float raster_scale) {
    static u64 frame = ~0ull, o;
    static float cached_scale;
    static VkBuffer b;
    if (frame != vk_submit_number() || cached_scale != raster_scale) {
        u8* p = vk_stage(64, vk_stage_align(), &b, &o);
        if (!p) return 0;
        float rs[4] = {1 / raster_scale, 1 / raster_scale, raster_scale, 0}, fa[4] = {0, 0, 0, 1};
        u32 ia[4] = {0, 0, 0, 1};
        memcpy(p, rs, 16);
        memcpy(p + 16, fa, 16);
        memcpy(p + 32, ia, 16);
        memset(p + 48, 0, 16); /* empty vertex bindings retain their original format */
        snx_le32_array(p, 12); /* the device reads them little-endian */
        frame = vk_submit_number();
        cached_scale = raster_scale;
    }
    *buf = b;
    *off = o;
    return 1;
}

typedef struct {
    VkWriteDescriptorSet w[SNX_VKB_END];
    VkDescriptorBufferInfo bi[SNX_VKB_END];
    VkDescriptorImageInfo ii[SNX_VKB_SMAP + 1];
    VkDescriptorImageInfo depth_table[SNX_TEX_ARRAY];
    VkDescriptorImageInfo table[SNX_TEX_ARRAY];
    u32 n;
} DescWrites;

/* Fragment programs' storage images, written back to guest memory: each with its bytes from before the draw (original,
   for the merge with the CPU's writes since) and the pages it covers. A release puts their write-back aside: their
   copy is recorded into the frame's host-readable ring (sub, the submit it is in, back, wgen the write generation it
   copies), and they land once that submit is done (vk_gfx_image_frame_done), as storage buffers' results do. Before,
   each release copied and waited for the GPU: half of one game's frame time in vk_finish. Whatever needs their memory
   sooner lands them at once, waiting for that submit when its copy is recorded */
typedef struct {
    Surf* surf;
    u8* original;
    u32 size;
    u64* pages;
    size_t npages;
    u64 sub, back_off, back_size, wgen;
    u8* back;
    VmaAllocation back_mem;
} GfxImageOut;
static GfxImageOut* g_img_out;
static size_t g_img_count, g_img_cap;
static _Thread_local int g_img_landing;
_Atomic int g_gfx_img_any;

static int image_page_cmp(const void* a, const void* b) {
    u64 x = *(const u64*)a, y = *(const u64*)b;
    return (x > y) - (x < y);
}
/* surf has a write-back waiting whose copy isn't recorded yet: a later draw's writes are in it */
static int image_pending(Surf* surf) {
    for (size_t i = 0; i < g_img_count; i++)
        if (g_img_out[i].surf == surf && !g_img_out[i].sub) return 1;
    return 0;
}
static void image_record(Surf* surf, u8* original, u32 size) {
    if (image_pending(surf)) {
        free(original);
        return;
    }
    if (g_img_count == g_img_cap) {
        size_t cap = g_img_cap ? g_img_cap * 2 : 8;
        GfxImageOut* next = realloc(g_img_out, cap * sizeof *next);
        if (!next) fatal("Out of memory recording graphics storage image");
        g_img_out = next;
        g_img_cap = cap;
    }
    size_t max_pages = (size_t)(((surf->gva & 4095) + (u64)size + 4095) >> 12);
    u64* pages = malloc(max_pages * sizeof *pages);
    if (!pages) fatal("Out of memory recording graphics storage image pages");
    size_t npages = 0;
    u64 first = surf->gva & ~4095ull;
    for (size_t i = 0; i < max_pages; i++) {
        u64 cpu = gmmu_translate(first + (u64)i * 4096);
        if (cpu) pages[npages++] = cpu >> 12;
    }
    qsort(pages, npages, sizeof *pages, image_page_cmp);
    g_img_out[g_img_count++] = (GfxImageOut){.surf = surf, .original = original, .size = size, .pages = pages,
                                             .npages = npages};
    g_gfx_img_any = 1;
}
static int image_overlaps(const GfxImageOut* out, u64 gva, u64 size) {
    if (!size) return 0;
    u64 base = out->surf->gva;
    if (gva >= base ? gva - base < out->size : base - gva < size) return 1;
    u64 first = gva & ~4095ull, last = (gva + size - 1) & ~4095ull;
    for (u64 at = first; at <= last; at += 4096) {
        u64 cpu = gmmu_translate(at), page = cpu >> 12;
        if (cpu && bsearch(&page, out->pages, out->npages, sizeof page, image_page_cmp)) return 1;
        if (last - at < 4096) break;
    }
    return 0;
}
static void image_remove(size_t index) {
    free(g_img_out[index].original);
    free(g_img_out[index].pages);
    memmove(g_img_out + index, g_img_out + index + 1, (g_img_count - index - 1) * sizeof *g_img_out);
    g_img_count--;
    g_gfx_img_any = g_img_count != 0;
}
/* an entry whose copy's submit is done, landed from the ring */
static void image_land(size_t index) {
    GfxImageOut* o = &g_img_out[index];
    vmaInvalidateAllocation(g_vk.vma, o->back_mem, o->back_off, o->back_size);
    vk_surf_flush_land(o->surf, o->back, o->original, o->wgen);
    image_remove(index);
}
/* the entry whose bytes from before the draw are original (each its own allocation): waiting for the GPU can land
   others and move the entries, so an entry is found again after a wait */
static size_t image_find(const u8* original) {
    size_t k = 0;
    while (k < g_img_count && g_img_out[k].original != original) k++;
    return k;
}
static void image_flush_at(size_t index) {
    GfxImageOut out = g_img_out[index];
    SNX_ZONE(z, "vk image flush");
    if (out.sub) { /* its copy is recorded: once its submit is done, from there */
        if (out.sub > vk_last_submit()) vk_submit(VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
        vk_wait_submit(out.sub);
        index = image_find(out.original);
        if (index < g_img_count) image_land(index); /* not landed by a frame's landing meanwhile */
        return;
    }
    vk_surf_flush(out.surf, out.original);
    if (out.surf->mem_wgen != out.surf->wgen) fatal("Cannot write back graphics storage image");
    index = image_find(out.original);
    if (index < g_img_count) image_remove(index);
}
void vk_gfx_image_mem_need(u64 gva, u64 size) {
    if (!g_gfx_img_any || g_img_landing || !size) return;
    SNX_ZONE(z, "vk image mem need");
    vk_lock();
    g_img_landing = 1;
    for (size_t i = 0; i < g_img_count;)
        if (image_overlaps(&g_img_out[i], gva, size))
            image_flush_at(i);
        else
            i++;
    g_img_landing = 0;
    vk_unlock();
}
/* every entry holds its surface until it lands, its copy recorded or not (image_pending leaves those out) */
int vk_gfx_image_holds(const Surf* s) {
    for (size_t i = 0; g_gfx_img_any && i < g_img_count; i++)
        if (g_img_out[i].surf == s) return 1;
    return 0;
}
int vk_gfx_image_pending(u64 gva, u64 size) {
    int hit = 0;
    if (!g_gfx_img_any || !size) return 0;
    vk_lock();
    for (size_t i = 0; i < g_img_count && !hit; i++) hit = image_overlaps(&g_img_out[i], gva, size);
    vk_unlock();
    return hit;
}
void vk_gfx_image_land(void) {
    if (!g_gfx_img_any || g_img_landing) return;
    SNX_ZONE(z, "vk image land");
    vk_lock();
    g_img_landing = 1;
    while (g_img_count) image_flush_at(0);
    g_img_landing = 0;
    vk_unlock();
}
void vk_gfx_image_put_aside(void) {
    if (!g_gfx_img_any || g_img_landing) return;
    SNX_ZONE(z, "vk image put aside");
    vk_lock();
    g_img_landing = 1;
    vk_cmd(); /* a new frame, if one begins, lands what it lands before the entries are walked */
    for (size_t i = 0; i < g_img_count;) {
        GfxImageOut* o = &g_img_out[i];
        u64 size, off;
        VkBuffer buf;
        VmaAllocation mem;
        u8* p;
        if (o->sub) {
            i++;
            continue;
        }
        size = vk_surf_flush_size(o->surf);
        p = size ? vk_stage_back(size, &buf, &off, &mem) : NULL;
        if (!p) { /* nothing to copy, or no room in the ring: at once */
            image_flush_at(i);
            continue;
        }
        vk_surf_flush_record(o->surf, buf, off);
        o->sub = vk_submit_number();
        o->back = p;
        o->back_mem = mem;
        o->back_off = off;
        o->back_size = size;
        o->wgen = o->surf->wgen;
        i++;
    }
    g_img_landing = 0;
    vk_unlock();
}
int vk_gfx_image_in_flight(int unsubmitted) {
    for (size_t i = 0; i < g_img_count; i++)
        if (g_img_out[i].sub && (!unsubmitted || g_img_out[i].sub > vk_last_submit())) return 1;
    return 0;
}
void vk_gfx_image_frame_done(u64 sub) {
    int landing = g_img_landing;
    g_img_landing = 1; /* nothing lands the entries over again while they are walked */
    for (size_t i = 0; i < g_img_count;)
        if (g_img_out[i].sub && g_img_out[i].sub <= sub)
            image_land(i);
        else
            i++;
    g_img_landing = landing;
}

typedef struct { /* the vertex and fragment programs' images, 8 each */
    Surf* surf[16];
    u8* original[16];
    u32 size[16];
    int n;
} DrawImages;
static void draw_images_discard(DrawImages* images) {
    for (int i = 0; i < images->n; i++) free(images->original[i]);
    images->n = 0;
}
static void draw_images_commit(DrawImages* images) {
    for (int i = 0; i < images->n; i++) {
        vk_surf_wrote(images->surf[i]);
        if (images->original[i]) image_record(images->surf[i], images->original[i], images->size[i]);
    }
    images->n = 0;
}
static void dw_buffer(DescWrites* d, VkDescriptorSet set, u32 binding, VkDescriptorType type, VkBuffer b, u64 off,
                      u64 range) {
    d->bi[binding] = (VkDescriptorBufferInfo){b, off, range};
    d->w[d->n++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                          .dstSet = set,
                                          .dstBinding = binding,
                                          .descriptorCount = 1,
                                          .descriptorType = type,
                                          .pBufferInfo = &d->bi[binding]};
}

static int is_target(Surf* s, Surf** rt, Surf* zt, u32 n) {
    if (s == zt) return 1;
    for (u32 i = 0; i < n; i++)
        if (rt[i] == s) return 1;
    return 0;
}

/* The elements of SNX_VKB_TEXARRAY, from an indexed texture slot: SNX_TEX_ARRAY handles from the slot's offset on,
   (1 << shift) bytes apart. A handle past the constant buffer's size, or with a header or sampler index past its
   pool's limit (no texture on hardware either), reads as the null texture. So does a texture of another kind than
   the slot's, and a target the draw renders to (the bound path's snapshot, for every element, would copy each
   target the table names on every draw). */
static int tex_tables_set(GpuChannel* ch, int stage, const VkSh* s, const int* first, int count, Surf** rt, Surf* zt,
                          u32 n, VkImageLayout layout, DescWrites* d, VkDescriptorSet set) {
    const ShTex* kind = &s->si.tex[first[0]];
    VkImageViewType want = vk_tex_view_type(kind);
    int num = kind->shadow || !((s->si.tex_int_mask >> first[0]) & 1) ? 0
              : ((s->si.tex_sint_mask >> first[0]) & 1)               ? 2
                                                                      : 1;
    ShTex plain = *kind;
    if (kind->shadow) plain.shadow = 0;
    VkImageView none = vk_null_view(want, num);
    /* not far: windef.h defines it away */
    VkImageView farthest = kind->shadow ? vk_null_view(want, 3) : VK_NULL_HANDLE;
    if (!none || (kind->shadow && !farthest)) return 0;
    u32 handles[SNX_TEX_ARRAY] = {0};
    u32 null_tsc[8] = {0};
    u32 funcs[SNX_TEX_ARRAY] = {0};
    /* each element's entry, NO_ENTRY for one past its constant buffer: the null texture, given an entry only when an
       element needs it, so a table of SNX_TEX_ARRAY real handles fits (edenprobe's bindless suite samples 256) */
    enum { NO_ENTRY = 0xFFFFFFFFu };
    u32 map[SH_INDEXED_TABLES * SNX_TEX_ARRAY] = {0};
    u32 used = 0;
    int need_null = 0;
    VkDescriptorImageInfo null_info, null_depth = {0};
    VkSampler null_sampler = vk_tex_bind_sampler(null_tsc, NULL, &plain, num, NULL, want);
    if (!null_sampler) return 0;
    null_info = (VkDescriptorImageInfo){null_sampler, none, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (kind->shadow) {
        VkSampler null_compare = vk_tex_bind_sampler(null_tsc, NULL, kind, 0, NULL, want);
        if (!null_compare) return 0;
        null_depth = (VkDescriptorImageInfo){null_compare, farthest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    for (int table = 0; table < count; table++) {
        const ShTex* st = &s->si.tex[first[table]];
        u32 shift = (u32)st->flags >> 4, bank = st->bl_cbuf & 31;
        u64 cba = bank < 18 ? ch->cbind[stage][bank] : 0;
        u32 size = cba ? ch->cbind_size[stage][bank] : 0;
        if (size > 65536 || !size) size = 65536;
        for (u32 k = 0; k < SNX_TEX_ARRAY; k++) {
            u32 at = st->cbuf_off * 4 + (k << shift);
            /* past the constant buffer: the null texture. A handle of 0 is header 0 with sampler 0, a texture like any
               other (edenprobe's bindless case samples it) */
            if (!cba || at + 4 > size) {
                map[table * SNX_TEX_ARRAY + k] = NO_ENTRY;
                need_null = 1;
                continue;
            }
            u32 handle = gmem_r32(cba + at);
            u32 id = 0;
            while (id < used && handles[id] != handle) id++;
            if (id == used) {
                if (used == SNX_TEX_ARRAY) {
                    LOG_LIMIT(4, "vulkan: indexed texture tables exceed %u unique handles\n", SNX_TEX_ARRAY);
                    return 0;
                }
                handles[id] = handle;
                used++;
                TexRes x = {0};
                VkImageView view = VK_NULL_HANDLE;
                if ((handle & 0xFFFFF) <= ch->r3d[0x55F] && ((handle >> 20) & 0xFFF) <= ch->r3d[0x559]) {
                    tex_resolve(ch, handle, &x);
                    if (x.t && (!kind->shadow || !x.t->f.is_int) && !is_target(x.t, rt, zt, n))
                        view = vk_tex_bind_view(x.t, want, x.tic, x.tsc, num);
                }
                funcs[id] = x.have && (x.tsc[0] >> 9 & 1) ? (x.tsc[0] >> 10) & 7 : 3;
                int depth = kind->shadow && (!view || x.t->f.depth);
                if (depth) funcs[id] |= 8;
                if (view)
                    vk_surf_use(vk_cmd(), x.t, layout, VK_SAMPLED_STAGES, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                else {
                    view = none;
                    /* an unused entry holds 0: a handle that names nothing else is the black-texture signal */
                    if (handle)
                        LOG_LIMIT(4, "vulkan: indexed texture reads the null texture (bank %u handle %08x)\n", bank,
                                  handle);
                }
                VkImageView color_view = depth ? none : view;
                VkSampler smp = vk_tex_bind_sampler(x.tsc, x.tic, &plain, num, color_view != none ? x.t : NULL, want);
                if (!smp) return 0;
                d->table[id] = (VkDescriptorImageInfo){
                    smp, color_view, color_view == none ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : layout};
                if (kind->shadow) {
                    VkImageView depth_view = depth && view != none ? view : farthest;
                    VkSampler compare =
                        vk_tex_bind_sampler(x.tsc, x.tic, kind, 0, depth_view != farthest ? x.t : NULL, want);
                    if (!compare) return 0;
                    d->depth_table[id] = (VkDescriptorImageInfo){
                        compare, depth_view,
                        depth_view == farthest ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : layout};
                }
            }
            map[table * SNX_TEX_ARRAY + k] = id;
        }
    }
    if (need_null) {
        if (used == SNX_TEX_ARRAY) {
            LOG_LIMIT(4, "vulkan: indexed texture tables exceed %u unique handles\n", SNX_TEX_ARRAY);
            return 0;
        }
        for (u32 e = 0; e < (u32)count * SNX_TEX_ARRAY; e++)
            if (map[e] == NO_ENTRY) map[e] = used;
    }
    /* the null texture in every entry left, its own included: the binding is read whole */
    for (u32 k = used; k < SNX_TEX_ARRAY; k++) {
        d->table[k] = null_info;
        if (kind->shadow) {
            d->depth_table[k] = null_depth;
            funcs[k] = 11; /* LEQUAL and null depth */
        }
    }
    VkWriteDescriptorSet* w = &d->w[d->n++];
    *w = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = set,
                                .dstBinding = SNX_VKB_TEXARRAY,
                                .descriptorCount = SNX_TEX_ARRAY,
                                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = d->table};
    if (kind->shadow) {
        VkWriteDescriptorSet* depth_write = &d->w[d->n++];
        *depth_write = *w;
        depth_write->dstBinding = SNX_VKB_TEXDEPTH;
        depth_write->pImageInfo = d->depth_table;
        VkBuffer buffer;
        u64 offset;
        u8* data = vk_stage(sizeof funcs, vk_stage_align(), &buffer, &offset);
        if (!data) return 0;
        memcpy(data, funcs, sizeof funcs);
        snx_le32_array(data, SNX_TEX_ARRAY);
        dw_buffer(d, set, SNX_VKB_TEXFUNCS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer, offset, sizeof funcs);
    }
    if (count) {
        VkBuffer buffer;
        u64 offset;
        u8* data = vk_stage(sizeof map, vk_stage_align(), &buffer, &offset);
        if (!data) return 0;
        memcpy(data, map, sizeof map);
        snx_le32_array(data, sizeof map / 4);
        dw_buffer(d, set, SNX_VKB_TEXMAP, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer, offset, sizeof map);
    }
    return 1;
}

static void draw_storage_read(const GpuChannel* ch, VkDrawStorage* draw, VkSh* vs, VkSh* fs) {
    VkSh* sh[2] = {vs, fs};
    memset(draw, 0, sizeof *draw);
    for (int s = 0; s < 2; s++) {
        if (!sh[s]) continue;
        int stage = s ? 4 : 0;
        u64 cb0 = ch->cbind[stage][0];
        draw->n[s] = sh[s]->si.ngbuf < 16 ? sh[s]->si.ngbuf : 16;
        if (cb0 && draw->n[s]) vk_buf_mem_need(cb0, 0x610);
        for (int i = 0; i < draw->n[s]; i++) {
            VkDrawGbuf* b = &draw->b[s][i];
            u32 v[3] = {0, 0, 0}, desc = sh[s]->si.gbuf_cbuf_off[i];
            /* the constant buffer holding the address (SH_GBUF_*): NVN's descriptors in cbuf 0, or one of the
               program's own */
            u64 cb = ch->cbind[stage][SH_GBUF_BANK(desc)];
            if (cb && SH_GBUF_BANK(desc)) vk_buf_mem_need(cb + SH_GBUF_OFFSET(desc), 8);
            if (cb) vk_gmem_words(cb + SH_GBUF_OFFSET(desc), v, SH_GBUF_BANK(desc) ? 2 : 3);
            b->size = vk_gbuf_desc(stage, desc, v, &b->addr);
            b->bare = SH_GBUF_BANK(desc) != 0;
            b->writable = (sh[s]->si.gbuf_wmask >> i) & 1;
        }
    }
}

/* a stage's descriptor set: constant buffers, storage buffers, textures. *zt_ro is set when it samples the depth
   target, which the pass then binds read-only. *fb gathers the color slots it samples in place, which the pass then
   binds in the feedback-loop layout (feedback_order): the fragment stage's, NULL for the others, which sample a copy.
   res: textures fs_key resolved for this stage, or NULL */
/* A stage's descriptor set by its contents: the same writes (buffers, offsets, ranges, views, samplers, layouts) in
   the same layout reuse the set made for them earlier in the frame, which still holds them (its pool is reset when the
   frame slot comes round again, and what it names is destroyed no earlier: vk_defer_*). One game allocated and wrote
   13,400 sets a frame, two a draw, much of it the same textures again. Two independent 64-bit hashes stand for the
   contents */
u64 g_vk_dset_new, g_vk_dset_reused;
static u64 dset_mix(u64 h, u64 v, u64 m) {
    h = (h ^ v) * m;
    return h ^ (h >> 29);
}
static VkDescriptorSet desc_set_cached(VkDescriptorSetLayout layout, DescWrites* d) {
    static struct {
        u64 h1, h2, frame;
        VkDescriptorSet set;
    } cache[1u << 14];
    const u64 m1 = 0x9E3779B97F4A7C15ull, m2 = 0xC2B2AE3D27D4EB4Full;
    u64 h1 = dset_mix(0x243F6A8885A308D3ull, (u64)(uintptr_t)layout, m1),
        h2 = dset_mix(0x13198A2E03707344ull, (u64)(uintptr_t)layout, m2);
    for (u32 i = 0; i < d->n; i++) {
        const VkWriteDescriptorSet* w = &d->w[i];
        u64 head = (u64)w->dstBinding | (u64)w->descriptorType << 20 | (u64)w->descriptorCount << 40 |
                   (u64)w->dstArrayElement << 56;
        h1 = dset_mix(h1, head, m1);
        h2 = dset_mix(h2, head, m2);
        for (u32 j = 0; j < w->descriptorCount; j++) {
            u64 a, b, c;
            if (w->pBufferInfo) {
                a = (u64)(uintptr_t)w->pBufferInfo[j].buffer, b = w->pBufferInfo[j].offset, c = w->pBufferInfo[j].range;
            } else if (w->pImageInfo) {
                a = (u64)(uintptr_t)w->pImageInfo[j].imageView, b = (u64)(uintptr_t)w->pImageInfo[j].sampler;
                c = (u64)w->pImageInfo[j].imageLayout;
            } else {
                a = (u64)(uintptr_t)(w->pTexelBufferView ? w->pTexelBufferView[j] : VK_NULL_HANDLE), b = c = 0;
            }
            h1 = dset_mix(dset_mix(dset_mix(h1, a, m1), b, m1), c, m1);
            h2 = dset_mix(dset_mix(dset_mix(h2, a, m2), b, m2), c, m2);
        }
    }
    u64 frame = vk_frame_id() + 1; /* 0 for an unused slot */
    typeof(cache[0])* e = &cache[h1 >> 50];
    if (e->frame == frame && e->h1 == h1 && e->h2 == h2) {
        g_vk_dset_reused++;
        return e->set;
    }
    VkDescriptorSet set = vk_rec_new_set(layout, d->n, d->w); /* made on the recording thread, before it is bound */
    if (!set) return VK_NULL_HANDLE;
    g_vk_dset_new++;
    *e = (typeof(cache[0])){h1, h2, frame, set};
    return set;
}
/* Opt-in cosmetic workaround, not a proven emulation correction.
   Match the captured scene's two category1 color entries exactly. */
static int rim_candidate_enabled(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SNX_ARCEUS_NEUTRAL_RIM"); on = e && !strcmp(e,"1"); }
    return on;
}
static int rim_candidate_match(const void* data, u32 size, int stage, int bank, u64 hash) {
    static const u32 purple[4] = {0x3ee9aeb6,0x3e405416,0x3f3ce7b4,0x3ef0f0f1};
    static const u32 warm[4] = {0x3f800000,0x3f4a9282,0x3f54ad57,0x3f169697};
    if (!data || size < 0x160 || size > 65536 || stage != 4 || bank != 3 ||
        (hash != 0x89619027ba43e5d3ull && hash != 0xe02c1a109a77fac4ull)) return 0;
    return !memcmp((const u8*)data+0x150,purple,sizeof purple) &&
           !memcmp((const u8*)data+0xd0,warm,sizeof warm);
}
static void rim_candidate_apply(void* data) {
    float rgb[3]; memcpy(rgb,(u8*)data+0x150,sizeof rgb);
    float y = .2126f*rgb[0] + .7152f*rgb[1] + .0722f*rgb[2];
    rgb[0]=rgb[1]=rgb[2]=y;
    memcpy((u8*)data+0x150,rgb,sizeof rgb);
}
static void rim_candidate_buffer(GpuChannel* ch, int stage, int bank, VkSh* shader,
                                  u32 size, VkBuffer* buffer, u64* offset) {
    if (!rim_candidate_enabled() || stage!=4 || bank!=3 ||
        (shader->hash!=0x89619027ba43e5d3ull && shader->hash!=0xe02c1a109a77fac4ull)) return;
    u64 address=ch->cbind[stage][bank]; VkBuffer pending; u64 pending_offset;
    if (!address || vk_buf_pending(address,size) || vk_compute_gpu_src(address,size,&pending,&pending_offset)) {
        LOG_LIMIT(4,"arceus-neutral-rim: skipped non-CPU constant buffer\n"); return;
    }
    const void* src=gmem_ptr(address,size);
    if (!rim_candidate_match(src,size,stage,bank,shader->hash)) return;
    VkBuffer b; u64 o;
    void* dst=vk_stage(size,vk_stage_align(),&b,&o);
    if (!dst) { LOG_LIMIT(4,"arceus-neutral-rim: allocation failed; original lighting retained\n"); return; }
    memcpy(dst,src,size); rim_candidate_apply(dst);
    *buffer=b; *offset=o;
    LOG_LIMIT(8,"arceus-neutral-rim: applied category1 purple tint workaround fs=%llx; alpha and luminance preserved\n",(unsigned long long)shader->hash);
}

static VkImageView up_override;static u64 up_address;
static VkDescriptorSet stage_set_(GpuChannel* ch, int stage, VkSh* s, Surf** rt, Surf* zt, u32 n, int* zt_ro,
                                  u32* fb, u64* scaled, TexRes* res, const VkDrawStorage* draw, DrawImages* images, float raster_scale);
/* scaled: a bit for each texture slot bound to a scaled render target (scaled_coords in spirv_translate.c) */
static VkDescriptorSet stage_set(GpuChannel* ch, int stage, VkSh* s, Surf** rt, Surf* zt, u32 n, int* zt_ro, u32* fb,
                                 u64* scaled, TexRes* res, const VkDrawStorage* draw, DrawImages* images, float raster_scale) {
    SNX_ZONE(z, "stage set");
    VkDescriptorSet set = stage_set_(ch, stage, s, rt, zt, n, zt_ro, fb, scaled, res, draw, images, raster_scale);
    SNX_ZONE_END(z);
    return set;
}
static VkDescriptorSet stage_set_(GpuChannel* ch, int stage, VkSh* s, Surf** rt, Surf* zt, u32 n, int* zt_ro,
                                  u32* fb, u64* scaled, TexRes* res, const VkDrawStorage* draw, DrawImages* images, float raster_scale) {
    u32* r = ch->r3d;
    VkDescriptorSetLayout layout = g_vk_dslayout[s->indexed][shader_vk_set(stage)];
    VkDescriptorSet set = VK_NULL_HANDLE; /* the writes take it once their contents are known (desc_set_cached) */
    DescWrites d;
    d.n = 0;
    for (int i = 0; i < 18; i++) {
        if (!(s->si.cbuf_mask & (1u << i))) continue;
        u32 sz = cb_range(ch, stage, s, i);
        VkBuffer b;
        u64 o;
        if (!cb_get(ch, stage, i, sz, &b, &o)) return VK_NULL_HANDLE;
        rim_candidate_buffer(ch,stage,i,s,sz,&b,&o);
        dw_buffer(&d, set, SNX_VKB_CB + i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, b, o, sz);
    }
    for (int i = 0; i < s->si.ngbuf && i < 16; i++) {
        const VkDrawGbuf* gb = &draw->b[stage ? 1 : 0][i];
        u64 addr = gb->addr;
        u32 size = gb->size;
        VkBuffer b;
        u64 o;
        int ok = gb->aliased            ? (b = gb->buf, o = gb->off, 1)
                 : addr && gb->writable ? vk_buf_copy_writable(addr, size, &b, &o)
                 : addr && gb->bare     ? vk_buf_get_window(addr, size, &b, &o)
                 : addr                 ? vk_buf_get(addr, size, &b, &o)
                                        : vk_buf_copy(0, size, &b, &o);
        if (!ok) return VK_NULL_HANDLE;
        dw_buffer(&d, set, SNX_VKB_SSBO + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, b, o, gb->aliased ? gb->range : size);
    }
    if (s->si.ngbuf)
        dw_buffer(&d, set, SNX_VKB_SMAP, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, draw->map_buf[stage ? 1 : 0],
                  draw->map_off[stage ? 1 : 0], draw->map_size[stage ? 1 : 0]);
    VkBuffer cb;
    u64 co;
    if (stage == 4 && frame_consts(&cb, &co, raster_scale))
        dw_buffer(&d, set, SNX_VKB_RS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cb, co, 16);
    u32 tcb = r[0x982];
    int nt = s->si.ntex > SH_MAXTEX ? SH_MAXTEX : s->si.ntex;
    int table_first[SH_INDEXED_TABLES] = {0}, table_count = 0;
    for (int i = 0; i < nt; i++) {
        ShTex* st = &s->si.tex[i];
        if (st->flags & SNX_TEX_INDEXED) {
            int table = 0;
            while (table < table_count && !shader_same_indexed_table(st, &s->si.tex[table_first[table]])) table++;
            if (table == table_count) {
                if (table_count == SH_INDEXED_TABLES) return VK_NULL_HANDLE;
                table_first[table_count++] = i;
            }
        }
    }
    if (table_count &&
        !tex_tables_set(ch, stage, s, table_first, table_count, rt, zt, n,
                        s->si.nimg ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &d, set))
        return VK_NULL_HANDLE;
    for (int i = 0; i < nt; i++) {
        ShTex* st = &s->si.tex[i];
        if (st->flags & SNX_TEX_INDEXED) continue;
        ShTex plain;
        if (st->shadow && (s->si.tex_dc_color_mask >> i & 1)) { /* compared in the shader: a plain sampler and view */
            plain = *st;
            plain.shadow = 0;
            st = &plain;
        }
        u32 handle = tex_handle(ch, stage, tcb, st);
        /* what fs_key resolved, unless it is a render target or a copy of one: a crop of the same target at
           another size, for another stage, replaces it. Other textures stay until the next present */
        TexRes own, *x = res ? &res[i] : NULL;
        if (!x || !x->have || x->handle != handle || (x->t && x->t->is_rt)) {
            x = &own;
            tex_resolve(ch, handle, x);
        }
        Surf* t = x->t;
        if (st->flags & 2) return VK_NULL_HANDLE;
        if (st->flags & 1) t = vk_tex_from_tic_raw(x->tic);
        /* a raw load's header without 4-byte texels, or no header at all, reads zeros through the null view, as a
           missing texture does: the draw still runs (it was dropped, from one game's movie and gameplay passes) */
        if ((st->flags & 1) && !t)
            LOG_LIMIT(1, "vulkan: a raw surface load's header %08x %08x %08x has no 32-bit view: it reads zeros\n",
                      x->tic[0], x->tic[2], x->tic[4]);
        VkImageViewType want = vk_tex_view_type(st);
        int num = st->flags & 1                                    ? 1
                  : st->shadow || !((s->si.tex_int_mask >> i) & 1) ? 0
                  : ((s->si.tex_sint_mask >> i) & 1)               ? 2
                                                                   : 1;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = s->si.nimg ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        /* sampling the depth target needs it read-only: no depth writes, and no stencil writes on either face */
        int sten_w = zt && zt->f.stencil && r[0x4E0] && ((r[0x4E7] & 0xFF) || (r[0x565] && (r[0x3D6] & 0xFF)));
        int depth_ro = t && t == zt && !r[0x4BA] && !sten_w;
        u32 slots = 0; /* the color slots t is bound to, when the fragment shader may sample it in place */
        if (t && fb && g_vk.has_feedback && t->layout != VK_IMAGE_LAYOUT_UNDEFINED)
            for (u32 j = 0; j < n; j++)
                if (rt[j] == t) slots |= 1u << j;
        if (slots) {
            /* the guest reads the target's previous contents during a draw that writes it: in place, in a feedback
               loop */
            view = vk_tex_bind_view(t, want, x->tic, x->tsc, num);
            if (view) *fb |= slots;
        } else if (t && is_target(t, rt, zt, n) && !depth_ro) {
            /* The guest reads the target's previous contents during a draw that writes it. Without a feedback loop
               Vulkan can't bind one image for both uses here, so copy it before the draw starts and sample that
               copy. */
            Surf* snap = vk_rt_snapshot(t);
            if (snap != t) {
                t = snap;
                view = vk_tex_bind_view(t, want, x->tic, x->tsc, num);
                g_vk_rt_copies++;
            } else
                LOG_LIMIT(1, "vulkan: a draw samples a target it renders to (read as empty)\n");
        } else
            view = vk_tex_bind_view(t, want, x->tic, x->tsc, num);
        const Surf* sampled = view ? t : NULL; /* the texture whose own view is bound, for the sampler */
        if (scaled && sampled && (sampled->sw != sampled->w || sampled->sh != sampled->h))
            *scaled |= 1ull << i;
        if (view && depth_ro) {
            *zt_ro = 1; /* render_begin moves it to the read-only layout */
            layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } else if (view && slots)
            layout = VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT; /* render_begin moves it there */
        else if (view)
            vk_surf_use(vk_cmd(), t, layout, VK_SAMPLED_STAGES, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        else {
            view = vk_null_view(want, st->shadow ? 3 : num);
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        if (!view) return VK_NULL_HANDLE;
        VkSampler smp = vk_tex_bind_sampler(x->tsc, x->tic, st, num, sampled, want);
        if (!smp) return VK_NULL_HANDLE;
        if(stage==4 && up_override && t && t->gva==up_address && want==VK_IMAGE_VIEW_TYPE_2D){view=up_override;LOG_LIMIT(12,"arceus-upscaler: bound reconstructed scene to composite slot %d\n",i);layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;if(scaled)*scaled|=1ull<<i;}
        u32 b = SNX_VKB_TEX + i;
        d.ii[b] = (VkDescriptorImageInfo){smp, view, layout};
        d.w[d.n++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                            .dstSet = set,
                                            .dstBinding = b,
                                            .descriptorCount = 1,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            .pImageInfo = &d.ii[b]};
    }
    for (int i = 0; i < s->si.nimg; i++) {
        const u32 bank = s->si.img[i].bindless ? s->si.img[i].bl_cbuf : tcb;
        u64 cba = bank < 18 ? ch->cbind[stage][bank] : 0;
        if (!cba || !images) return VK_NULL_HANDLE;
        u32 handle = gmem_r32(cba + (u64)s->si.img[i].cbuf_off * 4);
        u64 tic_base = ((u64)r[0x55D] << 32) | r[0x55E];
        u32 tic[8];
        vk_gmem_words(tic_base + (handle & 0xFFFFF) * 32ull, tic, 8);
        Surf* surf = vk_tex_from_tic_storage(tic);
        u64 size = vk_surf_guest_size(surf);
        int kind = s->si.img[i].kind;
        /* a draw can't also write one of its own attachments through a storage image */
        VkImageView view =
            surf && is_target(surf, rt, zt, n) ? VK_NULL_HANDLE : vk_storage_image_view(surf, &s->si, i, &size);
        if (!view) {
            LOG_LIMIT(4,
                      "vulkan: unsupported storage image (draw skipped): %s handle %08x image %p format %u "
                      "raw %d integer %d storage %d levels %u depth %u size %llu target %d view type %d block "
                      "linear %d %ux%u at %ux%u, the program's surface type %d\n",
                      kind == 1 ? "atomic" : "store", handle, (void*)surf, surf ? surf->f.vk : 0,
                      surf ? surf->f.raw : 0, surf ? surf->f.is_int : 0, surf ? surf->storage_capable : 0,
                      surf ? surf->levels : 0, surf ? surf->depth3d : 0, (unsigned long long)size,
                      surf ? is_target(surf, rt, zt, n) : 0, surf ? (int)surf->type : -1, surf ? (int)surf->bl : -1,
                      surf ? surf->w : 0, surf ? surf->h : 0, surf ? surf->sw : 0, surf ? surf->sh : 0,
                      s->si.img[i].type);
            return VK_NULL_HANDLE;
        }
        int seen = 0;
        for (int j = 0; j < images->n; j++)
            if (images->surf[j] == surf) seen = 1;
        /* an earlier draw's writes to it wait for their copy (image_pending): that copy, recorded later, takes this
           draw's writes too, over the same bytes from before them. Reading guest memory for this draw's own would land
           those writes first, waiting for the GPU (once a frame in Pokemon Legends Z-A). A landing during the setup
           submits, and the setup starts over */
        if (!seen && !image_pending(surf)) {
            u8* original = malloc((size_t)size);
            if (!original) return VK_NULL_HANDLE;
            gmem_read(surf->gva, original, size);
            images->original[images->n] = original;
        } else
            images->original[images->n] = NULL;
        images->surf[images->n] = surf;
        images->size[images->n] = (u32)size;
        images->n++;
        vk_surf_use(vk_cmd(), surf, VK_IMAGE_LAYOUT_GENERAL,
                    stage ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                    VK_STORAGE_IMAGE_ACCESS);
        u32 binding = SNX_VKB_IMG + (u32)i;
        d.ii[binding] = (VkDescriptorImageInfo){VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
        d.w[d.n++] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                            .dstSet = set,
                                            .dstBinding = binding,
                                            .descriptorCount = 1,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                            .pImageInfo = &d.ii[binding]};
    }
    return desc_set_cached(layout, &d);
}

/* ------------------------------------------------------------ draws */
/* The dynamic state, pipeline and descriptor sets the previous draw recorded in the command buffer g_dyn.cb names
   (vk_cb_begun): a draw records only what differs, where every draw set about 30 states again (one game: 6,700 draws a
   frame). Every draw pipeline has these states dynamic, so binding another one keeps them. The clear path and the
   on-screen controls set or bind otherwise and forget them (vk_render_forget_state) */
/* Maxwell's viewports, each with its own transform, clip and scissor (NVIDIA open-gpu-doc classes/3d/clb197.h):
   SET_VIEWPORT_SCALE_X(j) at method 0x0A00 + 32 j (register 0x280 + 8 j) for j < 16, then
   SET_VIEWPORT_CLIP_HORIZONTAL(j) at 0x0C00 + 16 j (0x300 + 4 j) and SET_SCISSOR_ENABLE(j) at 0x0E00 + 16 j
   (0x380 + 4 j). A program that writes the viewport index selects among them */
#define GUEST_VIEWPORTS 16
static struct {
    u32 cb;
    VkPipeline pipe;
    VkPipelineLayout layout; /* the one sets were bound through: another may disturb them */
    VkDescriptorSet sets[5];
    u32 fb, cull, front, topo, restart, discard, bias_on, dt, dw, dcmp, dbounds, st;
    VkViewport vp[GUEST_VIEWPORTS];
    VkRect2D sc[GUEST_VIEWPORTS];
    u32 viewports;
    float lw, bias[3], bc[4];
    u32 sfront[7], sback[7];
} g_dyn;
#define DYN(field, value, call)                                                                                        \
    do {                                                                                                               \
        if (fresh || g_dyn.field != (value)) {                                                                         \
            g_dyn.field = (value);                                                                                     \
            call;                                                                                                      \
        }                                                                                                              \
    } while (0)
#define DYNV(field, value, call)                                                                                       \
    do {                                                                                                               \
        if (fresh || memcmp(&g_dyn.field, &(value), sizeof g_dyn.field)) {                                             \
            memcpy(&g_dyn.field, &(value), sizeof g_dyn.field);                                                        \
            call;                                                                                                      \
        }                                                                                                              \
    } while (0)
void vk_render_forget_state(void) { g_dyn.cb = 0; }
u64 g_vk_drawn, g_vk_skipped;

/* NVB197_SET_VIEWPORT_COORDINATE_SWIZZLE (0x0A18) can negate Y before the scale and offset. A negative Vulkan
   height implements the same mapping without changing texture coordinates or guest render-target contents.
   Reference: NVIDIA open-gpu-doc/classes/3d/clb197.h. */
static VkViewport render_viewport_scaled(const u32* r, u32 index, float raster_scale) {
    if (index >= GUEST_VIEWPORTS) fatal("vulkan: viewport %u of the %u the GPU has", index, GUEST_VIEWPORTS);
    float vt[6];
    memcpy(vt, &r[0x280 + index * 8], sizeof vt);
    float sx = vt[0], sy = vt[1], tx = vt[3], ty = vt[4], ax = sx < 0 ? -sx : sx, ay = sy < 0 ? -sy : sy;
    VkViewport vp;
    int small = 2 * ax < 1 || 2 * ay < 1;
    if (small != !(r[0x64B] & 1))
        LOG_LIMIT(1, "vulkan: viewport scale %g x %g with transform enable %x\n", sx, sy, r[0x64B]);
    if (small) {
        u32 vx = r[0x300 + index * 4], vy = r[0x301 + index * 4];
        vp = (VkViewport){((float)(vx & 0xFFFF) * raster_scale), ((float)(vy & 0xFFFF) * raster_scale), ((float)(vx >> 16) * raster_scale), ((float)(vy >> 16) * raster_scale), 0, 1};
    } else
        vp = (VkViewport){((float)(tx - ax) * raster_scale), ((float)(ty - sy) * raster_scale), ((float)(2 * ax) * raster_scale), ((float)(2 * sy) * raster_scale), 0, 1};
    /* A lower-left window measures y from the surface clip's bottom. Match the 3D blit path's conversion. */
    if (r[0x4EB] & 1) {
        vp.y += ((float)((float)(r[0x3FE] >> 16)) * raster_scale);
        vp.height = -vp.height;
    }
    if (((r[0x286 + index * 8] >> 4) & 7) == 3) {
        vp.y += vp.height;
        vp.height = -vp.height;
    }
    float zn, zf;
    memcpy(&zn, &r[0x302 + index * 4], 4);
    memcpy(&zf, &r[0x303 + index * 4], 4);
    if (vt[2] != 0.0f || vt[5] != 0.0f) {
        zn = vt[5] - ((r[0x35F] & 1) ? 0.0f : vt[2]);
        zf = vt[5] + vt[2];
    }
    vp.minDepth = zn < 0 ? 0 : zn > 1 ? 1 : zn;
    vp.maxDepth = zf < 0 ? 0 : zf > 1 ? 1 : zf;
    return vp;
}
VkViewport vk_render_viewport_at(const u32* r, u32 index) {
    return render_viewport_scaled(r, index, g_vk_scale);
}
VkViewport vk_render_viewport(const u32* r) { return vk_render_viewport_at(r, 0); }
/* the vertices of the geometry program's input primitive: the tessellator's output primitive (0xC8: the domain in
   bits 0-1, isolines 0, and the output in bits 8-9, points 0), or the draw's topology */
static u32 gs_input_vertices(u32 topology, int tes_on, u32 params) {
    if (tes_on) return ((params >> 8) & 3) == 0 ? 1 : (params & 3) == 0 ? 2 : 3;
    return topology == 0                      ? 1
           : topology <= 3                    ? 2
           : topology == 10 || topology == 11 ? 4
           : topology == 12 || topology == 13 ? 6
                                              : 3;
}

static VkFrontFace front_face_at_viewport(const u32* r, float viewport_height) {
    int cw = r[0x647] == 0x900;
    /* A negative Vulkan viewport already reverses framebuffer winding. WINDOW_ORIGIN_FLIP_Y also reverses guest
       facing, so only compensate for that bit when the viewport has not performed the same reversal. */
    if ((r[0x4EB] & 0x10) && viewport_height > 0) cw = !cw;
    return cw ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}
int vk_render_depth_clamp(const u32* r) {
    /* GeometryClip: Passthrough, FrustumXYZ and FrustumZ clip depth; the remaining modes clamp it. */
    u32 mode = (r[0x64F] >> 11) & 7;
    return mode != 1 && mode != 3 && mode != 5;
}

VkFrontFace vk_render_front_face(const u32* r) { return front_face_at_viewport(r, vk_render_viewport(r).height); }

/* the Vulkan topology of a draw's (the 3D engine's numbers, 14 a patch list) */
static VkPrimitiveTopology topology_vk(u32 topology) {
    static const VkPrimitiveTopology adjacent[5] = {
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST};
    if (topology >= 10 && topology <= 14) return adjacent[topology - 10];
    switch (topology) {
        case 0: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case 1: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case 2: /* line loop: drawn open */
        case 3: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case 5:
        case 8: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; /* quad strips are triangle strips */
        case 6:
        case 9: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;   /* polygons are fans */
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; /* triangles, and quads made into them */
    }
}
/* ------------------------------------------------------------ a draw's shaders and pipeline, kept between draws */
/* the register groups a draw's shaders and pipeline key are made from */
#define DRAW_GROUPS (R3D_SHADER | R3D_VATTR | R3D_VFORMAT | R3D_RTFORMAT | R3D_BLEND | R3D_PIPEMISC | R3D_XFB)
/* the programs a draw's registers select */
typedef struct {
    VkSh *vs, *tcs, *tes, *gs; /* the stages between NULL when off */
    VkSh* fs0;                 /* the fragment program as it is, NULL when off: its textures pick the variant drawn */
    u32 attr_int, attr_sint;   /* the integer vertex attributes, of all 32, which key the vertex program */
    u32 rt_int, rt_sint;       /* the integer color targets the registers name (rt_int_targets) */
    u32 topology;              /* the draw's, every primitive a patch with tessellation */
    int xfb_on;                /* transform feedback captures */
} DrawShaders;
/* the vertex bindings a pipeline key describes, for finding their buffers each draw: binding b reads guest stream
   key[b] & 31 from its base offset key[b] >> 5 on, with pad bytes past its last stride when still inside the stream's
   limit, or the constants of the attributes without data when key[b] is ~0u. When the guest's stride isn't a multiple
   of its attributes' components (guest_stride[b], else 0), the binding reads a copy with the vertices stride[b] apart,
   rounded up to one (copy_restrided) */
typedef struct {
    u32 key[33], pad[33], guest_stride[33];
    VkDeviceSize stride[33];
    u32 used;  /* the streams whose limits the bindings were planned with */
    u32 empty; /* attributes whose relative offsets are outside their guest bindings */
} VertexPlan;
/* what a pipeline key takes from the surfaces drawn to: the color slots up to the last bound one, those drawn to,
   which of them are integer, their formats, and the depth target's */
typedef struct {
    u32 n, present, is_int, stencil;
    u32 color[8], depth;
} TargetFormats;
/* What the last draw made from the registers, kept for the next draw while nothing it was made from changed: the
   register groups (pend: those a method changed since, all of them when another channel draws), the draw's topology,
   the shaders' code (trusted within an epoch and shader generation, as vk_shader_get trusts it) and the constant
   buffer words their translation read (checked each draw, as vk_shader_get checks them on a hit). What else goes into it is found each
   draw and compared: the fragment variant's key (its textures' formats), the targets' formats, which vertex streams
   are empty, and whether the draw samples a target it renders to. Draws made all of it again, a 1 KiB key built and
   compared included, for each of the ~3,400 draws a frame in one game, most of them with the state of the draw
   before. SNX_DIRTY_CHECK makes it again for each draw that keeps it, and warns of any difference: a register missing
   from a group, or an input that isn't compared */
static struct {
    u32 ch_id, pend;
    u32 topology; /* the draw's argument, before tessellation makes it a patch list */
    u32 epoch;    /* g_vk_epoch */
	u64 shader_generation; /* instruction invalidations also expire kept draws */
    DrawShaders sh; /* the rest holds when sh.vs is set */
    FsKey fkey;
    VkSh* fs;
    VertexPlan plan;
    TargetFormats tf;
    PipeKey k; /* k and pipe hold together whenever pipe is set */
    VkPipeline pipe;
    /* the vertex registers the state was made from, for a change of R3D_VATTR or R3D_VFORMAT that leaves them as they
       were: attributes and streams the vertex program doesn't read. The attributes changed in a third of one game's
       draws, and comparing what was read kept 66% of its draws where the groups alone kept 60% */
    u32 vattr[32];      /* the attributes the vertex program reads, 0 for the others */
    u32 vstreams;       /* the streams those attributes name */
    u32 vstream[32][3]; /* each one's stride and enable word, divisor, and whether it is per instance */
    int check;          /* SNX_DIRTY_CHECK, -1 until read */
} g_draw = {.pend = ~0u, .check = -1};
u64 g_vk_draw_kept, g_vk_draw_checked;

static void draw_differs(const char* what) {
    WARN_LIMIT(32, "vulkan: SNX_DIRTY_CHECK: a draw that kept the last draw's state has another %s when it is made "
                   "again (%llu draws checked)\n", what, (unsigned long long)g_vk_draw_checked);
}
/* the integer vertex attributes (int, signed sint) of all 32, which key the vertex program */
static void attr_ints(const u32* r, u32* ai, u32* as) {
    *ai = *as = 0;
    for (int i = 0; i < 32; i++) {
        u32 ty = (r[0x458 + i] >> 27) & 7;
        if (ty == 3 || ty == 4) {
            *ai |= 1u << i;
            if (ty == 3) *as |= 1u << i;
        }
    }
}
static void stream_regs(const u32* r, u32 s, u32* w) {
    w[0] = r[0x700 + s * 4];
    w[1] = r[0x703 + s * 4];
    w[2] = r[0x620 + s];
}
/* keeps the vertex registers of the state made, for vertex_regs_hold */
static void vertex_regs_keep(const u32* r, u32 in) {
    g_draw.vstreams = 0;
    for (u32 i = 0; i < 32; i++) {
        g_draw.vattr[i] = in >> i & 1 ? r[0x458 + i] : 0;
        if (in >> i & 1) g_draw.vstreams |= 1u << (r[0x458 + i] & 31);
    }
    for (u32 m = g_draw.vstreams; m; m &= m - 1) stream_regs(r, (u32)__builtin_ctz(m), g_draw.vstream[__builtin_ctz(m)]);
}
/* whether what the kept state read of the vertex registers is as it was: all that the shaders, the key and the
   vertex bindings take from them */
static int vertex_regs_hold(const u32* r, const DrawShaders* s) {
    u32 in = s->vs->si.in_mask, ai, as, w[3];
    attr_ints(r, &ai, &as);
    if (ai != s->attr_int || as != s->attr_sint) return 0;
    for (u32 m = in; m; m &= m - 1)
        if (r[0x458 + __builtin_ctz(m)] != g_draw.vattr[__builtin_ctz(m)]) return 0;
    for (u32 m = g_draw.vstreams; m; m &= m - 1) {
        stream_regs(r, (u32)__builtin_ctz(m), w);
        if (memcmp(w, g_draw.vstream[__builtin_ctz(m)], sizeof w)) return 0;
    }
    return 1;
}
/* whether the state kept in g_draw holds for this draw of ch, as far as what is checked before drawing tells */
static int draw_kept(const GpuChannel* ch, u32 topology, u32 epoch) {
    const DrawShaders* s = &g_draw.sh;
    u32 pend = g_draw.pend & DRAW_GROUPS;
	if (g_draw.shader_generation != g_vk.shader_generation)
		return 0;
    if (!s->vs || (pend & ~(R3D_VATTR | R3D_VFORMAT)) || g_draw.topology != topology || g_draw.epoch != epoch)
        return 0;
    if (pend) { /* the vertex registers changed: those the state read may not have */
        if (!vertex_regs_hold(ch->r3d, s)) return 0;
        g_draw.pend &= ~pend;
    }
    if (s->xfb_on != (g_vk.has_xfb && ch->r3d[0x1D1] && xfb_counters())) return 0; /* the counters were made since */
    const VkSh* used[6] = {s->vs, s->tcs, s->tes, s->gs, s->fs0, g_draw.fs};
    for (int i = 0; i < 6; i++)
        if (used[i] && used[i]->cbr.n && !vk_shader_cb_hold(ch, used[i])) return 0;
    return 1;
}
static int draw_shaders_same(const DrawShaders* a, const DrawShaders* b) {
    return a->vs == b->vs && a->tcs == b->tcs && a->tes == b->tes && a->gs == b->gs && a->fs0 == b->fs0 &&
           a->attr_int == b->attr_int && a->attr_sint == b->attr_sint && a->rt_int == b->rt_int && a->rt_sint == b->rt_sint && a->topology == b->topology && a->xfb_on == b->xfb_on;
}
/* the shaders of ch's registers for a draw of topology: 0 when the draw is skipped */
static int draw_shaders(GpuChannel* ch, u32 topology, DrawShaders* o) {
    const u32* r = ch->r3d;
    /* shaders: VertexB always runs (after VertexA when that is on), the fragment stage when enabled */
    u64 code = ((u64)r[0x582] << 32) | r[0x583];
    u32 attr_int, attr_sint, clip;
    int tcs_on, tes_on, gs_on;
    attr_ints(r, &attr_int, &attr_sint);
    /* the stages between: tessellation control and evaluation (program slots 2 and 3), geometry (slot 4) */
    tcs_on = r[0x820] & 1;
    tes_on = r[0x830] & 1;
    gs_on = r[0x840] & 1;
    if ((tcs_on || tes_on || gs_on) && g_vk_stage_sets < 5) {
        LOG_LIMIT(1, "vulkan: this GPU binds too few descriptor sets for tessellation and geometry (draw skipped)\n");
        return 0;
    }
    if (tcs_on != tes_on) {
        /* the hardware tessellates with the levels of registers 0xC9-0xCE without a control program, which
           Vulkan can't: no game is known to */
        LOG_LIMIT(1, "vulkan: tessellation with only one of its two programs (draw skipped)\n");
        return 0;
    }
    if ((topology == 14 && !tes_on) || (topology >= 10 && topology <= 13 && !gs_on))
        return 0; /* patches and adjacency need the stages above */
    if (tes_on) topology = 14; /* every primitive is a patch with tessellation, as Vulkan requires */
    /* SetUserClipEnable (0x544): a clip distance the program writes clips only when its bit is set, in the last
       stage before rasterization */
    clip = r[0x544] & 0xFF;
    /* transform feedback (0x1D1): what the last stage before rasterization captures, from each buffer's control
       (0x1C0 + 4 * i: its stream, the words of a vertex, their stride in bytes) and the layout of those words
       (0xA00 + 0x20 * i, a byte each: an attribute word, as Eden's MakeTransformFeedbackVaryings reads them) */
    ShXfb xfb;
    int xfb_on = g_vk.has_xfb && r[0x1D1] && xfb_counters();
    if (xfb_on) {
        memset(&xfb, 0, sizeof xfb);
        for (int i = 0; i < 4; i++) {
            u32 words = r[0x1C1 + 4 * i];
            xfb.count[i] = (u8)(words > 128 ? 128 : words);
            xfb.stride[i] = (u16)r[0x1C2 + 4 * i];
            memcpy(xfb.layout[i], &r[0xA00 + 0x20 * i], sizeof xfb.layout[i]); /* word n / 4's byte n % 4 */
        }
    }
    int capture_positions=g_vk.has_xfb && !xfb_on && !tes_on && !gs_on && topology==4;
    if(capture_positions){
        memset(&xfb,0,sizeof xfb);
        xfb.count[0]=6;xfb.stride[0]=32;xfb.layout[0][4]=32;xfb.layout[0][5]=33;
        for(u32 j=0;j<4;j++)xfb.layout[0][j]=(u8)(28+j);
    }
    o->topology = topology;
    o->xfb_on = xfb_on;
    o->attr_int = attr_int;
    o->attr_sint = attr_sint;
    o->vs = vk_shader_get(ch, code + r[0x811], (r[0x800] & 1) ? code + r[0x801] : 0, ST_VS, attr_int, attr_sint, 0, 0,
                          tes_on || gs_on ? SNX_SH_NOT_LAST : clip, 0, 0, 0,
                          (xfb_on || capture_positions) && !tes_on && !gs_on ? &xfb : NULL);
    /* a vertex program's images pick its variant as a fragment program's do (fs_key) */
    u32 vs_signed_images = 0;
    u32 vs_images = o->vs && !o->vs->failed && o->vs->si.nimg ? image_variant(ch, 0, o->vs, &vs_signed_images) : 0;
    if (vs_images || vs_signed_images)
        o->vs = vk_shader_get(ch, code + r[0x811], (r[0x800] & 1) ? code + r[0x801] : 0, ST_VS, attr_int, attr_sint,
                              vs_images << 8, vs_signed_images << 8, tes_on || gs_on ? SNX_SH_NOT_LAST : clip, 0, 0, 0,
                              (xfb_on || capture_positions) && !tes_on && !gs_on ? &xfb : NULL);
    o->tcs =
        tcs_on ? vk_shader_get(ch, code + r[0x821], 0, ST_TCS, 0, 0, 0, 0, SNX_SH_NOT_LAST, 0, 0, 0, NULL) : NULL;
    o->tes = tes_on ? vk_shader_get(ch, code + r[0x831], 0, ST_TES, 0, 0, r[0xC8] & 0x333, 0,
                                    gs_on ? SNX_SH_NOT_LAST : clip, 0, 0, 0, xfb_on && !gs_on ? &xfb : NULL)
                    : NULL;
    o->gs = gs_on ? vk_shader_get(ch, code + r[0x841], 0, ST_GS, 0, 0, gs_input_vertices(topology, tes_on, r[0xC8]),
                                  0, clip, 0, 0, 0, xfb_on ? &xfb : NULL)
                  : NULL;
    if ((tcs_on && (!o->tcs || o->tcs->failed)) || (tes_on && (!o->tes || o->tes->failed)) ||
        (gs_on && (!o->gs || o->gs->failed)) || !o->vs || o->vs->failed)
        return 0;
    o->fs0 = (r[0x850] & 1) ? vk_shader_get(ch, code + r[0x851], 0, ST_FS, 0, 0, 0, 0, 0, 0, 0, 0, NULL) : NULL;
    rt_int_targets(r, &o->rt_int, &o->rt_sint);
    return 1;
}
static u64 stream_start(const u32* r, u32 s) { return ((u64)r[0x701 + s * 4] << 32) | r[0x702 + s * 4]; }
/* the bytes of vertex stream s, from its start to its limit (0x7C0 + 2 * s, inclusive): 0 when the limit is below */
static u64 stream_len(const u32* r, u32 s) {
    u64 start = stream_start(r, s), end = ((u64)r[0x7C0 + s * 2] << 32) | r[0x7C1 + s * 2];
    return end >= start ? end - start + 1 : 0;
}
/* Attributes whose relative offsets start outside their bindings. */
static u32 attributes_empty(const u32* r, u32 inputs) {
    u32 e = 0;
    while (inputs) {
        u32 i = (u32)__builtin_ctz(inputs), attribute = r[0x458 + i];
        inputs &= inputs - 1;
        if (stream_len(r, attribute & 31) <= ((attribute >> 7) & 0x3fff)) e |= 1u << i;
    }
    return e;
}
static void target_formats(Surf* const* rt, const Surf* zt, u32 n, TargetFormats* tf) {
    *tf = (TargetFormats){.n = n};
    for (u32 i = 0; i < n; i++)
        if (rt[i]) {
            tf->present |= 1u << i;
            tf->is_int |= (rt[i]->f.is_int ? 1u : 0u) << i;
            tf->color[i] = rt[i]->f.vk;
        }
    if (zt) {
        tf->depth = zt->f.vk;
        tf->stencil = zt->f.stencil ? 1 : 0;
    }
}
/* the pipeline key of the shaders (fs the fragment variant drawn), ch's registers and the targets drawn to, all but
   the feedback loop, and the vertex bindings it describes: 0 when the draw is skipped */
static int draw_key(const GpuChannel* ch, const DrawShaders* sh, const VkSh* fs, Surf* const* rt, const Surf* zt,
                    u32 n, PipeKey* k, VertexPlan* pl) {
    const u32* r = ch->r3d;
    const VkSh* vs = sh->vs;
    u32 topology = sh->topology;
    memset(k, 0, sizeof *k);
    memset(pl, 0, sizeof *pl);
    k->vs = vs->mod;
    k->fs = fs ? fs->mod : VK_NULL_HANDLE;
    k->depth_clamp = g_vk.feat.depthClamp && vk_render_depth_clamp(r);
    /* PROVOKING_VERTEX (0x5A1): 1 the last vertex, GL's and NVN's default, 0 the first, Vulkan's */
    k->provoking_last = (r[0x5A1] & 1) && g_vk.has_provoking_last;
    if ((r[0x5A1] & 1) && !g_vk.has_provoking_last)
        LOG_LIMIT(1, "vulkan: flat inputs take the first vertex of a primitive, not the last the game asks for\n");
    /* ANTI_ALIAS_ALPHA_CONTROL (0x54F) bit 0. The game uses it for grass and leaves: with one sample the driver
       dithers the coverage by alpha, which softens the edges of the alpha-cut quads */
    k->alpha_to_coverage = fs && (fs->si.sph[18]&8) ? r[0x54F] & 1 : 0;
    if (k->alpha_to_coverage)
        LOG_LIMIT(4, "vulkan: the game draws with alpha to coverage (register 0x54f = %x, msaa mode 0x574 = %x)\n",
                  r[0x54F], r[0x574]);
    /* LOGIC_OP (0x671 on, 0x672 the operation as GL numbers it from CLEAR 0x1500 to SET 0x150F, VkLogicOp's order) */
    if (r[0x671] & 1) {
        if (g_vk.feat.logicOp)
            k->logic_op = (r[0x672] & 15) + 1;
        else
            LOG_LIMIT(1, "vulkan: this GPU has no logic operations: targets are blended instead\n");
    }
    VkPrimitiveTopology topo = topology_vk(topology);
    /* adjacency and patches are their own topologies: the geometry program's input primitive is one of them */
    k->topo_class = topo >= VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY ? topo
                    : topo == VK_PRIMITIVE_TOPOLOGY_POINT_LIST             ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                    : topo <= VK_PRIMITIVE_TOPOLOGY_LINE_STRIP             ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                           : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    k->tcs = sh->tcs ? sh->tcs->mod : VK_NULL_HANDLE;
    k->tes = sh->tes ? sh->tes->mod : VK_NULL_HANDLE;
    k->gs = sh->gs ? sh->gs->mod : VK_NULL_HANDLE;
    {
        const VkSh* stages[5] = {vs, fs, sh->tcs, sh->tes, sh->gs};
        for (int i = 0; i < 5; i++)
            if (stages[i] && stages[i]->indexed) k->arrays |= 1u << shader_vk_set(stages[i]->stage);
    }
    if (sh->tes) { /* 0x373: the control points of a patch */
        k->patch = r[0x373] ? r[0x373] : 1;
        if (k->patch > g_vk.props.limits.maxTessellationPatchSize)
            k->patch = g_vk.props.limits.maxTessellationPatchSize;
    }
    k->polygon = !g_vk.feat.fillModeNonSolid ? VK_POLYGON_MODE_FILL
                 : r[0x36B] == 0x1B01        ? VK_POLYGON_MODE_LINE
                 : r[0x36B] == 0x1B00        ? VK_POLYGON_MODE_POINT
                                             : VK_POLYGON_MODE_FILL;

    /* vertex streams: each used guest stream is a binding, and so is an attribute at an offset past the device's
       limit or reaching past its stream's stride (it is read from its own offset there); one more holds the 0,0,0,1
       of attributes without data (an attribute marked constant, a stream disabled, or a format the device
       can't read) */
    const VkPhysicalDeviceLimits* lim = &g_vk.props.limits;
    u32 maxoff = lim->maxVertexInputAttributeOffset;
    u32 nloc = lim->maxVertexInputAttributes < 32 ? lim->maxVertexInputAttributes : 32;
    u32 maxbind = lim->maxVertexInputBindings < 33 ? lim->maxVertexInputBindings : 33;
    if (nloc < 32 && (vs->si.in_mask >> nloc)) {
        LOG_LIMIT(1, "vulkan: a vertex shader reads attributes past the %u this GPU has (draw skipped)\n", nloc);
        return 0;
    }
    int cbind = -1, zbind = -1, over = 0;
    u32 align[33] = {0}; /* each binding's largest attribute component */
    pl->empty = attributes_empty(r, vs->si.in_mask);
    for (u32 i = 0; i < nloc && !over; i++) {
        if (!(vs->si.in_mask & (1u << i))) continue;
        u32 a = r[0x458 + i];
        u32 sbuf = a & 31, off = (a >> 7) & 0x3FFF, size = (a >> 21) & 0x3F, ty = (a >> 27) & 7;
        const u32* va = &r[0x700 + sbuf * 4];
        VkFormat fmt = attr_format(size, ty, (a >> 31) & 1);
        int is_int = ty == 3 || ty == 4, data = !((a >> 6) & 1) && (va[0] & 0x1000) && vertex_format_ok(fmt);
        if (data) {
            pl->used |= 1u << sbuf;
        }
        if (data && (pl->empty & (1u << i))) {
            if (zbind < 0) {
                if (k->nbind == maxbind) {
                    over = 1;
                    continue;
                }
                pl->key[k->nbind] = ~1u;
                zbind = (int)k->nbind++;
            }
            k->attr[k->nattr].loc = i;
            k->attr[k->nattr].binding = (u32)zbind;
            k->attr[k->nattr].format = fmt;
            k->attr[k->nattr++].offset = 0;
            continue;
        }
        if (!data) {
            if (cbind < 0) {
                if (k->nbind == maxbind) {
                    over = 1;
                    continue;
                }
                pl->key[k->nbind] = ~0u;
                cbind = (int)k->nbind;
                k->nbind++;
            }
            k->attr[k->nattr].loc = i;
            k->attr[k->nattr].binding = (u32)cbind;
            k->attr[k->nattr].format = is_int ? (ty == 3 ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT)
                                              : VK_FORMAT_R32G32B32A32_SFLOAT;
            k->attr[k->nattr].offset = is_int ? 16 : 0;
            k->nattr++;
            continue;
        }
        u32 stride = va[0] & 0xFFF, asz = attr_bytes(size);
        /* an attribute may be wider than its stream's stride (a game may read 8-byte attributes from
           6-byte strides, its world geometry): Maxwell reads the rest from the next vertex, and so does Vulkan,
           which allows it outside the portability subset (vertexAttributeAccessBeyondStride) */
        if (stride > lim->maxVertexInputBindingStride) {
            LOG_LIMIT(1, "vulkan: a vertex stream with stride %u can't be read (draw skipped)\n", stride);
            return 0;
        }
        /* an attribute past the stride, or past the offsets the GPU allows, gets a binding of its own starting at it */
        u32 base = off > maxoff || (stride && off + asz > stride) ? off : 0;
        u32 key = sbuf | base << 5, b = 0;
        while (b < k->nbind && pl->key[b] != key) b++;
        if (b == k->nbind) {
            if (b == maxbind) {
                over = 1;
                continue;
            }
            /* the stream's limit covers its last vertex's stride: an attribute reaching past the stride reads past it */
            pl->pad[b] = stride && base + asz > stride ? base + asz - stride : 0;
            pl->stride[b] = stride;
            pl->key[b] = key;
            u32 div = r[0x620 + sbuf] ? (va[3] ? va[3] : 1) : 0;
            if (div > 1 && !g_vk.has_divisor) div = 1;
            k->bind[b].divisor = div;
            k->bind[b].stride = g_vk.has_dynamic_stride ? 0 : stride;
            k->nbind++;
        }
        k->attr[k->nattr].loc = i;
        k->attr[k->nattr].binding = b;
        k->attr[k->nattr].format = fmt;
        k->attr[k->nattr].offset = off - base;
        k->nattr++;
        if (attr_component_bytes(size) > align[b]) align[b] = attr_component_bytes(size);
    }
    if (over) {
        LOG_LIMIT(1, "vulkan: a draw needs more vertex bindings than the %u this GPU has (draw skipped)\n", maxbind);
        return 0;
    }
    /* Maxwell reads an attribute at any byte, Vulkan only at a multiple of its components' size, which a stride like
       one game's 69 for 4-byte floats breaks every other vertex: RADV's fetches hung the GPU on them */
    for (u32 b = 0; b < k->nbind; b++) {
        u32 st = (u32)pl->stride[b], a = align[b];
        if (!st || a < 2 || !(st % a)) continue;
        pl->guest_stride[b] = st;
        pl->stride[b] = (st + a - 1) / a * a;
        if (!g_vk.has_dynamic_stride) k->bind[b].stride = (u32)pl->stride[b];
    }

    /* attachments and blending */
    k->ncolor = n;
    for (u32 i = 0; i < n; i++) {
        if (!rt[i]) continue; /* format UNDEFINED, nothing written */
        k->color[i] = r[0x56E] & 1 ? rt[i]->f.vk : vk_srgb_as(rt[i]->f.vk, 0);
        u32 cm = r[0x3E4] ? r[0x680] : r[0x680 + i];
        k->blend[i].mask = ((cm & 0xF) ? VK_COLOR_COMPONENT_R_BIT : 0) |
                           ((cm & 0xF0) ? VK_COLOR_COMPONENT_G_BIT : 0) |
                           ((cm & 0xF00) ? VK_COLOR_COMPONENT_B_BIT : 0) |
                           ((cm & 0xF000) ? VK_COLOR_COMPONENT_A_BIT : 0);
        if (r[0x4D8 + i] && !rt[i]->f.is_int) {
            /* the alpha fields are used even without separate alpha, as on hardware */
            const u32* b = &r[0x780 + i * 8];
            u32 eq, sc, dc, eqa, sa, da;
            if (r[0x4B9]) {
                eq = b[1], sc = b[2], dc = b[3];
                eqa = b[4] ? b[4] : b[1], sa = b[5] ? b[5] : b[2], da = b[6] ? b[6] : b[3];
            } else {
                eq = r[0x4D0], sc = r[0x4D1], dc = r[0x4D2];
                eqa = r[0x4D3] ? r[0x4D3] : r[0x4D0], sa = r[0x4D4] ? r[0x4D4] : r[0x4D1];
                da = r[0x4D6] ? r[0x4D6] : r[0x4D2];
            }
            k->blend[i].en = 1;
            k->blend[i].cop = blend_op(eq);
            k->blend[i].sc = blend_factor(sc);
            k->blend[i].dc = blend_factor(dc);
            k->blend[i].aop = blend_op(eqa);
            k->blend[i].sa = blend_factor(sa);
            k->blend[i].da = blend_factor(da);
            if((k->blend[i].dc>=VK_BLEND_FACTOR_SRC1_COLOR || k->blend[i].da>=VK_BLEND_FACTOR_SRC1_COLOR) && fs){
                LOG_LIMIT(14,"world-blend: fs=%llx out=%x ctl=%x blend=%x ind=%x n=%u slot=%u sc=%x dc=%x sa=%x da=%x dual=%u\n",
                    (unsigned long long)fs->hash,fs->si.sph[18],r[0x487],r[0x4d8+i],r[0x4b9],n,i,sc,dc,sa,da,fs->mask[6]);
            }
        }
    }
    if (zt) {
        k->depth = zt->f.vk;
        k->stencil = zt->f.stencil ? zt->f.vk : VK_FORMAT_UNDEFINED;
    }
    return 1;
}

/* A range's highest index is kept until its memory is written (by the GPU or, through the write tracking, the CPU):
   scanning the indices of every draw was a tenth of one game's GPU thread, most of them meshes' that never change.
   The baselines are taken before the bytes are read, as buf_get takes them. One game reads about 1,900 index ranges a
   frame: 256 entries answered 55% of them, and scanning the rest again was a twentieth of a draw's setup */
static struct {
    u64 a, n, gw;
    u32 ws, isz, ri, skip, top;
} g_top_memo[4096];
static u32 top_memo_slot(u64 a) { return (u32)((a * 0x9E3779B97F4A7C15ull) >> 52); }
/* A draw's entry above and its indices' range in the buffer cache, fetched when the draw starts: each was a cache miss
   in the middle of its setup, the entry above the most frequent last-level miss in vk_render_draw's own code in one
   game */
static void draw_indices_prefetch(const GpuChannel* ch, int indexed, u32 first, u32 count, int step) {
    const u32* r = ch->r3d;
    u32 ifmt = r[0x5F6], isz = ifmt == 0 ? 1 : ifmt == 1 ? 2 : 4;
    u64 a = (((u64)r[0x5F2] << 32) | r[0x5F3]) + (u64)first * isz;
    if (indexed != 1 && indexed != 2) return;
    if (!step) __builtin_prefetch(&g_top_memo[top_memo_slot(a)]);
    vk_buf_prefetch(a, (u64)count * isz, step);
}
/* the len bytes of a vertex stream at gva, its vertices gstride apart, copied to staging with them stride apart
   instead: each vertex's stride bytes, so an attribute reaching past the guest's stride still reads the next vertex's
   first bytes. The stream lands first if the GPU wrote it */
static int copy_restrided(u64 gva, u64 len, u32 gstride, u32 stride, VkBuffer* buf, VkDeviceSize* off,
                          VkDeviceSize* size) {
    u64 nv = (len + gstride - 1) / gstride;
    u8* src = malloc((size_t)len);
    u8* dst = src ? vk_stage_upload(nv * stride, vk_stage_align(), buf, off) : NULL;
    if (!dst) {
        free(src);
        return 0;
    }
    gmem_read(gva, src, len);
    for (u64 i = 0; i < nv; i++) {
        u64 at = i * gstride, n = len - at < stride ? len - at : stride;
        memcpy(dst + i * stride, src + at, (size_t)n);
        if (n < stride) memset(dst + i * stride + n, 0, (size_t)(stride - n));
    }
    free(src);
    *size = nv * stride;
    return 1;
}
/* The last vertex a draw reads, for its streams' ranges, or ~0 when that isn't known: indices outside contiguous guest
   memory, or under results the GPU hasn't landed. A stream's limit is often the end of a ring the game streams
   vertices through, so its range from the draw's start to the limit was megabytes for a few hundred vertices: one
   game copied 100 to 360 MB a frame that way */
static u64 draw_last_vertex(GpuChannel* ch, int indexed, u32 first, u32 count, u32 base_vertex) {
    const u32* r = ch->r3d;
    u64 top = 0;
    if (!indexed) return (u64)first + count - 1;
    if (indexed == 3) { /* inline indices, 32-bit */
        for (u32 j = 0; j < count; j++)
            if (ch->inline_indices[first + j] > top) top = ch->inline_indices[first + j];
    } else {
        u32 ifmt = r[0x5F6], isz = ifmt == 0 ? 1 : ifmt == 1 ? 2 : 4;
        u64 ia = ((u64)r[0x5F2] << 32) | r[0x5F3], iend = ((u64)r[0x5F4] << 32) | r[0x5F5];
        if (iend < ia || first >= (iend - ia + 1) / isz) return ~0ull;
        u64 n = (iend - ia + 1) / isz - first, a = ia + (u64)first * isz;
        VkBuffer tb;
        u64 to;
        if (n > count) n = count;
        if (vk_buf_pending(a, n * isz) || vk_compute_gpu_src(a, n * isz, &tb, &to)) return ~0ull;
        vk_compute_mem_need(a, n * isz);
        /* the restart index, and all ones where Vulkan restarts instead, mark no vertex */
        u32 ones = isz == 1 ? 0xFFu : isz == 2 ? 0xFFFFu : 0xFFFFFFFFu, ri = r[0x591] ? r[0x592] & ones : ones;
        u32 skip = r[0x591] ? ones : 0;
        typeof(g_top_memo[0])* memo = g_top_memo;
        u32 slot = top_memo_slot(a), ws = vk_ww_now();
        u64 gw = gpu_mem_generation();
        const u8* p = gmem_ptr(a, n * isz);
        if (!p) return ~0ull;
        if (memo[slot].a == a && memo[slot].n == n && memo[slot].isz == isz && memo[slot].ri == ri &&
            memo[slot].skip == skip && !gpu_mem_dirty_since(memo[slot].gw, a, n * isz) &&
            vk_written_since(p, n * isz, memo[slot].ws) == 0)
            top = memo[slot].top;
        else {
            /* a loop for each size, in signed arithmetic where the indices fit it, which SSE2 makes vector maxima of */
            if (isz == 4) {
                u32 m = 0;
                for (u64 j = 0; j < n; j++) {
                    u32 v = snx_ld_le32(p + j * 4);
                    v = v == ri || v == skip ? 0 : v;
                    m = v > m ? v : m;
                }
                top = m;
            } else {
                i32 m = 0, sri = (i32)ri, sskip = (i32)skip;
                if (isz == 1)
                    for (u64 j = 0; j < n; j++) {
                        i32 v = p[j];
                        v = v == sri || v == sskip ? 0 : v;
                        m = v > m ? v : m;
                    }
                else
                    for (u64 j = 0; j < n; j++) {
                        i32 v = snx_ld_le16(p + j * 2);
                        v = v == sri || v == sskip ? 0 : v;
                        m = v > m ? v : m;
                    }
                top = (u32)m;
            }
            memo[slot].a = a;
            memo[slot].n = n;
            memo[slot].gw = gw;
            memo[slot].ws = ws;
            memo[slot].isz = isz;
            memo[slot].ri = ri;
            memo[slot].skip = skip;
            memo[slot].top = (u32)top;
        }
    }
    i64 last = (i64)top + (i32)base_vertex;
    return last < 0 ? 0 : (u64)last;
}

static void render_draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex,
                        u32 base_instance, u32 instances);
/* a draw as a profiler zone (SNX_TRACY builds), around its many ways out */
void vk_render_draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex, u32 base_instance,
                    u32 instances) {
    SNX_ZONE(z, "draw");
    render_draw(ch, indexed, topology, first, count, base_vertex, base_instance, instances);
    SNX_ZONE_END(z);
}
/* Diagnostic CPU snapshots only. Never flush or mutate guest state here.
   Pending GPU-written constants are explicitly omitted rather than mislabeled. */
#define REF_DRAW_MAX 8
void vk_reflection_bindings_capture(const char* prefix);
static struct {
    u64 hash, target;
    u32 present, mask, nt;
    struct { u64 address; u32 size, status; u8 data[65536]; } cb[18];
    struct { u64 address; u32 word, tic[8], tsc[8], format, levels; } tex[SH_MAXTEX];
} reflection_draw[REF_DRAW_MAX];
static u32 reflection_present = ~0u, reflection_count, reflection_dropped;
static int reflection_enabled(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SNX_REFLECTION_CAPTURE"); on = e && !strcmp(e,"1"); }
    return on;
}
#include <math.h>
#include "prepare_sky.h"
#include "vk_dlaa.h"
#include "material_flow_sky.h"
#include "../motion/cut_frame.h"
#include "../motion/capture.inc"
#include "nx/runtime/platform/plat.h"
#include <stdarg.h>
void vk_fsr_log(const char *fmt,...){char buf[1024];va_list a;va_start(a,fmt);vsnprintf(buf,sizeof buf,fmt,a);va_end(a);LOG("%s",buf);}
void vk_fsr_message(const char*t,bool error,const wchar_t*m){LOG_LIMIT(16,"%s error=%d: %ls\n",t,error,m);}
typedef struct {VkImage im; VmaAllocation mem; VkImageView view; VkImageLayout layout;} THist;
static THist th[2][2];
static VkDescriptorSetLayout tdsl; static VkPipelineLayout tpl; static VkPipeline tpipe; static VkSampler tsamp;
static u32 tlast,tw,thh,tindex,tjitframe; static int tready,tjitvalid;
static float tprev[16]; static u64 tzaddr;
static float up_halton(u32 x,u32 b){float v=0,a=1;while(x){a/=b;v+=a*(x%b);x/=b;}return v;}
extern int arceus_contact_supported;
extern u32 arceus_texture_slots;
static void tjitter(float *xy){u32 phase=1+g_vk_present%14;xy[0]=up_halton(phase,2)-.5f;xy[1]=up_halton(phase,3)-.5f;}
#include "world-limits.h"
static int tinit(void){
 static int attempted;if(attempted)return tpipe!=0;attempted=1;
 VkFormatProperties a,bfmt;vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_R16G16B16A16_SFLOAT,&a);vkGetPhysicalDeviceFormatProperties(g_vk.phys,VK_FORMAT_B10G11R11_UFLOAT_PACK32,&bfmt);if(!(a.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(bfmt.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))return 0;
 VkDescriptorSetLayoutBinding b[12];for(int i=0;i<8;i++) b[i]=(VkDescriptorSetLayoutBinding){i,(i==0||i==2||i>=5)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 b[8]=(VkDescriptorSetLayoutBinding){8,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,arceus_texture_slots,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 for(u32 i=9;i<12;i++)b[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
 VkDescriptorSetLayoutCreateInfo dc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=arceus_contact_supported?12:8,.pBindings=b};
 if(vkCreateDescriptorSetLayout(g_vk.dev,&dc,0,&tdsl))return 0;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,128};
 VkPipelineLayoutCreateInfo lc={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&tdsl,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(vkCreatePipelineLayout(g_vk.dev,&lc,0,&tpl))return 0;
 VkSamplerCreateInfo sc={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
 if(vkCreateSampler(g_vk.dev,&sc,0,&tsamp))return 0;
 VkShaderModule m=vk_module_spv(up_words,sizeof up_words/4);if(!m)return 0;
 VkComputePipelineCreateInfo pc={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=m,.pName="main"},.layout=tpl};
 VkResult r=vkCreateComputePipelines(g_vk.dev,vk_pipeline_cache(),1,&pc,0,&tpipe);vkDestroyShaderModule(g_vk.dev,m,0);return !r;
}
static int timages(u32 w,u32 h){
 if(tw==w && thh==h)return 1;
 tw=thh=0;tready=0;temporal_snapshot.backend=NULL;
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];if(t->im)vk_defer_image(t->im,t->mem,t->view,0);memset(t,0,sizeof *t);}
 for(int i=0;i<4;i++){THist *t=&th[i/2][i%2];VkFormat f=i==1?VK_FORMAT_R32_SFLOAT:i==2?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT;
 VkImageCreateInfo c={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=f,.extent={i==3?w:1920,i==3?h:1080,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
 if(!vk_image_create(&c,&t->im,&t->mem))return 0;
 VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=t->im,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=f,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 if(vkCreateImageView(g_vk.dev,&v,0,&t->view))return 0;}
 tw=w;thh=h;return 1;
}
#include "../taa/temporal_camera.h"
static void tuse(VkCommandBuffer cb,THist *t,VkImageLayout l){
 vk_barrier(cb,t->im,VK_IMAGE_ASPECT_COLOR_BIT,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,t->layout,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,l);t->layout=l;
}
static u32 cap_frame;static u64 cap_color,cap_depth;static float cap_matrix[16];static int cap_valid;
static struct {u64 address;VkImage image;u32 tic[8],tsc[8];} worldProbe,worldDiffuse;
typedef struct {u64 address,write;VkImage image;} LightingImage;
static LightingImage lightingAlbedo,lightingEmission,lightingNormal;
static Surf *lighting_surface(const LightingImage *input,u32 w,u32 h){
    if(!input->image)return NULL;
    for(Surf *s=vk_surf_first(input->address);s;s=s->next)
        if(s->gva==input->address && s->img==input->image && s->wseq==input->write &&
           s->sw==w && s->sh==h && s->layout!=VK_IMAGE_LAYOUT_UNDEFINED)return s;
    return NULL;
}
static struct {float direction[4],radiance[4];} mainLight;
static void read_main_light(GpuChannel *ch,const VkSh *fs){
    memset(&mainLight,0,sizeof mainLight);
    u64 addr=ch->cbind[4][3];
    if(!addr || cb_range(ch,4,fs,3)<0x960 || vk_buf_pending(addr+0x490,16) ||
       vk_compute_pending(addr+0x490,16) || vk_buf_pending(addr+0x950,16) ||
       vk_compute_pending(addr+0x950,16))return;
    const void *d=gmem_host(addr+0x490,16),*c=gmem_host(addr+0x950,16);
    if(!d || !c)return;
    float dir[4],color[4];
    memcpy(dir,d,16);memcpy(color,c,16);
    float length=dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2];
    if(!(length>.99f && length<1.01f && color[3]>0 && color[3]<=64.f))return;
    for(u32 i=0;i<3;i++)if(!(color[i]>=0 && color[i]<=16.f))return;
    for(u32 i=0;i<3;i++){
        mainLight.direction[i]=-dir[i]/sqrtf(length);
        mainLight.radiance[i]=color[i]*color[3];
    }
    mainLight.direction[3]=1.f;
}
static struct { u64 address, write; VkImage image; } cap_material;
static FlowGpu material_flow;
static int material_init_attempted;
static u32 material_last_frame;
static u64 material_last_address;
static float material_last_jitter[2];
static int material_surface_valid(const Surf *s,u32 w,u32 h){
 return s && s->layers==1 && s->sw==w && s->sh==h &&
        s->f.vk==VK_FORMAT_R8G8B8A8_UNORM && s->layout!=VK_IMAGE_LAYOUT_UNDEFINED;
}
static Surf *up_material_surface(u32 w,u32 h){
 if(!cap_material.image)return NULL;
 for(Surf *s=vk_surf_first(cap_material.address);s;s=s->next){
  if(s->gva==cap_material.address && s->img==cap_material.image && s->wseq==cap_material.write && material_surface_valid(s,w,h))return s;
 }
 return NULL;
}
static void upscale_scene(Surf *col,Surf *z,bool opaqueStage);
static void taa_after(GpuChannel *ch,VkSh *fs,TexRes *tex,Surf *col){
 if(!fs||!col||(fs->hash!=0x89619027ba43e5d3ull&&fs->hash!=0xe02c1a109a77fac4ull)||col->f.vk!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||col->layers!=1||ch->render_pred)return;
 cap_valid=0;
 lightingAlbedo.image=lightingEmission.image=lightingNormal.image=VK_NULL_HANDLE;
 cap_material.image=VK_NULL_HANDLE;worldProbe.image=VK_NULL_HANDLE;worldDiffuse.image=VK_NULL_HANDLE;
 Surf *z=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have){
  Surf *s=tex[i].t;
  if(s && fs->si.tex[i].cbuf_off==0x36 && vk_tex_view_type(&fs->si.tex[i])==VK_IMAGE_VIEW_TYPE_CUBE){
       worldProbe.address=s->gva;worldProbe.image=s->img;
       memcpy(worldProbe.tic,tex[i].tic,sizeof worldProbe.tic);memcpy(worldProbe.tsc,tex[i].tsc,sizeof worldProbe.tsc);
   }
   if(s && fs->si.tex[i].cbuf_off==0x34 && vk_tex_view_type(&fs->si.tex[i])==VK_IMAGE_VIEW_TYPE_CUBE){
       worldDiffuse.address=s->gva;worldDiffuse.image=s->img;
       memcpy(worldDiffuse.tic,tex[i].tic,sizeof worldDiffuse.tic);memcpy(worldDiffuse.tsc,tex[i].tsc,sizeof worldDiffuse.tsc);
   }
   if(fs->si.tex[i].cbuf_off==0x32)z=s;
   if(fs->si.tex[i].cbuf_off==0x2c && material_surface_valid(s,1920,1080))
       lightingNormal=(LightingImage){s->gva,s->wseq,s->img};
   if(s && fs->si.tex[i].cbuf_off==0x30 && s->sw==1920 && s->sh==1080 &&
      s->layers==1 && s->f.vk==VK_FORMAT_B10G11R11_UFLOAT_PACK32)
       lightingEmission=(LightingImage){s->gva,s->wseq,s->img};
   if(s && fs->si.tex[i].cbuf_off==0x2a && s->sw==1920 && s->sh==1080 &&
      s->layers==1 && s->f.vk==VK_FORMAT_R8G8B8A8_SRGB){
       lightingAlbedo.address=s->gva;
       lightingAlbedo.image=s->img;
       lightingAlbedo.write=s->wseq;
   }
  if(fs->si.tex[i].cbuf_off==0x2e && material_surface_valid(s,1920,1080)){
   cap_material.image=s->img;cap_material.address=s->gva;cap_material.write=s->wseq;
  }
 }
 if(!z||z->sw!=1920||z->sh!=1080||col->sw!=1920||col->sh!=1080||z->f.vk!=VK_FORMAT_D24_UNORM_S8_UINT)return;
 read_main_light(ch,fs);
 u64 addr=ch->cbind[4][4];VkBuffer b;u64 off;if(!addr||cb_range(ch,4,fs,4)<256||vk_buf_pending(addr,256)||vk_compute_gpu_src(addr,256,&b,&off))return;
 const void *m=gmem_ptr(addr+192,64);if(!m)return;memcpy(cap_matrix,m,64);for(int i=0;i<16;i++)if(!isfinite(cap_matrix[i]))return;
 cap_frame=g_vk_present;cap_color=col->gva;cap_depth=z->gva;cap_valid=1;
 upscale_scene(col,z,true);
}
#include "contact.h"
#include "denoise.inc"
#include "rr-render.inc"

#include "opaque-scene.inc"
static void upscale_scene(Surf *col,Surf *z,bool opaqueStage){
 if(upscale_disabled)return;
 if(opaqueStage && (!tready || tlast+1!=g_vk_present || previous_color!=cap_color || tzaddr!=cap_depth))return;
 if(!col||!z||col->sw!=1920||col->sh!=1080||z->sw!=1920||z->sh!=1080)return;
 if(tready && tlast==g_vk_present){
  if(previous_color==cap_color && tzaddr==cap_depth && previous_color_write==col->wseq &&
     previous_depth_write==z->wseq && !memcmp(cap_matrix,tprev,sizeof tprev)){
    up_override=th[1][1].view;
    up_address=cap_color;
  }
  return;
 }
 rr_capture_previous();
 if(!opaqueStage && opaque.frame==g_vk_present && opaque.color==col->img && opaque.depth==z->img){
     opaque_resolve(col,z);
     return;
 }
 if(opaqueStage && !opaque_image_ready(col->sw,col->sh))return;
 float *cur=cap_matrix;
 VkComponentMapping id={0};VkImageView cv=vk_tex_view(col,VK_IMAGE_VIEW_TYPE_2D,0,&id,0),zv=vk_tex_view(z,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
 if(!cv||!zv||!tinit()||!timages(3840,2160)){LOG_LIMIT(2,"arceus-taa: resources unavailable\n");return;}
 VkDescriptorSet set=vk_desc_alloc(tdsl);if(!set)return;
 VkCommandBuffer cb=vk_cmd();vk_render_end();
 const VkImageLayout ro=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,rw=VK_IMAGE_LAYOUT_GENERAL;
 vk_surf_use(cb,col,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 vk_surf_use(cb,z,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
 THist *color=&th[0][0],*depth=&th[0][1],*motion=&th[1][0],*dst=&th[1][1];
 u32 w=col->sw,h=col->sh;
 for(int i=0;i<4;i++)tuse(cb,&th[i/2][i%2],rw);
 VkImageView albedoView=VK_NULL_HANDLE;
 if(lightingAlbedo.image && world.albedoAddress==lightingAlbedo.address){
     for(Surf *surface=vk_surf_first(world.albedoAddress);surface;surface=surface->next){
         if(surface->gva!=world.albedoAddress || surface->img!=lightingAlbedo.image ||
            surface->wseq!=lightingAlbedo.write || surface->layout==VK_IMAGE_LAYOUT_UNDEFINED ||
            surface->sw!=w || surface->sh!=h)continue;
         albedoView=vk_tex_view(surface,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
         if(albedoView){vk_surf_touch(surface);vk_surf_use(cb,surface,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);break;}
     }
 }
 VkImageView probeView=VK_NULL_HANDLE;
 if(worldProbe.image){
     for(Surf *probe=vk_surf_first(worldProbe.address);probe;probe=probe->next){
         if(probe->img!=worldProbe.image||probe->gva!=worldProbe.address)continue;
         probeView=vk_tex_bind_view(probe,VK_IMAGE_VIEW_TYPE_CUBE,worldProbe.tic,worldProbe.tsc,0);
         if(probeView){vk_surf_touch(probe);vk_surf_use(cb,probe,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);}
         break;
     }
 }
 VkImageView diffuseView=VK_NULL_HANDLE;
 if(worldDiffuse.image){
     for(Surf *probe=vk_surf_first(worldDiffuse.address);probe;probe=probe->next){
         if(probe->img!=worldDiffuse.image||probe->gva!=worldDiffuse.address)continue;
         diffuseView=vk_tex_bind_view(probe,VK_IMAGE_VIEW_TYPE_CUBE,worldDiffuse.tic,worldDiffuse.tsc,0);
         if(diffuseView){vk_surf_touch(probe);vk_surf_use(cb,probe,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);}
         break;
     }
 }
 VkImageView views[]={zv,motion->view,cv,color->view,depth->view,albedoView?albedoView:cv,probeView,diffuseView};VkDescriptorImageInfo ii[8];VkWriteDescriptorSet wr[8];
 for(int i=0;i<(probeView && diffuseView?8:6);i++){int sampled=i==0||i==2||i>=5;ii[i]=(VkDescriptorImageInfo){sampled?tsamp:0,views[i],sampled?ro:rw};wr[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=sampled?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=ii+i};} vkUpdateDescriptorSets(g_vk.dev,probeView && diffuseView?8:6,wr,0,NULL);
 u32 surfaceInputs=0;
 if(probeView && diffuseView && albedoView && drawCount){
     bind_world_textures(cb,set,albedoView,ro);
     Surf *extras[]={lighting_surface(&lightingEmission,w,h),lighting_surface(&lightingNormal,w,h),up_material_surface(w,h)};
     VkDescriptorImageInfo infos[3];VkWriteDescriptorSet writes[3];
     for(u32 i=0;i<3;i++){
         Surf *s=extras[i];
         VkImageView view=s?vk_tex_view(s,VK_IMAGE_VIEW_TYPE_2D,0,&id,0):VK_NULL_HANDLE;
         if(view){
             vk_surf_touch(s);vk_surf_use(cb,s,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
             surfaceInputs|=1u<<i;
         }
         infos[i]=(VkDescriptorImageInfo){tsamp,view?view:albedoView,ro};
         writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=9+i,
             .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=infos+i};
     }
     vkUpdateDescriptorSets(g_vk.dev,3,writes,0,NULL);
 }
 struct{float m[16],j[2];u32 valid;float skyDepth;}pc={0};if(tjitvalid&&tjitframe==g_vk_present)tjitter(pc.j);
 if(getenv("ARCEUS_QA_JITTER") && g_vk_present%90==0)
     LOG("arceus-qa: resolve present=%u last=%u jitterFrame=%u jitter=%g,%g depth=%llx\n",g_vk_present,tlast,tjitframe,pc.j[0],pc.j[1],(unsigned long long)z->gva);
 if(fabsf(cur[11])>1.e-8f)pc.skyDepth=-cur[15]/cur[11];
 pc.valid=tready && tlast+1==g_vk_present && previous_color==cap_color && tzaddr==z->gva && tmatrix(cur,tprev,pc.m);
 rec_CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpipe);rec_CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,tpl,0,1,&set,0,NULL);
 rec_CmdPushConstants(cb,tpl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof pc,&pc);rec_CmdDispatch(cb,(w+7)/8,(h+7)/8,1);
 bool dl=vk_dlaa_wanted();
 static bool initialized;
 if(dl && !initialized){initialized=true;vk_dlaa_init(g_vk.instance,g_vk.phys,g_vk.dev);}
 const bool requested=rr_requested();
 if(requested && dl && !vk_dlaa_available())dl=false;
 rr.writing=requested && dl && surfaceInputs && probeView && diffuseView && albedoView && drawCount &&
     vk_rr_available() && rr_prepare(w,h);
 bool traced=probeView && diffuseView && albedoView && drawCount && contact_record(cb,set,cur,surfaceInputs,pc.j);
 if(opaqueStage && !traced){vk_render_forget_state();return;}
 VkImageLayout vendor_layout=dl?rw:ro;
 tuse(cb,color,vendor_layout);tuse(cb,depth,vendor_layout);tuse(cb,motion,rw);
 Surf *material=up_material_surface(w,h);
 VkImageView material_view=VK_NULL_HANDLE;
 VkDescriptorSet flow_set=VK_NULL_HANDLE;
 if(material && !material_init_attempted){
  material_init_attempted=1;
  const char *setting=getenv("SNX_ARCEUS_MATERIAL_MOTION");
  if(!setting || strcmp(setting,"0")){
   VkResult result=flow_create(&material_flow,g_vk.phys,g_vk.dev,w,h,vk_pipeline_cache());
   LOG("arceus-motion: material estimator initialization %d at %ux%u\n",result,w,h);
  }
 }
 if(material && material_flow.setLayout){
  material_view=vk_tex_view(material,VK_IMAGE_VIEW_TYPE_2D,0,&id,0);
  if(material_view)flow_set=vk_desc_alloc(material_flow.setLayout);
  if(flow_set){
   vk_surf_touch(material);
   vk_surf_use(cb,material,ro,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
 }
 vk_rec_sync(); /* finish all queued records before the vendor directly records to this same command buffer */
 world_mark(cb,7);
 static u64 prevtime;u64 now=plat_ns();float ms=prevtime?(float)(now-prevtime)*1e-6f:8.333f;prevtime=now;
 VkFsr2Frame f={.color=color->im,.depth=depth->im,.mv=motion->im,.out=dst->im,.color_view=color->view,.depth_view=depth->view,.mv_view=motion->view,.out_view=dst->view,.color_format=VK_FORMAT_R16G16B16A16_SFLOAT,.depth_format=VK_FORMAT_R32_SFLOAT,.out_format=VK_FORMAT_R16G16B16A16_SFLOAT,.w=w,.h=h,.ow=tw,.oh=thh,.jx=-pc.j[0],.jy=-pc.j[1],.dt_ms=fminf(100,fmaxf(1,ms)),.near_z=1.f/fmaxf(.001f,cur[11]+cur[15]),.far_z=1.f/fmaxf(.000001f,cur[15]),.fov_y=2*atanf(sqrtf(cur[4]*cur[4]+cur[5]*cur[5]+cur[6]*cur[6])),.reset=!pc.valid||ms>200};
 bool material_history=false;
 FlowParams p={0};
 memcpy(p.clipToPrevious,pc.m,sizeof pc.m);memcpy(p.jitter,pc.j,sizeof pc.j);
 memcpy(p.previousJitter,material_last_jitter,sizeof p.previousJitter);
 if(flow_set){
  static bool lastFlowUsedAlbedo;
  bool flowUsesAlbedo=traced && albedoView;
  p.historyValid=material_flow.warm && !f.reset && material_last_frame+1==g_vk_present &&
      material_last_address==cap_material.address && flowUsesAlbedo==lastFlowUsedAlbedo;
  material_history=p.historyValid;
  // Shading noise changes even on a stationary object. Match the underlying texture.
  FlowInputs inputs={flowUsesAlbedo?albedoView:color->view,depth->view,material_view,motion->view,
                     flowUsesAlbedo?ro:vendor_layout,vendor_layout,ro};
  flow_record(&material_flow,cb,flow_set,&inputs,p);
  lastFlowUsedAlbedo=flowUsesAlbedo;
  static int motionReactiveOverride=-2;
  if(motionReactiveOverride==-2){
   const char *setting=getenv("ARCEUS_FSR_MATERIAL_REACTIVE");
   motionReactiveOverride=setting?(strcmp(setting,"0")!=0):-1;
  }
  // An opaque path sample can change even when the material and camera stay still.
  // Its matching cost cannot identify transparency in this pass.
  bool useMotionReactive=motionReactiveOverride<0?!traced:motionReactiveOverride!=0;
  if(!dl && material_history && useMotionReactive){
   flow_mask_layout(&material_flow,cb,ro);
   f.reactive=material_flow.images[FlowMask].image;f.reactive_view=material_flow.images[FlowMask].view;
  }
  material_last_frame=g_vk_present;material_last_address=cap_material.address;
  memcpy(material_last_jitter,pc.j,sizeof pc.j);
 }else material_flow.warm=0;
 SceneResolve input={.f=f,.flow=p,.albedo=albedoView,.materialView=flow_set?material_view:VK_NULL_HANDLE,
     .materialAddress=material?material->gva:0,.dl=dl,.traced=traced,.guides=traced && rr.writing,
     .hasFlow=flow_set!=VK_NULL_HANDLE,.materialHistory=material_history};
 if(opaqueStage){
     if(input.guides && !vk_rr_create(cb,&input.f))input.guides=false;
     if(!input.guides)scene_filter(cb,&input);
     opaque_write(cb,col,z,&input);
     return;
 }
 rr.guides.beforeImage=VK_NULL_HANDLE;rr.guides.beforeView=VK_NULL_HANDLE;
 scene_finish(cb,col,z,&input);
}
static void upscale_before(VkSh *fs,TexRes *tex,Surf *target){
 up_override=0;up_address=0;
 if(!cap_valid||cap_frame!=g_vk_present||!fs||!target||target->sw!=3840||target->sh!=2160||
   (fs->hash!=0x63b3848c1a6e23e0ull && fs->hash!=0x8a160a27d90af72dull))return;
 Surf *col=NULL,*z=NULL;
 for(int i=0;i<fs->si.ntex&&i<SH_MAXTEX;i++)if(tex[i].have&&tex[i].t){
  if(tex[i].t->gva==cap_color)col=tex[i].t;
  if(tex[i].t->gva==cap_depth)z=tex[i].t;
 }
 if(col && z)upscale_scene(col,z,false);
}


static void reflection_record(GpuChannel* ch, VkSh* fs, const TexRes* tex) {
    if (!reflection_enabled() || !fs || !tex) return;
    if (reflection_present != g_vk_present) {
        reflection_present = g_vk_present; reflection_count = 0; reflection_dropped = 0;
    }
    /* Both known deferred lighting variants. Other shaders remain outside scope. */
    if (fs->hash != 0xe02c1a109a77fac4ull && fs->hash != 0x89619027ba43e5d3ull) return;
    if (reflection_count == REF_DRAW_MAX) { reflection_dropped++; return; }
    typeof(reflection_draw[0])* d = &reflection_draw[reflection_count++];
    d->hash = fs->hash; d->present = g_vk_present; d->mask = fs->si.cbuf_mask & 0x3ffff;
    d->target = g_rp.n && g_rp.rt[0] ? g_rp.rt[0]->gva : 0;
    for (u32 i = 0; i < 18; i++) {
        d->cb[i].status = 0; d->cb[i].size = 0; d->cb[i].address = ch->cbind[4][i];
        if (!(d->mask & (1u << i))) continue;
        u32 n = cb_range(ch,4,fs,i); d->cb[i].size = n;
        u64 a = d->cb[i].address;
        if (!a) { memset(d->cb[i].data,0,n); d->cb[i].status = 1; continue; }
        VkBuffer b; u64 off;
        if (vk_buf_pending(a,n) || vk_compute_gpu_src(a,n,&b,&off)) { d->cb[i].status = 2; continue; }
        const void* p = gmem_ptr(a,n);
        if (!p) { d->cb[i].status = 3; continue; }
        memcpy(d->cb[i].data,p,n); d->cb[i].status = 1;
        if (rim_candidate_enabled() && rim_candidate_match(d->cb[i].data,n,4,i,fs->hash))
            rim_candidate_apply(d->cb[i].data);
    }
    d->nt = fs->si.ntex < SH_MAXTEX ? fs->si.ntex : SH_MAXTEX;
    for (u32 i = 0; i < d->nt; i++) {
        memset(&d->tex[i],0,sizeof d->tex[i]);
        d->tex[i].word = fs->si.tex[i].cbuf_off;
        if (!tex[i].have) continue;
        memcpy(d->tex[i].tic,tex[i].tic,sizeof d->tex[i].tic);
        memcpy(d->tex[i].tsc,tex[i].tsc,sizeof d->tex[i].tsc);
        if (tex[i].t) { d->tex[i].address=tex[i].t->gva; d->tex[i].format=tex[i].t->f.vk; d->tex[i].levels=tex[i].t->levels; }
    }
}
void vk_reflection_bindings_capture(const char* prefix) {
    if (!reflection_enabled() || !prefix || !*prefix || strpbrk(prefix,"\t\r\n")) return;
    temporal_snapshot_capture(prefix);
    char path[1100];
    int n=snprintf(path,sizeof path,"%s_lighting.tsv",prefix);
    if(n<0 || (size_t)n>=sizeof path) return;
    FILE* f=fopen(path,"wb"); if(!f) { LOG("reflection-bindings: cannot open manifest\n"); return; }
    u32 failures=0;
    fprintf(f,"# capture_present=%u recorded_present=%u draws=%u dropped=%u constants=CPU-at-draw-with-enabled-rim-transform-not-GPU-readback status1=valid-CPU status2=GPU-pending-omitted status3=unmapped\n",g_vk_present,reflection_present,reflection_count,reflection_dropped);
    for(u32 i=0;i<reflection_count;i++) {
        typeof(reflection_draw[0])* d=&reflection_draw[i];
        fprintf(f,"draw\t%u\tpresent\t%u\tfs\t%llx\ttarget\t%llx\n",i,d->present,(unsigned long long)d->hash,(unsigned long long)d->target);
        for(u32 j=0;j<18;j++) if(d->mask & (1u<<j)) {
            n=snprintf(path,sizeof path,"%s_light_%u_cb%u.bin",prefix,i,j);
            int ok=0;
            if(d->cb[j].status==1 && n>=0 && (size_t)n<sizeof path) {
                FILE* b=fopen(path,"wb");
                if(b) { ok=fwrite(d->cb[j].data,1,d->cb[j].size,b)==d->cb[j].size; if(fclose(b)) ok=0; }
                if(!ok) failures++;
            }
            fprintf(f,"cb\t%u\t%u\t%llx\t%u\tstatus\t%u\twritten\t%d\t%s\n",i,j,(unsigned long long)d->cb[j].address,d->cb[j].size,d->cb[j].status,ok,ok?path:"-");
        }
        for(u32 j=0;j<d->nt;j++) {
            fprintf(f,"texture\t%u\t%u\tword\t%x\taddress\t%llx\tformat\t%u\tlevels\t%u\ttic",i,j,d->tex[j].word,(unsigned long long)d->tex[j].address,d->tex[j].format,d->tex[j].levels);
            for(u32 k=0;k<8;k++) fprintf(f,"\t%08x",d->tex[j].tic[k]);
            fprintf(f,"\ttsc");for(u32 k=0;k<8;k++) fprintf(f,"\t%08x",d->tex[j].tsc[k]);fputc('\n',f);
        }
    }
    if(ferror(f)) failures++; if(fclose(f)) failures++;
    LOG("reflection-bindings: present=%u recorded=%u draws=%u dropped=%u failures=%u\n",g_vk_present,reflection_present,reflection_count,reflection_dropped,failures);
}

/* Opt-in material metadata for textured geometry. This reads already resolved
   CPU metadata only: no guest reads, image copies, descriptor creation or draw
   state changes. Unlike resolution_trace, distinct mesh shaders and bindings
   are retained. Indexed descriptor tables are explicitly not expanded here. */
static void material_trace(VkViewport vp, VkRect2D sc, VkSh* vs, VkSh* fs,
                           const TexRes* tex, u32 count, u64 scaled, const u32* r) {
    enum { LIMIT = 512, TABLE = 2048 };
    static int enabled = -1;
    static u32 used;
    static u64 seen[TABLE];
    if (enabled < 0) {
        const char* e = getenv("SNX_MATERIAL_TRACE");
        enabled = e && !strcmp(e, "1");
        if (enabled)
            LOG("material-trace: resolved-input metadata enabled, limit=%u; indexed tables not expanded\n", LIMIT);
    }
    if (!enabled || used == LIMIT || !fs || !g_rp.zt) return;
    int nt = fs->si.ntex < SH_MAXTEX ? fs->si.ntex : SH_MAXTEX;
    struct {
        u64 vs, fs, scaled;
        u32 colors, ntex;
        u32 bias[6];
        VkViewport vp;
        VkRect2D sc;
    } head = {0};
    head.vs = vs ? vs->hash : 0; head.fs = fs->hash; head.scaled = scaled;
    head.colors = g_rp.n; head.ntex = (u32)nt; head.vp = vp; head.sc = sc;
    head.bias[0] = r[0x55B]; head.bias[1] = r[0x56F]; head.bias[2] = r[0x61F];
    head.bias[3] = r[0x370]; head.bias[4] = r[0x371]; head.bias[5] = r[0x372];
    u64 key = vk_hash(&head, sizeof head);
    for (u32 i = 0; i <= g_rp.n; i++) {
        const Surf* t = i == g_rp.n ? g_rp.zt : g_rp.rt[i];
        if (!t) continue;
        u64 output[] = {i, i == g_rp.n, t->gva, t->w, t->h, t->sw, t->sh, t->fkey, t->f.vk};
        key = (key ^ vk_hash(output, sizeof output)) * 0x9e3779b97f4a7c15ull;
    }
    for (int i = 0; i < nt; i++) {
        const ShTex* st = &fs->si.tex[i];
        if (st->flags & SNX_TEX_INDEXED) continue;
        const TexRes* x = &tex[i];
        u64 part[4] = {(u64)(u32)i, x->have, 0, 0};
        if (x->have) {
            part[2] = vk_hash(x->tic, sizeof x->tic);
            part[3] = vk_hash(x->tsc, sizeof x->tsc);
        }
        key = (key ^ vk_hash(part, sizeof part)) * 0x9e3779b97f4a7c15ull;
        if (x->have && x->t) {
            const Surf* t = x->t;
            u64 shape[] = {t->gva, t->w, t->h, t->sw, t->sh, t->layers, t->levels, t->depth3d,
                           t->fkey, t->f.vk, t->f.conv, t->is_rt, t->bl, t->bh, t->bd, t->pitch};
            key = (key ^ vk_hash(shape, sizeof shape)) * 0x9e3779b97f4a7c15ull;
        }
    }
    /* Generation counters and per-draw counts deliberately do not grow the key. */
    if (!key) key = 1;
    u32 at = (u32)(key >> 32) & (TABLE - 1);
    while (seen[at] && seen[at] != key) at = (at + 1) & (TABLE - 1);
    if (seen[at]) return;
    seen[at] = key;
    u32 id = ++used;
    LOG("material-trace: draw id=%u present=%u vs=%llx fs=%llx count=%u colors=%u viewport=%g,%g,%g,%g scissor=%d,%d,%u,%u scaled=%llx bias_raw=%08x,%08x,%08x bias_enable=%u,%u,%u\n",
        id, g_vk_present, (unsigned long long)head.vs, (unsigned long long)head.fs, count, g_rp.n,
        vp.x, vp.y, vp.width, vp.height, sc.offset.x, sc.offset.y, sc.extent.width, sc.extent.height,
        (unsigned long long)scaled, r[0x55B], r[0x56F], r[0x61F], r[0x370], r[0x371], r[0x372]);
    for (u32 i = 0; i <= g_rp.n; i++) {
        const Surf* t = i == g_rp.n ? g_rp.zt : g_rp.rt[i];
        if (!t) continue;
        LOG("material-trace: output id=%u slot=%u depth=%u address=%llx guest=%ux%u image=%ux%u fkey=%x vkfmt=%u\n",
            id, i, i == g_rp.n, (unsigned long long)t->gva, t->w, t->h, t->sw, t->sh, t->fkey, (u32)t->f.vk);
    }
    for (int i = 0; i < nt; i++) {
        const ShTex* st = &fs->si.tex[i];
        LOG("material-trace: shader-input id=%u slot=%d cbuf_word=%x bindless=%u cb=%u split=%u split_cb=%u split_word=%x type=%u array=%u shadow=%u flags=%x\n",
            id, i, st->cbuf_off, st->bindless, st->bl_cbuf, st->bl2, st->bl2_cbuf, st->bl2_off,
            st->type, st->array, st->shadow, st->flags);
        if (st->flags & SNX_TEX_INDEXED) {
            LOG("material-trace: input id=%u slot=%d indexed_table=1 details=not-expanded\n", id, i);
            continue;
        }
        const TexRes* x = &tex[i];
        if (!x->have) {
            LOG("material-trace: input id=%u slot=%d resolved=0\n", id, i);
            continue;
        }
        LOG("material-trace: descriptors id=%u slot=%d handle=%08x tic=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x tsc=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
            id, i, x->handle, x->tic[0], x->tic[1], x->tic[2], x->tic[3], x->tic[4], x->tic[5], x->tic[6], x->tic[7],
            x->tsc[0], x->tsc[1], x->tsc[2], x->tsc[3], x->tsc[4], x->tsc[5], x->tsc[6], x->tsc[7]);
        const Surf* t = x->t;
        if (!t) {
            LOG("material-trace: input id=%u slot=%d resolved=1 surface=0\n", id, i);
            continue;
        }
        VkComponentMapping map = tex_map(t, x->tic);
        LOG("material-trace: input id=%u slot=%d address=%llx guest=%ux%u image=%ux%u layers=%u levels=%u depth3d=%u rt=%d fkey=%x vkfmt=%u conversion=%u block=%ux%u bytes=%u signed=%u integer=%u depth=%u bgra=%u srgb=%d map=%u,%u,%u,%u layout=%u,%u,%u,%u upload_valid=%d wgen=%u\n",
            id, i, (unsigned long long)t->gva, t->w, t->h, t->sw, t->sh, t->layers, t->levels, t->depth3d,
            t->is_rt, t->fkey, (u32)t->f.vk, t->f.conv, t->f.bw, t->f.bh, t->f.bpp,
            t->f.sint, t->f.is_int, t->f.depth, t->f.bgra, tex_srgb(x->tic, x->tsc),
            (u32)map.r, (u32)map.g, (u32)map.b, (u32)map.a,
            t->bl, t->bh, t->bd, t->pitch, t->upload_valid, t->wgen);
    }
    if (used == LIMIT) LOG("material-trace: distinct configuration limit reached\n");
}

/* Opt-in resolution diagnosis. Log each distinct viewport/target combination once;
   retain shader identity for small fullscreen/postprocess draws, but not every mesh.
   Disabled runs pay only the cached flag check. No shader or rendering state changes. */
static void resolution_trace(VkViewport vp, VkRect2D sc, VkSh* fs, TexRes* tex, u32 count, u64 fs_addr) {
    static int enabled = -1;
    static struct {
        u32 w, h, sw, sh, format, n, depth, small;
        float vw, vh;
        u32 cw, ch;
        u64 shader;
    } seen[512];
    static u32 used;
    if (enabled < 0) {
        const char* e = getenv("SNX_RESOLUTION_TRACE");
        enabled = e && *e && *e != '0';
    }
    if (!enabled || used == 512) return;
    Surf* dst = NULL;
    for (u32 i = 0; i < g_rp.n && !dst; i++) dst = g_rp.rt[i];
    if (!dst) dst = g_rp.zt;
    if (!dst) return;
    typeof(seen[0]) key = {0};
    key.w = dst->w; key.h = dst->h; key.sw = dst->sw; key.sh = dst->sh;
    key.format = dst->fkey; key.n = g_rp.n; key.depth = g_rp.zt != NULL;
    key.small = count <= 6; key.vw = vp.width; key.vh = vp.height;
    key.cw = sc.extent.width; key.ch = sc.extent.height;
    key.shader = key.small && fs ? fs->hash : 0;
    for (u32 i = 0; i < used; i++) if (!memcmp(&seen[i], &key, sizeof key)) return;
    seen[used++] = key;
    LOG("resolution-trace: draw id=%u present=%u guest=%ux%u image=%ux%u viewport=%gx%g scissor=%ux%u colors=%u depth=%u count=%u target=%llx fs=%llx hash=%llx\n",
        used, g_vk_present, dst->w, dst->h, dst->sw, dst->sh, vp.width, vp.height,
        sc.extent.width, sc.extent.height, g_rp.n, key.depth, count,
        (unsigned long long)dst->gva, (unsigned long long)fs_addr,
        (unsigned long long)(fs ? fs->hash : 0));
    if (fs) for (int i = 0; i < fs->si.ntex && i < SH_MAXTEX; i++) {
        if (!tex[i].have || !tex[i].t) continue;
        Surf* t = tex[i].t;
        LOG("resolution-trace: input id=%u slot=%d target=%llx guest=%ux%u image=%ux%u rt=%d\n",
            used, i, (unsigned long long)t->gva, t->w, t->h, t->sw, t->sh, t->is_rt);
    }
    if (used == 512) LOG("resolution-trace: draw configuration limit reached\n");
}

static void render_draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex,
                        u32 base_instance, u32 instances) {
    u32* r = ch->r3d;
    /* the stages between the vertex and fragment programs and their sets */
    int bset_ok, b;
    VkSh *vs, *tcs, *tes, *gs, *between[3];
    VkDescriptorSet bset[3];
    if (!count || !instances) return;
    vk_frame_check();
    world_frame();
    if (g_draw.check < 0) {
        const char* e = getenv("SNX_DIRTY_CHECK");
        g_draw.check = e && *e && *e != '0';
        if (g_draw.check)
            LOG("vulkan: SNX_DIRTY_CHECK: draws that keep the last draw's shaders and pipeline make them again to "
                "compare\n");
    }
    /* the register groups changed since the kept state was made: all of them when another channel draws */
    if (g_draw.ch_id != ch->id) {
        g_draw.ch_id = ch->id;
        g_draw.pend = ~0u;
    }
    g_draw.pend |= ch->r3d_dirty;
    ch->r3d_dirty = 0;
    u32 initial_first = first, initial_count = count, draw_topo = topology;
    int retries = 0, initial_indexed = indexed;
retry:
    vk_buf_writable_abort();
    indexed = initial_indexed;
    first = initial_first;
    count = initial_count;
    u64 setup_frame = vk_submit_number();
    u64 code = ((u64)r[0x582] << 32) | r[0x583];
    u32 epoch = __atomic_load_n(&g_vk_epoch, __ATOMIC_RELAXED);
    /* kept: the state the last draw made holds for this one, as far as each check below tells. Each part is made
       here when it doesn't (and when SNX_DIRTY_CHECK compares) */
    int kept = draw_kept(ch, draw_topo, epoch);
    if (kept && g_draw.check) g_vk_draw_checked++;
    DrawShaders sh;
    if (kept) sh = g_draw.sh;
    if (!kept || g_draw.check) {
        DrawShaders made;
        if (!draw_shaders(ch, draw_topo, &made)) {
            if (kept) {
                draw_differs("shaders");
                g_draw.sh.vs = NULL;
            }
            g_vk_skipped++;
            return;
        }
        if (kept && !draw_shaders_same(&made, &sh)) {
            draw_differs("shaders");
            kept = 0;
        }
        sh = made;
    }
    topology = sh.topology;
    vs = sh.vs;
    tcs = sh.tcs;
    tes = sh.tes;
    gs = sh.gs;
    int xfb_on = sh.xfb_on;
    /* the fragment shader's variant, keyed on its textures each draw: they are resolved for stage_set anyway. fres is
       filled for the fragment program's textures (each resolved, have set), and read only for them: a clear of all
       SH_MAXTEX (10 KiB) and a check of all of them against each target were per draw */
    TexRes fres[SH_MAXTEX];
    VkSh* last = gs ? gs : tes ? tes : vs;
    VkSh* fs = NULL;
    FsKey fk = {0};
    if (sh.fs0) {
        fs_key(ch, sh.fs0, fres, last->si.out_mask, sh.rt_int, sh.rt_sint, &fk);
        if (kept && memcmp(&fk, &g_draw.fkey, sizeof fk)) kept = 0;
        if (kept) fs = g_draw.fs;
        if (!kept || g_draw.check) {
            VkSh* made = fs_variant(ch, code + r[0x851], sh.fs0, &fk);
            if (kept && made != fs) {
                draw_differs("fragment shader");
                kept = 0;
            }
            fs = made;
        }
        if (fs && fs->failed) {
            g_vk_skipped++;
            return;
        }
    }
    cb_prefetch(ch, 0, vs, 0);
    if (fs) cb_prefetch(ch, 4, fs, 0);
    draw_indices_prefetch(ch, indexed, first, count, 0);
    Surf* rt[8];
    Surf* zt;
    u32 n;
    read_targets(ch, rt, &zt, &n);
    for (u32 i = 0; i < n; i++)
        if (trace_target(rt[i]))
            LOG_LIMIT(64,
                      "gpu-trace-target: draw color slot %u target=%llx %ux%u count=%u indexed=%d vs=%llx fs=%llx\n", i,
                      (unsigned long long)rt[i]->gva, rt[i]->w, rt[i]->h, count, indexed,
                      (unsigned long long)(code + r[0x811]), (unsigned long long)(code + r[0x851]));
    if (trace_target(zt))
        LOG_LIMIT(64, "gpu-trace-target: draw depth target=%llx %ux%u count=%u indexed=%d vs=%llx fs=%llx\n",
                  (unsigned long long)zt->gva, zt->w, zt->h, count, indexed, (unsigned long long)(code + r[0x811]),
                  (unsigned long long)(code + r[0x851]));
    /* a color target the fragment shader samples but doesn't write (no output for it, or a write mask of 0) is drawn
       without: a game's lighting passes may keep the whole G-buffer bound while they read some of it, which
       Maxwell allows, and Vulkan can't sample an attachment of the pass */
    if (fs)
        for (u32 j = 0; j < n; j++) {
            u32 cm = r[0x3E4] ? r[0x680] : r[0x680 + j];
            if (!rt[j] || (((fs->si.sph[18] >> (j * 4)) & 15) && (cm & 0xFFFF))) continue;
            for (int i = 0; i < (fs->si.ntex > SH_MAXTEX ? SH_MAXTEX : fs->si.ntex); i++)
                if (fres[i].have && fres[i].t == rt[j]) {
                    rt[j] = NULL;
                    break;
                }
        }
    float raster_scale = target_raster_scale(rt, zt, n);
    if (!raster_scale) { g_vk_skipped++; return; }
    /* nothing drawn to, captured or stored. A program's stores to memory or images still run in a pass without
       attachments: a per-pixel pass may set every target's format to 0 and keep only what it stores */
    int stores = vs->si.gbuf_wmask || vs->si.nimg || (fs && (fs->si.gbuf_wmask || fs->si.nimg));
    if (!n && !zt && !xfb_on && !stores) {
        g_vk_skipped++;
        return;
    }
    static u32 last_cw, last_ch; /* the size of the last color target drawn to */
    if (n && rt[0]) {
        last_cw = rt[0]->w;
        last_ch = rt[0]->h;
    }
    if (!g_settings.shadows && !n && zt && (zt->w != last_cw || zt->h != last_ch)) {
        /* shadows=0: depth-only passes at another size (shadow maps) are skipped, and the map is cleared to far once
           a frame so it reads as fully lit */
        static u32 cleared_frame = ~0u;
        static Surf* cleared;
        if (cleared_frame != g_vk_present || cleared != zt) {
            cleared_frame = g_vk_present;
            cleared = zt;
            if (render_begin(rt, zt, 0, 0, r[0x56E] & 1, 0)) {
                VkClearAttachment a = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                                     (zt->f.stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)};
                a.clearValue.depthStencil = (VkClearDepthStencilValue){1.0f, 0};
                VkClearRect cr = {{{0, 0}, g_rp.ext}, 0, 1};
                rec_CmdClearAttachments(vk_cmd(), 1, &a, 1, &cr);
                target_written(zt);
            }
        }
        g_vk_skipped++;
        return;
    }

    /* the pipeline key and its vertex bindings: the kept ones when the targets' formats and the empty streams are the
       same too */
    TargetFormats tf;
    target_formats(rt, zt, n, &tf);
    if (kept && (memcmp(&tf, &g_draw.tf, sizeof tf) || attributes_empty(r, vs->si.in_mask) != g_draw.plan.empty))
        kept = 0;
    PipeKey made_k;
    VertexPlan made_pl;
    const PipeKey* k = &g_draw.k;
    const VertexPlan* pl = &g_draw.plan;
    if (!kept || g_draw.check) {
        if (!draw_key(ch, &sh, fs, rt, zt, n, &made_k, &made_pl)) {
            if (kept) {
                draw_differs("pipeline key");
                g_draw.sh.vs = NULL;
            }
            g_vk_skipped++;
            return;
        }
        if (kept && memcmp(&made_pl, pl, sizeof made_pl)) {
            draw_differs("vertex bindings");
            kept = 0;
        }
        k = &made_k;
        pl = &made_pl;
    }
    /* each binding's buffer: all of them prefetched first (vk_buf_prefetch), then looked up */
    VkBuffer vbuf[33];
    VkDeviceSize voff[33], vsize[33];
    struct {
        u64 gva, len;
        u32 b, base;
    } vq[33];
    u32 nvq = 0;
    /* each stream up to the last vertex or instance the draw reads, rather than to its limit */
    u64 last_vertex = k->nbind ? draw_last_vertex(ch, indexed, first, count, base_vertex) : ~0ull;
    u64 last_instance = (u64)base_instance + instances - 1;
    for (u32 v = 0; v < k->nbind; v++) {
        if (pl->key[v] == ~0u || pl->key[v] == ~1u) {
            /* the constants: staging that fails skips the draw (it once drew without the attributes) */
            if (!frame_consts(&vbuf[v], &voff[v], raster_scale)) {
                g_vk_skipped++;
                return;
            }
            int zero = pl->key[v] == ~1u;
            voff[v] += zero ? 48 : 16;
            vsize[v] = zero ? 16 : 32; /* typed zero, or float/integer defaults */
            continue;
        }
        u32 sbuf = pl->key[v] & 31;
        /* up to a power of two of the bytes it reads: the cache keeps a range by its length, and draws of a mesh's parts
           that end at different vertices then share a few ranges, not one each */
        u64 limit = stream_len(r, sbuf), len = limit;
        u64 last = k->bind[v].divisor ? last_instance : last_vertex;
        u64 gstride = pl->guest_stride[v] ? pl->guest_stride[v] : pl->stride[v];
        if (gstride && last < len / gstride) {
            u64 need = (last + 1) * gstride, up = 4096;
            while (up < need) up <<= 1;
            if (up < len) len = up;
        }
        if (len > (64u << 20)) len = 64u << 20;
        /* Overlapping records can need the attribute's tail beyond a stride,
           but never beyond the guest binding's inclusive limit. */
        len += pl->pad[v] < limit - len ? pl->pad[v] : limit - len;
        u32 base = pl->key[v] >> 5;
        if (base >= len) {
            if (!frame_consts(&vbuf[v], &voff[v], raster_scale)) {
                g_vk_skipped++;
                return;
            }
            voff[v] += 48;
            vsize[v] = 0;
            continue;
        }
        vq[nvq].gva = stream_start(r, sbuf);
        vq[nvq].len = len;
        vq[nvq].b = v;
        vq[nvq].base = base;
        nvq++;
    }
    for (u32 q = 0; q < nvq; q++) vk_buf_prefetch(vq[q].gva, vq[q].len, 0);
    for (u32 q = 0; q < nvq; q++) vk_buf_prefetch(vq[q].gva, vq[q].len, 1);
    for (u32 q = 0; q < nvq; q++) {
        u32 b = vq[q].b;
        if (pl->guest_stride[b]) {
            if (!copy_restrided(vq[q].gva + vq[q].base, vq[q].len - vq[q].base, pl->guest_stride[b], (u32)pl->stride[b],
                                &vbuf[b], &voff[b], &vsize[b])) {
                g_vk_skipped++;
                return;
            }
            continue;
        }
        /* vertex formats have components of up to 4 bytes, which each attribute's address must be a multiple of */
        if (!vk_buf_get_input(vq[q].gva, vq[q].len, 4, &vbuf[vq[q].b], &voff[vq[q].b])) {
            g_vk_skipped++;
            return;
        }
        voff[vq[q].b] += vq[q].base;
        vsize[vq[q].b] = vq[q].len - vq[q].base;
    }
    VkPrimitiveTopology topo = topology_vk(topology);

    /* descriptor sets (may record uploads and barriers, so before the pass opens) */
    cb_prefetch(ch, 0, vs, 1);
    if (fs) cb_prefetch(ch, 4, fs, 1);
    draw_indices_prefetch(ch, indexed, first, count, 1);
    VkDrawStorage storage;
    draw_storage_read(ch, &storage, vs, fs);
    int alias = storage.n[0] || storage.n[1] ? vk_buf_draw_has_alias(&storage) : 0;
    if (alias < 0) {
        g_vk_skipped++;
        return;
    }
    if ((storage.n[0] || storage.n[1]) && !vk_buf_draw_prepare(&storage)) {
        g_vk_skipped++;
        return;
    }
    int zt_ro = 0;
    u32 fb = 0;
    u64 scaled = 0;
    DrawImages images = {0};
    upscale_before(fs,fres,n?rt[0]:NULL);
    VkDescriptorSet vset = stage_set(ch, 0, vs, rt, zt, n, &zt_ro, NULL, NULL, NULL, &storage, &images, raster_scale);
    VkDescriptorSet fset =
        fs ? stage_set(ch, 4, fs, rt, zt, n, &zt_ro, &fb, &scaled, fres, &storage, &images, raster_scale) : VK_NULL_HANDLE;
    /* the sets of the stages between, at their stage's index (shader_vk_set) */
    between[0] = tcs;
    between[1] = tes;
    between[2] = gs;
    bset_ok = 1;
    for (b = 0; b < 3; b++) {
        bset[b] = VK_NULL_HANDLE;
        if (!between[b]) continue;
        if (between[b]->si.ngbuf || between[b]->si.nimg) { /* global memory and images are read in VS and FS */
            LOG_LIMIT(1, "vulkan: a tessellation or geometry program uses global memory or images (draw skipped)\n");
            bset_ok = 0;
            break;
        }
        bset[b] = stage_set(ch, b + 1, between[b], rt, zt, n, &zt_ro, NULL, NULL, NULL, &storage, NULL, raster_scale);
        if (!bset[b]) bset_ok = 0;
    }
    if (!vset || (fs && !fset) || !bset_ok) {
        draw_images_discard(&images);
        g_vk_skipped++;
        return;
    }
    /* indices, up to the end of the index buffer (0x5F4, inclusive). What is made into other indices on the CPU (quads,
       8-bit indices, another restart index) is capped */
    VkBuffer ibuf = VK_NULL_HANDLE;
    u64 ioff = 0;
    VkIndexType itype = VK_INDEX_TYPE_UINT32;
    u32 ifmt = indexed == 3 ? 2 : r[0x5F6], isz = ifmt == 0 ? 1 : ifmt == 1 ? 2 : 4;
    u64 ia = ((u64)r[0x5F2] << 32) | r[0x5F3], iend = ((u64)r[0x5F4] << 32) | r[0x5F5];
    if (indexed && indexed != 3 && iend >= ia && (u64)first + count > (iend - ia + 1) / isz) {
        u64 avail = (iend - ia + 1) / isz;
        LOG_LIMIT(1, "vulkan: a draw reads indices %u..%u of an index buffer of %llu (cut there)\n", first,
                  first + count - 1, (unsigned long long)avail);
        if (first >= avail) {
            draw_images_discard(&images);
            return;
        }
        count = (u32)(avail - first);
    }
    if ((topology == 7 || (indexed && ifmt == 0)) && count > (1u << 24)) {
        LOG_LIMIT(1, "vulkan: a draw of %u vertices converted on the CPU (cut to %u)\n", count, 1u << 24);
        count = 1u << 24;
    }
    /* Vulkan restarts at all ones of the index type, in strips and fans; the guest's restart index is 0x592 */
    int restart =
        r[0x591] && topo != VK_PRIMITIVE_TOPOLOGY_POINT_LIST && topo != VK_PRIMITIVE_TOPOLOGY_LINE_LIST &&
        topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topo != VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY &&
        topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY && topo != VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    u32 icount = count;
    int quads_indexed = 0;
    if (topology == 7) { /* quads -> two triangles each */
        u32 nq = count / 4;
        const u8* src = NULL;
        if (indexed)
            src = indexed == 3 ? (const u8*)(ch->inline_indices + first)
                               : gmem_ptr(ia + (u64)first * isz, (u64)count * isz);
        u32* idx = (u32*)vk_stage((u64)nq * 24 + 4, vk_stage_align(), &ibuf, &ioff);
        if (!idx) {
            draw_images_discard(&images);
            return;
        }
        /* guest indices are little-endian, the inline ones host words (already widened), and what the device reads
           is little-endian again */
        for (u32 q = 0; q < nq; q++) {
            u32 v[4];
            for (int j = 0; j < 4; j++) {
                u32 e = q * 4 + j;
                u8 g[4] = {0};
                if (!indexed)
                    v[j] = first + e;
                else if (indexed == 3)
                    v[j] = ch->inline_indices[first + e];
                else {
                    if (src)
                        memcpy(g, src + (u64)e * isz, isz);
                    else
                        gmem_read(ia + ((u64)first + e) * isz, g, isz);
                    v[j] = isz == 1 ? g[0] : isz == 2 ? snx_ld_le16(g) : snx_ld_le32(g);
                }
            }
            u32 t6[6] = {v[0], v[1], v[2], v[0], v[2], v[3]};
            snx_le32_array(t6, 6);
            memcpy(idx + q * 6, t6, 24);
        }
        icount = nq * 6;
        quads_indexed = indexed;
        indexed = 2; /* drawn indexed, with the first vertex in the indices */
    } else if (indexed) {
        if (indexed == 3) {
            u32* o = (u32*)vk_stage((u64)count * 4, vk_stage_align(), &ibuf, &ioff);
            if (!o) {
                draw_images_discard(&images);
                return;
            }
            memcpy(o, ch->inline_indices + first, (size_t)count * 4);
            if (restart && r[0x592] != 0xFFFFFFFFu)
                for (u32 j = 0; j < count; j++)
                    if (o[j] == r[0x592]) o[j] = 0xFFFFFFFFu;
            snx_le32_array(o, count); /* host words, read by the device little-endian */
            itype = VK_INDEX_TYPE_UINT32;
        } else if (ifmt == 0) { /* 8-bit indices need an extension: widen them */
            const u8* src = gmem_ptr(ia + first, count);
            u16* o = (u16*)vk_stage((u64)count * 2, vk_stage_align(), &ibuf, &ioff);
            if (!o) {
                draw_images_discard(&images);
                return;
            }
            for (u32 j = 0; j < count; j++) {
                u8 v = 0;
                if (src)
                    v = src[j];
                else
                    gmem_read(ia + first + j, &v, 1);
                /* the index type's bits, as below, little-endian for the device */
                o[j] = snx_le16(r[0x591] && v == (r[0x592] & 0xFF) ? 0xFFFF : v);
            }
            itype = VK_INDEX_TYPE_UINT16;
        } else {
            u32 ones = isz == 2 ? 0xFFFFu : 0xFFFFFFFFu, ri = r[0x592] & ones;
            u64 bytes = (u64)count * isz;
            if (restart && ri != ones && count <= (1u << 24)) { /* the guest's restart index becomes Vulkan's */
                u8* o = vk_stage(bytes, vk_stage_align(), &ibuf, &ioff);
                if (!o) {
                    draw_images_discard(&images);
                    return;
                }
                gmem_read(ia + (u64)first * isz, o, bytes);
                for (u32 j = 0; j < count; j++)
                    if (isz == 2 && snx_ld_le16(o + (u64)j * 2) == ri)
                        memset(o + (u64)j * 2, 0xFF, 2);
                    else if (isz == 4 && snx_ld_le32(o + (u64)j * 4) == ri)
                        memset(o + (u64)j * 4, 0xFF, 4);
            } else if (!vk_buf_get_input(ia + (u64)first * isz, bytes, isz, &ibuf, &ioff)) {
                draw_images_discard(&images);
                return;
            }
            itype = isz == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        }
    }

    /* the transform feedback buffers (0xE0 + 8 * i: enable, address, size, start offset), from the start offset on as
       Eden's UpdateTransformFeedbackBuffer binds them: a writable copy of guest memory each, which lands there and
       which later draws read on the GPU until then (vk_buf_copy_writable) */
    VkBuffer xbuf[4];
    VkDeviceSize xoff[4], xsize[4];
    u32 xbound = 0;
    for (int i = 0; i < 4 && xfb_on; i++) {
        const u32* t = &r[0xE0 + 8 * i];
        i32 size = (i32)t[3];
        u64 a = (((u64)(t[1] & 0xFF) << 32) | t[2]) + t[4];
        if (!t[0] || size <= 0 || (!r[0x1C1 + 4 * i] && !r[0x1C2 + 4 * i])) continue;
        if (!vk_buf_copy_writable(a, (u32)size, &xbuf[i], &xoff[i])) {
            draw_images_discard(&images);
            g_vk_skipped++;
            return;
        }
        xsize[i] = (u32)size;
        xbound |= 1u << i;
    }
    /* the feedback loop goes into the key last: stage_set found it */
    u32 feedback = fb && !g_vk.has_feedback_dynamic;
    if (k == &made_k)
        made_k.feedback = feedback;
    else if (k->feedback != feedback) {
        made_k = *k;
        made_k.feedback = feedback;
        k = &made_k;
        kept = 0;
    }
    if (kept && g_draw.check && memcmp(k, &g_draw.k, sizeof *k)) {
        draw_differs("pipeline key");
        kept = 0;
    }
    /* the kept pipeline, or the last draw's when its key is the same (draws in a row mostly are): one comparison of
       the key, where the table hashed all of it each draw. Pipelines and modules live as long as the run, so the
       handle stays good, a stand-in for one still being made too (vk_pipeline_get_deferred) */
    VkPipeline pipe;
    if (kept) {
        pipe = g_draw.pipe;
        if (g_draw.check) {
            VkPipeline made = vk_pipeline_get_deferred(k);
            if (vk_pipeline_ready(made) != vk_pipeline_ready(pipe)) {
                draw_differs("pipeline");
                kept = 0;
                pipe = made;
            }
        }
    } else if (g_draw.pipe && !memcmp(k, &g_draw.k, sizeof *k))
        pipe = g_draw.pipe;
    else
        pipe = vk_pipeline_get_deferred(k);
    if (!pipe) {
        draw_images_discard(&images);
        g_vk_skipped++;
        return;
    }
    /* A guest read while preparing descriptors can submit and land an older graphics output into memory these
       descriptors and staged inputs were read from. Rebuild them before recording the draw. */
    if (setup_frame != vk_submit_number()) {
        draw_images_discard(&images);
        if (++retries < 4) goto retry;
        g_vk_skipped++;
        return;
    }
    /* kept for the next draw */
    if (kept)
        g_vk_draw_kept++;
    else if (pipe) {
        g_draw.topology = draw_topo;
        g_draw.epoch = epoch;
		g_draw.shader_generation = g_vk.shader_generation;
        g_draw.sh = sh;
        g_draw.fkey = fk;
        g_draw.fs = fs;
        g_draw.tf = tf;
        if (pl != &g_draw.plan) g_draw.plan = *pl;
        if (k != &g_draw.k) g_draw.k = *k;
        g_draw.pipe = pipe;
        vertex_regs_keep(r, vs->si.in_mask);
        g_draw.pend &= ~DRAW_GROUPS;
    }
    /* the color slots the draw writes */
    u32 writes = 0;
    if (fs)
        for (u32 i = 0; i < n; i++)
            if (rt[i] && (k->blend[i].mask & ((fs->si.sph[18] >> (i * 4)) & 15))) writes |= 1u << i;
    /* a buffer that captured since its write pointer was loaded resumes from its counter, which the last capture's end
       wrote: the end is ordered before the begin outside a pass */
    u32 resume = xbound & ch->xfb_started & ~ch->xfb_reload;
    if (resume) {
        vk_render_end();
        VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                               .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT,
                               .srcAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
                               .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT,
                               .dstAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT};
        VkDependencyInfo di = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
        rec_CmdPipelineBarrier2(vk_cmd(), &di);
    }
    int zt_undefined = zt && zt->layout == VK_IMAGE_LAYOUT_UNDEFINED;
    if (!n && !zt ? !render_begin_empty()
                  : !render_begin(rt, zt, n, zt_ro, r[0x56E] & 1, fb) || !feedback_order(fb, writes)) {
        draw_images_discard(&images);
        return;
    }
    VkCommandBuffer cb = vk_cmd();
    /* only what differs from the previous draw's in this command buffer (g_dyn) */
    int fresh = g_dyn.cb != vk_cb_begun();
    if (fresh) {
        memset(&g_dyn, 0, sizeof g_dyn);
        g_dyn.cb = vk_cb_begun();
    }
    if (fresh || g_dyn.pipe != pipe) {
        g_dyn.pipe = pipe;
        rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    }
    if (g_vk.has_feedback_dynamic)
        DYN(fb, fb ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_NONE,
            rec_CmdSetAttachmentFeedbackLoopEnableEXT(cb, fb ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_NONE));
    VkDescriptorSet sets[2] = {vset, fset};
    VkPipelineLayout layout = vk_playout(k->arrays);
    if (g_dyn.layout != layout) { /* the sets bound through another layout are bound again */
        memset(g_dyn.sets, 0, sizeof g_dyn.sets);
        g_dyn.layout = layout;
    }
    if (fresh || g_dyn.sets[0] != vset || (fs && g_dyn.sets[1] != fset)) {
        g_dyn.sets[0] = vset;
        g_dyn.sets[1] = fs ? fset : VK_NULL_HANDLE;
        rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, fs ? 2 : 1, sets, 0, NULL);
    }
    for (b = 0; b < 3; b++)
        if (bset[b] && (fresh || g_dyn.sets[2 + b] != bset[b])) {
            g_dyn.sets[2 + b] = bset[b];
            rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (u32)b + 2, 1, &bset[b], 0, NULL);
        }
    if (k->nbind)
        rec_CmdBindVertexBuffers2(cb, 0, k->nbind, vbuf, voff, vsize, g_vk.has_dynamic_stride ? pl->stride : NULL);

    VkViewport vp = render_viewport_scaled(r, 0, raster_scale);
    u32 viewports = last->si.writes_viewport ? GUEST_VIEWPORTS : 1;
    if (viewports == 1 && !(vp.width > 0 && vp.height != 0)) {
        draw_images_discard(&images);
        return; /* its one viewport has no area: nothing is drawn, so nothing is recorded */
    }
    VkViewport viewport[GUEST_VIEWPORTS] = {{0}};
    VkRect2D scissor[GUEST_VIEWPORTS] = {{{0}}};
    u32 sub = g_vk.props.limits.viewportSubPixelBits < g_vk.props.limits.subPixelPrecisionBits
                  ? g_vk.props.limits.viewportSubPixelBits
                  : g_vk.props.limits.subPixelPrecisionBits; /* the points' nudge below */
    for (u32 i = 0; i < viewports; i++) {
        viewport[i] = render_viewport_scaled(r, i, raster_scale);
        /* A point a pixel wide covers the pixel its window position falls in on the console (OpenGL's aliased point
           rule), so one exactly on a pixel border goes to the pixel after it. Vulkan covers the pixels whose center is
           in the point's square, and leaves a center on its edge to the device: RADV takes the pixel before. One game
           projects a cube map onto spherical harmonics by drawing every sample as a point at the middle of an 8x8
           target, window (4, 4), and reads texel (4, 4) back on the CPU: its ambient light came out zero, and rooms
           lit by it black. Points move up the guest's window axes (down a lower-left window's y) by 1/128 pixel, too
           little to see, or by a step of the coarser of the device's viewport and rasterizer subpixel grids when that
           is larger (Vulkan only requires 4 bits of the rasterizer's and none of the viewport's): a smaller move
           rounds away. Not at all on a viewport without subpixel bits. Only in the modes that game draws them in,
           the OpenGL point center (0x597) and half-integer pixel centers (0x649): the others' rules aren't known */
        if (topo == VK_PRIMITIVE_TOPOLOGY_POINT_LIST && !gs && !tes && !r[0x597] && !r[0x649] && sub) {
            float e = ((float)(1.0f / (float)(1u << (sub < 7 ? sub : 7))) * raster_scale);
            viewport[i].x += e;
            viewport[i].y += (r[0x4EB] & 1) ? -e : e;
        }
        int area = viewport[i].width > 0 && viewport[i].height != 0;
        if (!area) /* An empty guest viewport clips everything. Vulkan still needs a valid transform. */
            viewport[i] = (VkViewport){0, 0, 1, 1, 0, 1};
        VkRect2D sc = {{0, 0}, area ? g_rp.ext : (VkExtent2D){0, 0}};
        if (area && (r[0x380 + i * 4] & 1)) {
            u32 xs = r[0x381 + i * 4], ys = r[0x382 + i * 4];
            i32 x0 = (i32)((float)(xs & 0xFFFF) * raster_scale), x1 = (i32)((float)(xs >> 16) * raster_scale);
            i32 y0 = (i32)((float)(ys & 0xFFFF) * raster_scale), y1 = (i32)((float)(ys >> 16) * raster_scale);
            if (r[0x4EB] & 1) {
                i32 clip_height = (i32)((float)((float)(r[0x3FE] >> 16)) * raster_scale);
                i32 old_y0 = y0;
                y0 = clip_height - y1;
                y1 = clip_height - old_y0;
            }
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > (i32)g_rp.ext.width) x1 = (i32)g_rp.ext.width;
            if (y1 > (i32)g_rp.ext.height) y1 = (i32)g_rp.ext.height;
            sc = x1 > x0 && y1 > y0 ? (VkRect2D){{x0, y0}, {(u32)(x1 - x0), (u32)(y1 - y0)}}
                                   : (VkRect2D){{0, 0}, {0, 0}};
        }
        scissor[i] = sc;
    }
    if(tready && tlast+1==g_vk_present && zt && zt->gva==tzaddr && zt->sw==1920 && zt->sh==1080 && r[0x4B3] && viewports==1 && fabsf(viewport[0].width-1920)<1 && fabsf(fabsf(viewport[0].height)-1080)<1){
        float j[2];tjitter(j);viewport[0].x+=j[0];viewport[0].y+=j[1];tjitframe=g_vk_present;tjitvalid=1;
    }
    static u32 inspectedPresent=~0u;
    if(g_vk_present%90==0 && inspectedPresent!=g_vk_present && zt && n>=2 && (writes&2) && rt[1] &&
       rt[1]->f.vk==VK_FORMAT_R8G8B8A8_SRGB && getenv("ARCEUS_QA_JITTER")){
        inspectedPresent=g_vk_present;
        LOG("arceus-qa: geometry present=%u lastResolve=%u ready=%u depth=%llx previousDepth=%llx test=%u viewports=%u extent=%ux%u viewport=%g,%g\n",
            g_vk_present,tlast,tready,(unsigned long long)zt->gva,(unsigned long long)tzaddr,r[0x4B3],viewports,zt->sw,zt->sh,viewport[0].width,viewport[0].height);
    }
    resolution_trace(viewport[0], scissor[0], fs, fres, count, code + r[0x851]);
    reflection_record(ch, fs, fres);
    material_trace(viewport[0], scissor[0], vs, fs, fres, count, scaled, r);
    if (fresh || g_dyn.viewports != viewports || memcmp(g_dyn.vp, viewport, viewports * sizeof *viewport)) {
        memcpy(g_dyn.vp, viewport, viewports * sizeof *viewport);
        rec_CmdSetViewportWithCount(cb, viewports, viewport);
    }
    if (fresh || g_dyn.viewports != viewports || memcmp(g_dyn.sc, scissor, viewports * sizeof *scissor)) {
        memcpy(g_dyn.sc, scissor, viewports * sizeof *scissor);
        rec_CmdSetScissorWithCount(cb, viewports, scissor);
    }
    g_dyn.viewports = viewports;
    /* Triangle facing has its own window-origin control, independent of viewport scale and swizzle. */
    {
        VkCullModeFlags cull = !r[0x646]           ? VK_CULL_MODE_NONE
                               : r[0x648] == 0x404 ? VK_CULL_MODE_FRONT_BIT
                               : r[0x648] == 0x408 ? VK_CULL_MODE_FRONT_AND_BACK
                                                   : VK_CULL_MODE_BACK_BIT;
        VkFrontFace front = front_face_at_viewport(r, vp.height);
        u32 prim_restart = restart && (indexed == 1 || indexed == 3);
        DYN(cull, cull, rec_CmdSetCullMode(cb, cull));
        DYN(front, front, rec_CmdSetFrontFace(cb, front));
        DYN(topo, topo, rec_CmdSetPrimitiveTopology(cb, topo));
        DYN(restart, prim_restart, rec_CmdSetPrimitiveRestartEnable(cb, prim_restart));
        /* RASTERIZE_ENABLE (0xDF) off discards the primitives, after a capture or the vertex program's stores. One game
           draws points whose vertex program only stores to memory and writes no position: rasterized, they all lit
           the pixel in the middle of the screen. A channel starts with it on (reset_3d, as Eden's register defaults)
           (1 off, 2 on: 0 is a new command buffer's) */
        u32 discard = !r[0xDF] ? 2 : 1;
        DYN(discard, discard, rec_CmdSetRasterizerDiscardEnable(cb, discard == 2));
    }
    {
        float lw; /* smooth lines (0x55C) have a width of their own */
        memcpy(&lw, &r[r[0x55C] ? 0x4EC : 0x4ED], 4);
        lw = g_vk.feat.wideLines && lw > 0 && lw < 64 ? lw : 1.0f;
        DYNV(lw, lw, rec_CmdSetLineWidth(cb, lw));
    }
    /* depth bias: enabled per polygon mode (0x370 points, 0x371 lines, 0x372 filled), clamped by 0x61F. The units
       are passed as they are (Ryujinx halves them: a run decides) */
    {
        u32 bias_on = r[k->polygon == VK_POLYGON_MODE_POINT  ? 0x370
                        : k->polygon == VK_POLYGON_MODE_LINE ? 0x371
                                                            : 0x372] != 0;
        DYN(bias_on, bias_on, rec_CmdSetDepthBiasEnable(cb, bias_on));
    }
    {
        float fac, un, clamp;
        memcpy(&fac, &r[0x55B], 4);
        memcpy(&un, &r[0x56F], 4);
        memcpy(&clamp, &r[0x61F], 4);
        float bias[3] = {un, g_vk.feat.depthBiasClamp ? clamp : 0.0f, fac};
        DYNV(bias, bias, rec_CmdSetDepthBias(cb, bias[0], bias[1], bias[2]));
    }
    if (!g_vk.feat.depthClamp && vk_render_depth_clamp(r))
        LOG_LIMIT(1, "vulkan: depth clamp requested but unavailable on this device\n");
    {
        float bc[4];
        memcpy(bc, &r[0x4C7], 16);
        DYNV(bc, bc, rec_CmdSetBlendConstants(cb, bc));
    }
    /* depth and stencil */
    {
        u32 dt = zt && r[0x4B3], dw = zt && r[0x4BA];
        VkCompareOp dcmp = cmp_op(r[0x4C3]);
        DYN(dt, dt, rec_CmdSetDepthTestEnable(cb, dt));
        DYN(dw, dw, rec_CmdSetDepthWriteEnable(cb, dw));
        DYN(dcmp, dcmp, rec_CmdSetDepthCompareOp(cb, dcmp));
        DYN(dbounds, 1, (rec_CmdSetDepthBoundsTestEnable(cb, VK_FALSE), rec_CmdSetDepthBounds(cb, 0.0f, 1.0f)));
    }
    int stencil = zt && zt->f.stencil && r[0x4E0];
    DYN(st, (u32)stencil, rec_CmdSetStencilTestEnable(cb, stencil));
    /* GL order in the registers: fail, depth fail, pass */
    int two = r[0x565] != 0;
    {
        /* per face: fail, depth fail and pass operations, the compare op, reference, compare and write masks */
        u32 sf[7] = {stencil_op(r[0x4E1]),
                     stencil_op(r[0x4E3]),
                     stencil_op(r[0x4E2]),
                     cmp_op(r[0x4E4]),
                     r[0x4E5],
                     r[0x4E6],
                     r[0x4E7]};
        u32 sb[7] = {stencil_op(r[two ? 0x566 : 0x4E1]),
                     stencil_op(r[two ? 0x568 : 0x4E3]),
                     stencil_op(r[two ? 0x567 : 0x4E2]),
                     cmp_op(r[two ? 0x569 : 0x4E4]),
                     r[two ? 0x3D5 : 0x4E5],
                     r[two ? 0x3D7 : 0x4E6],
                     r[two ? 0x3D6 : 0x4E7]};
        DYNV(sfront, sf,
             (rec_CmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_BIT, sf[0], sf[1], sf[2], sf[3]),
              rec_CmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_BIT, sf[4]),
              rec_CmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_BIT, sf[5]),
              rec_CmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_BIT, sf[6])));
        DYNV(sback, sb,
             (rec_CmdSetStencilOp(cb, VK_STENCIL_FACE_BACK_BIT, sb[0], sb[1], sb[2], sb[3]),
              rec_CmdSetStencilReference(cb, VK_STENCIL_FACE_BACK_BIT, sb[4]),
              rec_CmdSetStencilCompareMask(cb, VK_STENCIL_FACE_BACK_BIT, sb[5]),
              rec_CmdSetStencilWriteMask(cb, VK_STENCIL_FACE_BACK_BIT, sb[6])));
    }
    /* push constants: the depth mode (snx_vp.y) for the vertex stage, the alpha test (0x4BB on, 0x4C4 reference,
       0x4C5 function) and the point sprite's T flip for the fragment stage. PointCoord's T is 0 at row 0, which is a
       lower-left window's bottom (0x4EB bit 0): it flips when the sprite's origin (0x581 bit 2, 1 the top) is the
       other side. Then the fragment stage's scaled textures (stage_set) */
    float pc[12] = {0, (r[0x35F] & 1) ? 0.0f : 1.0f, 0, 0};
    memcpy(&pc[0], &r[0x50E], sizeof(u32));
    {
        u32 fn = r[0x4C5];
        int f = fn >= 0x200 && fn <= 0x207 ? (int)(fn - 0x200) : fn >= 1 && fn <= 8 ? (int)fn - 1 : 7;
        pc[4] = (r[0x4BB] & 1) ? 1.0f : 0.0f;
        pc[5] = (float)f;
        memcpy(&pc[6], &r[0x4C4], 4);
        pc[7] = ((r[0x581] >> 2) & 1) == (r[0x4EB] & 1) ? 1.0f : 0.0f;
    }
    u32 scaled_words[2] = {(u32)scaled, (u32)(scaled >> 32)};
    memcpy(&pc[8], scaled_words, sizeof scaled_words);
    rec_CmdPushConstants(cb, g_vk_playout, VK_PUSH_STAGES, 0, sizeof pc, pc);

    vk_query_draw(cb, ch->id, r[0x574] | (raster_scale>1.1f?0x80000000u:0)); /* counted by the channel's samples-passed counter when it runs */
    VkBuffer counters[4] = {g_xfb.buf, g_xfb.buf, g_xfb.buf, g_xfb.buf};
    VkDeviceSize counter_offs[4] = {0, 4, 8, 12};
    /* the buffers from 0 to the last one bound, as begin and end take the counters of: one between that isn't bound
       gets another's memory for 4 bytes, which nothing captures into (its layout has no words) */
    u32 xcount = xbound & 8 ? 4 : xbound & 4 ? 3 : xbound & 2 ? 2 : xbound & 1 ? 1 : 0;
    if (xcount) {
        VkBuffer from[4];
        int first = xbound & 1 ? 0 : xbound & 2 ? 1 : xbound & 4 ? 2 : 3;
        for (u32 i = 0; i < xcount; i++) {
            if (!(xbound >> i & 1)) {
                xbuf[i] = xbuf[first];
                xoff[i] = xoff[first];
                xsize[i] = 4;
            }
            from[i] = resume >> i & 1 ? g_xfb.buf : VK_NULL_HANDLE; /* none: from the bound offset */
        }
        rec_CmdBindTransformFeedbackBuffersEXT(cb, 0, xcount, xbuf, xoff, xsize);
        rec_CmdBeginTransformFeedbackEXT(cb, 0, xcount, from, counter_offs);
    }
    u64 captureCount=(u64)(indexed?icount:count)*instances;
    int capturing=!xfb_on && !tcs && !gs && !tes && topology==4 && zt && zt->sw==1920 && zt->sh==1080 &&
        n>=2 && (writes&2) && rt[1] && rt[1]->f.vk==VK_FORMAT_R8G8B8A8_SRGB &&
        rt[1]->gva==lightingAlbedo.address && rt[1]->img==lightingAlbedo.image &&
        captureCount<=worldMaxVertices && g_vk.has_xfb && world_capture_begin(cb,(u32)captureCount,rt[1],ch,fs,fres);
    pred_begin(cb, ch->render_pred);
    if (indexed) {
        rec_CmdBindIndexBuffer(cb, ibuf, ioff, itype);
        /* quads made from guest indices keep the base vertex, quads made from vertex numbers have it in them */
        rec_CmdDrawIndexed(cb, icount, instances, 0, indexed == 2 && !quads_indexed ? 0 : (i32)base_vertex,
                         base_instance);
    } else
        rec_CmdDraw(cb, count, instances, first, base_instance);
    pred_end(cb, ch->render_pred);
    if(capturing)rec_CmdEndTransformFeedbackEXT(cb,0,0,NULL,NULL);
    if (xcount) {
        rec_CmdEndTransformFeedbackEXT(cb, 0, xcount, counters, counter_offs);
        ch->xfb_started = (u8)((ch->xfb_started | xbound) & 15);
        ch->xfb_reload = (u8)(ch->xfb_reload & ~xbound);
        vk_render_end(); /* the next capture's begin waits for this end outside a pass, and readers for its writes */
    }
    vk_buf_writable_commit();
    draw_images_commit(&images);
    feedback_done(fb, writes);
    if (fb) g_vk_fb_draws++;
    for (u32 i = 0; i < n; i++)
        if (writes >> i & 1) target_written(g_rp.rt[i]);
    if (zt && !zt_ro && (r[0x4BA] || (r[0x4E0] && ((r[0x4E7] & 0xFF) || (r[0x565] && (r[0x3D6] & 0xFF))))) &&
        zt_written(zt, ch->render_pred, zt_undefined))
        target_written(zt);
    if(n && (writes&1))taa_after(ch,fs,fres,rt[0]);
    if(fs && n && (writes&1) && (g_vk_present==600 || g_vk_present==1440 || g_vk_present==1960) && getenv("ARCEUS_QA_OVERLAYS")){
        Surf *color=rt[0];
        LOG("world-pass: f=%u draw=%u fs=%llx rt=%llx w=%u h=%u seq=%llu cap=%llx capFrame=%u resolve=%u z=%llx zWrite=%u blend=%u src=%u dst=%u op=%u rgba=%u targets=%u indices=%u instances=%u\n",
            g_vk_present,(u32)g_vk_drawn,(unsigned long long)fs->hash,(unsigned long long)color->gva,color->sw,color->sh,
            (unsigned long long)color->wseq,(unsigned long long)cap_color,cap_frame,tlast,
            (unsigned long long)(zt?zt->gva:0),!!(zt && r[0x4BA]),k->blend[0].en,k->blend[0].sc,k->blend[0].dc,
            k->blend[0].cop,k->blend[0].mask,n,(u32)captureCount,instances);
    }

    g_vk_drawn++;
}

/* DRAW_TEXTURE (3D 0x420-0x42B, drawn when 0x42B is written): a texture's rectangle blitted onto color target 0, as
   Eden's DrawManager::DrawTexture reads the registers: the destination in 20.12 fixed point, the source's start the
   same and its steps per destination pixel in 32.32, and y from the bottom in a lower-left window (0x4EB bit 0, the
   height from the surface clip, 0x3FE). The sampler's magnification filter filters it */
static int format_has(VkFormat f, VkFormatFeatureFlags need) {
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
    return (p.optimalTilingFeatures & need) == need;
}
void vk_render_draw_texture(GpuChannel* ch) {
    u32* r = ch->r3d;
    float x0 = (float)(i32)r[0x420] / 4096.f, y0 = (float)(i32)r[0x421] / 4096.f;
    float w = (float)(i32)r[0x422] / 4096.f, h = (float)(i32)r[0x423] / 4096.f;
    double du = (double)(i64)((u64)r[0x425] << 32 | r[0x424]) / 4294967296.0;
    double dv = (double)(i64)((u64)r[0x427] << 32 | r[0x426]) / 4294967296.0;
    float sx0 = (float)(i32)r[0x42A] / 4096.f, sy0 = (float)(i32)r[0x42B] / 4096.f;
    if (r[0x4EB] & 1) y0 = (float)(r[0x3FE] >> 16) - y0;
    float sx1 = sx0 + (float)(du * w), sy1 = sy0 + (float)(dv * h);
    TexRes x;
    tex_resolve(ch, (r[0x428] & 0xFFF) << 20 | (r[0x429] & 0xFFFFF), &x);
    Surf *rt[8], *zt, *t = x.t;
    u32 n;
    read_targets(ch, rt, &zt, &n);
    if (!t || !n || !rt[0]) {
        LOG_LIMIT(1, "vulkan: a draw texture without its texture or a color target (not drawn)\n");
        return;
    }
    Surf* d = rt[0];
    if (t->f.depth || d->f.depth || t->f.is_int != d->f.is_int ||
        !format_has(t->f.vk, VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !format_has(d->f.vk, VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        LOG_LIMIT(1, "vulkan: a draw texture between formats %d and %d can't be blitted (not drawn)\n", (int)t->f.vk,
                  (int)d->f.vk);
        return;
    }
    /* in the images' pixels: a target is scaled, and a texture made from one is too */
    float dsx = (float)d->sw/d->w;
    float dsy = dsx;
    float tsx = (float)t->sw / (float)t->w, tsy = (float)t->sh / (float)t->h;
    VkImageBlit b = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{(i32)(sx0 * tsx), (i32)(sy0 * tsy), 0}, {(i32)(sx1 * tsx), (i32)(sy1 * tsy), 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffsets = {{(i32)((x0) * dsx), (i32)((y0) * dsy), 0}, {(i32)((x0 + w) * dsx), (i32)((y0 + h) * dsy), 1}}};
    int linear =
        (x.tsc[1] & 3) == 2 && !t->f.is_int && format_has(t->f.vk, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    vk_render_end(); /* blits happen outside rendering passes */
    vk_rt_note_read(t);
    if (t->layout == VK_IMAGE_LAYOUT_UNDEFINED) return;
    VkCommandBuffer cb = vk_cmd();
    vk_surf_use(cb, t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
    vk_surf_use(cb, d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
    rec_CmdBlitImage(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &b, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    vk_surf_wrote(d);
    vk_rt_drawn(d);
    vk_surf_touch(d);
}
