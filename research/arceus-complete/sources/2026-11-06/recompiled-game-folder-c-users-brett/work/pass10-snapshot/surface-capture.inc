/* Append to surface.c: optional, local raw attachment snapshots after a completed game present.
   The caller holds vk_lock and has already submitted the swapchain-acquire wait. */
#define DIAG_SURF_MAX 96u
#define DIAG_SURF_IMAGE_BYTES (64ull << 20)
#define DIAG_SURF_TOTAL_BYTES (384ull << 20)
#define DIAG_SURF_PATH 1100u

static u32 diagnostic_pixel_bytes(const Surf* s, VkImageAspectFlags* aspect) {
    *aspect = s->f.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (s->f.depth) {
        switch (s->f.vk) {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D16_UNORM_S8_UINT: return 2;
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return 4;
            default: return 0;
        }
    }
    /* Vulkan storage bytes, not f.bpp: converted guest textures can differ. No
       native compressed/packed-YUV images or guessed bytes-per-pixel. */
    switch (s->f.vk) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_UINT: return 1;
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 0;
    }
}

static int diagnostic_surface_size(const Surf* s, u32 present, u32* bpp,
                                   VkImageAspectFlags* aspect, u64* size) {
    if (!s->img || (!s->is_rt && !s->took_drawn) ||
        (u32)(present - (u32)s->last_used) > 2 || !s->sw || !s->sh || !s->layers ||
        s->layout == VK_IMAGE_LAYOUT_UNDEFINED || s->pred_undefined || s->volume ||
        s->type == VK_IMAGE_VIEW_TYPE_3D || s->depth3d > 1) return 0;
    if (s->type != VK_IMAGE_VIEW_TYPE_2D && s->type != VK_IMAGE_VIEW_TYPE_2D_ARRAY &&
        s->type != VK_IMAGE_VIEW_TYPE_CUBE && s->type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) return 0;
    *bpp = diagnostic_pixel_bytes(s, aspect);
    if (!*bpp) return 0;
    /* Bound each multiply before doing it, including corrupt/unexpected extents. */
    u64 n = *bpp;
    if (s->sw > DIAG_SURF_IMAGE_BYTES / n) return 0;
    n *= s->sw;
    if (s->sh > DIAG_SURF_IMAGE_BYTES / n) return 0;
    n *= s->sh;
    if (s->layers > DIAG_SURF_IMAGE_BYTES / n) return 0;
    *size = n * s->layers;
    return 1;
}

void vk_surf_diagnostic_capture(const char* prefix) {
    const char* enabled = getenv("SNX_MATERIAL_SNAPSHOT");
    if (!enabled || strcmp(enabled, "1") || !prefix || !*prefix) return;
    char manifest_path[DIAG_SURF_PATH], file_path[DIAG_SURF_PATH];
    /* TSV paths cannot contain separators; reject rather than produce ambiguous metadata. */
    if (strpbrk(prefix, "\t\r\n")) {
        LOG("material-snapshot: invalid control character in prefix\n");
        return;
    }
    int written = snprintf(manifest_path, sizeof manifest_path, "%s_attachments.tsv", prefix);
    if (written < 0 || (size_t)written >= sizeof manifest_path) {
        LOG("material-snapshot: prefix is too long\n");
        return;
    }
    FILE* manifest = fopen(manifest_path, "wb");
    if (!manifest) {
        LOG("material-snapshot: cannot open %s\n", manifest_path);
        return;
    }
    /* Safe only after present's acquire-wait submission. Finish/land prior work
       before gathering pointers; recycle the new command slot before that too.
       No cache allocation, trim or eviction is performed after gathering. */
    vk_finish();
    vk_render_end();
    vk_cmd();
    const u32 present = g_vk_present;
    struct {
        Surf* surf;
        u64 bytes;
        u32 bpp;
        VkImageAspectFlags aspect;
    } selected[DIAG_SURF_MAX];
    u32 n = 0, eligible = 0, bounded_out = 0, succeeded = 0, failures = 0;
    u64 total = 0, maximum = 0;
    const int lists[] = {VK_LRU_RT, VK_LRU_RT + LRU_AGED, VK_LRU_TEX, VK_LRU_TEX + LRU_AGED};
    /* Newest first. Gather without changing recency or calling allocation/flush
       helpers while walking the LRU. Each Surf occurs in exactly one list. */
    for (u32 l = 0; l < sizeof lists / sizeof lists[0]; l++)
        for (Surf* s = g_lru[lists[l]].tail; s; s = s->lru_prev) {
            u32 bpp; u64 size; VkImageAspectFlags aspect;
            if (!diagnostic_surface_size(s, present, &bpp, &aspect, &size)) continue;
            eligible++;
            if (n == DIAG_SURF_MAX || size > DIAG_SURF_TOTAL_BYTES - total) {
                bounded_out++;
                continue;
            }
            selected[n].surf = s; selected[n].bytes = size;
            selected[n].bpp = bpp; selected[n].aspect = aspect;
            n++; total += size;
            if (size > maximum) maximum = size;
        }
    int manifest_ok = fprintf(manifest,
        "# material-snapshot v1 present=%u selected=%u eligible=%u bounded_out=%u total_bytes=%llu "
        "scope=raw-base-mip-all-array-layers-depth-only-no-stencil\n"
        "index\tstatus\tfile\tpresent\tgva\tfkey\tvkformat\taspect\tbpp\tguest_w\tguest_h\timage_w\timage_h"
        "\tlayers\tlevels\tmip\tbytes\trow_bytes\tlayer_bytes\tis_rt\ttook_drawn\twgen\tdrawn_at\tlast_used"
        "\toriginal_layout\twseq\n",
        present, n, eligible, bounded_out, (unsigned long long)total) >= 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    /* One bounded host buffer reused serially. This helper directly allocates
       through VMA, without vk_mem_trim. No full-capture-sized GPU buffer. */
    const u8* pixels = n && manifest_ok ? vk_readback_buffer(maximum, &buffer) : NULL;
    if (n && manifest_ok && !pixels) {
        LOG("material-snapshot: readback allocation of %llu bytes failed\n", (unsigned long long)maximum);
        manifest_ok = fprintf(manifest, "# readback-allocation-failed bytes=%llu\n",
                              (unsigned long long)maximum) >= 0;
        failures++;
    }
    for (u32 i = 0; i < n && pixels && manifest_ok; i++) {
        Surf* s = selected[i].surf;
        u64 size = selected[i].bytes;
        written = snprintf(file_path, sizeof file_path, "%s_%02u.bin", prefix, i);
        if (written < 0 || (size_t)written >= sizeof file_path) {
            LOG("material-snapshot: attachment path is too long\n");
            failures++;
            break;
        }
        FILE* out = fopen(file_path, "wb");
        if (!out) {
            LOG("material-snapshot: cannot open %s\n", file_path);
            failures++;
        }
        int ok = out != NULL;
        VkImageLayout layout = s->layout;
        VkPipelineStageFlags2 stage = s->stage;
        VkAccessFlags2 access = s->access;
        if (out) {
            VkCommandBuffer cb = vk_cmd();
            vk_surf_use(cb, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy copy = {.bufferOffset = 0,
                .imageSubresource = {selected[i].aspect, 0, 0, s->layers},
                .imageExtent = {s->sw, s->sh, 1}};
            rec_CmdCopyImageToBuffer(cb, s->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
            /* Restore both the Vulkan layout and the renderer's tracked use. */
            vk_surf_use(cb, s, layout, stage ? stage : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, access);
            VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT};
            VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
            rec_CmdPipelineBarrier2(cb, &di);
            vk_finish();
            /* Same-layout read/read use deliberately unions stages in
               vk_surf_use. The copy is now completed, so discard only its
               extra tracking and restore the caller's exact original state. */
            s->layout = layout;
            s->stage = stage;
            s->access = access;
            vk_readback_ready();
            ok = fwrite(pixels, 1, (size_t)size, out) == (size_t)size;
            if (fclose(out)) ok = 0;
            if (!ok) {
                LOG("material-snapshot: incomplete write of %s\n", file_path);
                failures++;
            } else succeeded++;
        }
        manifest_ok = fprintf(manifest,
            "%u\t%s\t%s\t%u\t%llx\t%x\t%u\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t0\t%llu"
            "\t%llu\t%llu\t%d\t%d\t%u\t%u\t%llu\t%u\t%llu\n",
            i, ok ? "ok" : "write-failed", file_path, present, (unsigned long long)s->gva, s->fkey,
            (u32)s->f.vk, s->f.depth ? "depth" : "color", selected[i].bpp, s->w, s->h, s->sw, s->sh,
            s->layers, s->levels, (unsigned long long)size, (unsigned long long)s->sw * selected[i].bpp,
            (unsigned long long)s->sw * s->sh * selected[i].bpp, s->is_rt, s->took_drawn, s->wgen,
            s->drawn_at, (unsigned long long)s->last_used, (u32)layout, (unsigned long long)s->wseq) >= 0;
    }
    if (manifest_ok)
        manifest_ok = fprintf(manifest, "# completed=%u failures=%u\n", succeeded, failures) >= 0;
    if (fclose(manifest)) manifest_ok = 0;
    if (!manifest_ok) LOG("material-snapshot: incomplete manifest %s\n", manifest_path);
    LOG("material-snapshot: present=%u wrote=%u selected=%u eligible=%u bounded_out=%u failures=%u manifest=%s\n",
        present, succeeded, n, eligible, bounded_out, failures, manifest_path);
}
