from pathlib import Path
import struct,json
root=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime')
p=root/'arceus_camera.h';s=p.read_text().replace('fabsf(raw) < deadzone','(deadzone > 0.0f && deadzone < 1.0f && fabsf(raw) < deadzone)').replace('fabsf(raw) == 1.0f && next >= old','fabsf(raw) == 1.0f && next == fminf(1.0f, old + accel)');p.write_text(s)
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes();fns=dict(json.loads(Path('work/timing-index.json').read_text())['fns'])
rows=[]
for o in [0x2795204,0x2af25b4]:
 z=fns[o]; candidates=[]
 for d in range(0,z,4):
  w=struct.unpack_from('<I',b,o+d)[0]
  if w>>26!=5:continue
  n=w&0x3ffffff;n=n-(1<<26) if n&(1<<25) else n
  if 0<=d+4*n<z and -(1<<18)<=n<(1<<18):candidates.append((d,w,0xb4000000|((n&0x7ffff)<<5)|31))
 d,w,r=candidates[-1];rows.append((o,z,struct.unpack_from('<I',b,o)[0],d,w,r))
print('Verified research probe guards:',[[hex(v) for v in row] for row in rows])
initrows=',\n        '.join('{'+','.join(hex(v) for v in row)+'}' for row in rows)
code='''/* Read-only cadence/mode observations. Do not change either physics dt here:
   a fixed-step subsystem may have its own scheduler. */
static GuestFn timing_research_original[2];
static u64 timing_research_calls[2];
static void timing_research_update(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], caller = c->x[30];
    u64 count = __atomic_add_fetch(&timing_research_calls[kind], 1, __ATOMIC_RELAXED);
    if ((count <= 4 || count % 300 == 0) && readable(object, 0x154)) {
        unsigned mode = gr32(object + (kind ? 0x120 : 0x150));
        mode = kind ? (mode >> 8) & 255 : mode & 255;
        LOG("arceus-timing-research: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx incoming_dt=%g variable_mode=%u rate=%g\\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)c->v[0].f[0], mode, kind ? (double)guest_float(object+0x124) : 1.0);
    }
    timing_research_original[kind](c, entry);
}
static void navigation_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 0); }
static void physics_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 1); }
static void timing_research_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    if (!main_base || !enabled || strcmp(enabled,"1")) return;
    static const struct { u64 offset; u32 size, head, branch, word, replacement; } sites[2] = {
        ROWS
    };
    const GuestFn wrappers[2] = {navigation_probe, physics_probe};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (unsigned k=0; k<2; k++) {
        u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0; i<g_mod->nfns; i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].branch)!=sites[k].word) {
            WARN("arceus-timing-research: refused guard kind=%u\\n", k); continue;
        }
        gw32(pc+sites[k].branch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        timing_research_original[k]=jit_wait(pc);
        if (!timing_research_original[k]) { WARN("arceus-timing-research: compile failed kind=%u\\n",k); continue; }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-timing-research: read-only probe installed kind=%u\\n",k);
    }
}

'''.replace('ROWS',initrows)
p=root/'arceus_clock.c';s=p.read_text().replace('void arceus_clock_post_jit(void) {',code+'void arceus_clock_post_jit(void) {\n    timing_research_init();');p.write_text(s)
q=Path('outputs/Capture-Arceus-Timing.ps1');s=q.read_text().replace('[switch]$CameraTrace,','[switch]$CameraTrace, [switch]$TimingResearch,');s=s.replace('$env:SNX_ARCEUS_CAMERA_TRACE=',"$env:SNX_ARCEUS_TIMING_RESEARCH=if ($TimingResearch) { '1' } else { '0' }\n$env:SNX_ARCEUS_CAMERA_TRACE=");q.write_text(s)
for name in ['Test-Arceus-Responsive-Camera-120FPS.cmd','Test-Arceus-Responsive-Camera-60FPS.cmd','Test-Arceus-Camera-Baseline-30FPS.cmd']:
 p=Path('outputs')/name;s=p.read_text().replace('-CaptureDisplay','-TimingResearch -CaptureDisplay');p.write_text(s)
Path('work/research-probe-guards.json').write_text(json.dumps(rows))
