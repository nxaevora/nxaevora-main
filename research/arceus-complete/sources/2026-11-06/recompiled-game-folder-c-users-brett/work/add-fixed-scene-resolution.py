from pathlib import Path
import shutil,struct,json
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
shutil.copy2(p,'work/arceus-clock-before-fixed-resolution.c')
s=p.read_text()
code=r'''/* Arceus 1.1.1's 3D resolution governor (3DBufferWidthScale/HeightScale).
   The update averages GPU duration against 33.333 ms and chooses the 0.90/0.85,
   0.85/0.80 and 0.80/0.70 scene scales. Its mode setter is separate (CC3888):
   normal mode begins at 1.0/1.0, with explicit half-size/custom/special modes.
   Keep those setters intact and skip only this automatic governor's update.
   The native Vulkan render scale still controls the allocated image size. */
static void fixed_resolution_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_FIXED_RESOLUTION");
    if (!main_base || !enabled || strcmp(enabled, "1")) return;
    const u64 pc = main_base + 0xcc35a4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x2c8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x2c8 > g_code_hi || !readable(pc, 0x2c8) ||
        gr32(pc) != 0xd10103ff || gr32(pc + 0x20) != 0x51000509 ||
        gr32(pc + 0x24) != 0x7100093f || gr32(pc + 0x28) != 0x54000308 ||
        gr32(pc + 0x94) != 0xd65f03c0) {
        WARN("arceus-resolution: refused governor instruction/function guard\n");
        return;
    }
    /* SUB W9,W8,#1 -> MOV W9,#3. The existing CMP #2 / B.HI takes the
       governor's ordinary early return before counters, timing or scale writes. */
    gw32(pc + 0x20, 0x52800069);
    jit_code_changed(pc, 0x2c8);
    if (!jit_wait(pc)) {
        WARN("arceus-resolution: governor JIT failed\n");
        return;
    }
    LOG("arceus-resolution: automatic 3D resolution governor disabled; explicit scene modes unchanged\n");
}

'''
anchor='void arceus_clock_post_jit(void) {'
assert s.count(anchor)==1
s=s.replace(anchor,code+anchor+'\n    fixed_resolution_init();')
p.write_text(s)
p=Path('outputs/Capture-Arceus-Timing.ps1');s=p.read_text()
s=s.replace('[switch]$ResolutionTrace,','[switch]$ResolutionTrace, [switch]$FixedSceneResolution,')
s=s.replace('$env:SNX_RESOLUTION_TRACE=',"$env:SNX_ARCEUS_FIXED_RESOLUTION=if ($FixedSceneResolution) { '1' } else { '0' }\n$env:SNX_RESOLUTION_TRACE=")
s=s.replace('resolution_trace=[bool]$ResolutionTrace;','resolution_trace=[bool]$ResolutionTrace;fixed_scene_resolution=[bool]$FixedSceneResolution;')
p.write_text(s)
for fps in (60,120):
    s=Path(f'outputs/Test-Arceus-1440p-{fps}FPS.cmd').read_text()
    s=s.replace('-ResolutionTrace','-FixedSceneResolution -ResolutionTrace')
    Path(f'outputs/Test-Arceus-Fixed-Resolution-1440p-{fps}FPS.cmd').write_text(s)
