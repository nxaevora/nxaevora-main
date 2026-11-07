/* Called only after show() submitted its swapchain-acquire wait and queued
   presentation. Refreshes of the runtime menu do not consume this request. */
static void material_snapshot_complete(Surf* src) {
    if (!g_material_snapshot[0] || !src || src != g_last || !g_last_w || !g_last_h) return;
    char prefix[sizeof g_material_snapshot], final_prefix[1100], pair_path[1100], final_path[1100];
    memcpy(prefix, g_material_snapshot, sizeof prefix);
    g_material_snapshot[0] = 0; /* one shot, including failures */
    int a = snprintf(final_prefix, sizeof final_prefix, "%s_final", prefix);
    int b = snprintf(pair_path, sizeof pair_path, "%s_pair.tsv", prefix);
    int c = snprintf(final_path, sizeof final_path, "%s_final.ppm", prefix);
    if (a < 0 || (size_t)a >= sizeof final_prefix || b < 0 || (size_t)b >= sizeof pair_path ||
        c < 0 || (size_t)c >= sizeof final_path) {
        LOG("material-snapshot: paired output path is too long\n");
        return;
    }
    /* Raw attachment copies finish before surf_save's temporary image is made;
       no temporary-image allocation can evict a gathered attachment pointer. */
    vk_surf_diagnostic_capture(prefix);
    u64 hash = surf_save(src, g_last_x, g_last_y, g_last_w, g_last_h, g_last_transform, final_prefix);
    int final_ok = 0;
    FILE* check = fopen(final_path, "rb");
    if (check) {
        char header[80];
        int bytes = snprintf(header, sizeof header, "P6\n%u %u\n255\n", g_last_w, g_last_h);
        if (bytes > 0 && !fseek(check, 0, SEEK_END)) {
            long length = ftell(check);
            final_ok = length >= 0 && (u64)length == (u64)bytes + (u64)g_last_w * g_last_h * 3;
        }
        fclose(check);
    }
    FILE* pair = fopen(pair_path, "wb");
    int ok = pair != NULL;
    if (pair) {
        ok = fprintf(pair,
            "# raw_orientation=guest-image-order final_orientation=crop-and-flip_src-transform\n"
            "# final_scope=guest-final-surface-excludes-runtime-postfx-letterbox-and-UI\n"
            "# ordinary-F3-screenshot-is-earlier-and-not-the-attachment-pair\n"
            "present\tfinal_file\tfinal_complete\tfinal_hash\tgva\tfkey\tvkformat\tguest_w\tguest_h"
            "\timage_w\timage_h\tlayers\tcrop_x\tcrop_y\tcrop_w\tcrop_h\ttransform\n"
            "%llu\t%s\t%d\t%llx\t%llx\t%x\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
            (unsigned long long)g_vk_present, final_path, final_ok, (unsigned long long)hash,
            (unsigned long long)src->gva, src->fkey, (u32)src->f.vk, src->w, src->h, src->sw, src->sh,
            src->layers, g_last_x, g_last_y, g_last_w, g_last_h, g_last_transform) >= 0;
        if (fclose(pair)) ok = 0;
    }
    if (!ok) LOG("material-snapshot: incomplete pairing metadata %s\n", pair_path);
    LOG("material-snapshot: paired present=%llu final_complete=%d final=%s metadata=%s\n",
        (unsigned long long)g_vk_present, final_ok, final_path, pair_path);
}
