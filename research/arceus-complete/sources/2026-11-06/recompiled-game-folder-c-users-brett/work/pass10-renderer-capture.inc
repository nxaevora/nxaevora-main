/* Optional local diagnosis: F3 still saves its ordinary screenshot immediately.
   A separate request records raw buffers and a matching final image at the next
   completed game present. All access is under vk_lock; no timer or gameplay input. */
static char g_material_snapshot[1024];
static u32 g_material_snapshot_requests;
static void material_snapshot_complete(Surf* src);

static void material_snapshot_request(const char* path) {
    const char* enabled = getenv("SNX_MATERIAL_SNAPSHOT");
    if (!enabled || strcmp(enabled, "1") || !path || !*path || !g_last) return;
    if (g_material_snapshot[0]) {
        LOG("material-snapshot: earlier request still pending; ordinary screenshot saved\n");
        return;
    }
    if (g_material_snapshot_requests >= 4) {
        LOG("material-snapshot: four-capture limit reached; ordinary screenshots remain available\n");
        return;
    }
    if (strpbrk(path, "\t\r\n")) return;
    int n = snprintf(g_material_snapshot, sizeof g_material_snapshot, "%s_buffers_%u",
                     path, g_material_snapshot_requests + 1);
    if (n < 0 || (size_t)n >= sizeof g_material_snapshot) {
        g_material_snapshot[0] = 0;
        LOG("material-snapshot: screenshot path is too long\n");
        return;
    }
    g_material_snapshot_requests++;
    LOG("material-snapshot: queued %s for next completed game present; ordinary screenshot is unpaired\n",
        g_material_snapshot);
}
