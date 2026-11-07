from pathlib import Path
import shutil

root = Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/gpu/vk')
for name in ('render.c', 'renderer.c'):
    shutil.copy2(root/name, Path('work')/(name+'.before-resolution-trace'))
p = root/'render.c'
s = p.read_text()
marker = 'static void render_draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex,\n                        u32 base_instance, u32 instances) {'
assert s.count(marker) == 1
helper = r'''/* Opt-in resolution diagnosis. Log each distinct viewport/target combination once;
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

'''
s=s.replace(marker,helper+marker)
anchor='    if (fresh || g_dyn.viewports != viewports || memcmp(g_dyn.vp, viewport, viewports * sizeof *viewport)) {'
assert s.count(anchor)==1
s=s.replace(anchor,'    resolution_trace(viewport[0], scissor[0], fs, fres, count, code + r[0x851]);\n'+anchor)
p.write_text(s)
p=root/'renderer.c'; s=p.read_text()
anchor='    Surf* src = NULL;\n    if (w && h && w <= g_vk.props.limits.maxImageDimension2D'
assert s.count(anchor)==1
s=s.replace(anchor,'    int cpu_fallback = 0;\n    Surf* src = NULL;\n    if (w && h && w <= g_vk.props.limits.maxImageDimension2D')
anchor='        if (!src || src->f.is_int) src = cpu_frame(cpu_addr, w, h, pitch, layout, bhl, fmt);'
assert s.count(anchor)==1
s=s.replace(anchor,'        if (!src || src->f.is_int) {\n            cpu_fallback = 1;\n            src = cpu_frame(cpu_addr, w, h, pitch, layout, bhl, fmt);\n        }')
anchor='    show(src, x1 - x0, y1 - y0, transform, 1);'
assert s.count(anchor)==1
s=s.replace(anchor,r'''    static int resolution_trace_enabled = -1;
    static u32 last_resolution[9];
    if (resolution_trace_enabled < 0) {
        const char* e = getenv("SNX_RESOLUTION_TRACE");
        resolution_trace_enabled = e && *e && *e != '0';
    }
    if (resolution_trace_enabled) {
        u32 dims[9] = {w, h, src ? src->w : 0, src ? src->h : 0,
                       src ? src->sw : 0, src ? src->sh : 0, g_last_w, g_last_h, (u32)cpu_fallback};
        if (memcmp(last_resolution, dims, sizeof dims)) {
            memcpy(last_resolution, dims, sizeof dims);
            LOG("resolution-trace: present buffer=%ux%u guest=%ux%u image=%ux%u crop=%ux%u cpu_fallback=%d\n",
                dims[0], dims[1], dims[2], dims[3], dims[4], dims[5], dims[6], dims[7], cpu_fallback);
        }
    }
'''+anchor)
p.write_text(s)
p=Path('outputs/Capture-Arceus-Timing.ps1');s=p.read_text()
s=s.replace('[switch]$Fullscreen,','[switch]$Fullscreen, [switch]$ResolutionTrace,')
s=s.replace('$env:SNX_ARCEUS_MENU_REPEAT_FIX=',"$env:SNX_RESOLUTION_TRACE=if ($ResolutionTrace) { '1' } else { '0' }\n$env:SNX_ARCEUS_MENU_REPEAT_FIX=")
s=s.replace('resolution_scale_override=$ResolutionScale;','resolution_scale_override=$ResolutionScale;resolution_trace=[bool]$ResolutionTrace;')
p.write_text(s)
for fps in (60,120):
    p=Path(f'outputs/Test-Arceus-1440p-{fps}FPS.cmd')
    s=p.read_text().replace('-ResolutionScale ', '-ResolutionTrace -ResolutionScale ')
    p.write_text(s)
