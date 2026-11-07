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
