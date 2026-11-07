from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text()
insert=r'''
static GuestFn field_message_original;
static u64 field_message_calls;
static void field_message_probe(CPU* c, u64 entry) {
    u64 object = c->x[0];
    field_message_original(c, entry);
    u64 count = __atomic_add_fetch(&field_message_calls, 1, __ATOMIC_RELAXED);
    if (count > 8 && count % 300 != 0) return;
    u64 manager = readable(object, 0x68) ? gr64(object + 0x60) : 0;
    u64 root = readable(manager, 0x60) ? gr64(manager + 0x58) : 0;
    /* Resolve the update vtable entries of the actual live message tree. No
       guessed state offsets are modified, and input is never consumed here. */
    u64 queue[16] = {root}; unsigned tail = root ? 1 : 0;
    for (unsigned head = 0; head < tail && head < 16; head++) {
        u64 node = queue[head];
        if (!readable(node, 0xa0)) continue;
        u64 vt = gr64(node), update = readable(vt, 0x88) ? gr64(vt + 0x80) : 0;
        LOG("arceus-field-message: count=%llu node=%llx update=main+%llx\n",
            (unsigned long long)count, (unsigned long long)node, (unsigned long long)(update - main_base));
        for (unsigned i = 0; i < 2; i++) {
            u64 next = gr64(node + 0x90 + i * 8); int seen = !next;
            for (unsigned j = 0; j < tail; j++) if (queue[j] == next) seen = 1;
            if (!seen && tail < 16) queue[tail++] = next;
        }
    }
}
static void field_message_probe_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_DIALOGUE_TRACE");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x2482818;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0xb8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0xb8 > g_code_hi || !readable(pc, 0xb8) ||
        gr32(pc) != 0xd10083ff || gr32(pc + 0xb4) != 0x17ffffda) {
        WARN("arceus-field-message: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0xb4, 0xb4fffb5f); /* Equivalent CBZ XZR, back to pc+1c. */
    jit_code_changed(pc, 0xb8);
    field_message_original = jit_wait(pc);
    if (!field_message_original) { WARN("arceus-field-message: could not compile probe body\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], field_message_probe, __ATOMIC_RELEASE);
    LOG("arceus-field-message: read-only live message update probe installed\n");
}

'''
s=s.replace('void arceus_clock_post_jit(void) {',insert+'void arceus_clock_post_jit(void) {').replace('    camera_input_init();','    camera_input_init();\n    field_message_probe_init();')
p.write_text(s)
q=Path('outputs/Capture-Arceus-Timing.ps1');s=q.read_text().replace('[switch]$EventCameraFix)', '[switch]$EventCameraFix, [switch]$BlinkFix, [switch]$CameraInputFix)')
s=s.replace('$env:SNX_ARCEUS_EVENT_CAMERA_FIX=', '''$env:SNX_ARCEUS_BLINK_FIX=if ($BlinkFix) { '1' } else { '0' }
$env:SNX_ARCEUS_CAMERA_INPUT_FIX=if ($CameraInputFix) { '1' } else { '0' }
if (($BlinkFix -or $CameraInputFix) -and !$TimingFixes) { throw 'Blink/camera input fixes require TimingFixes.' }
$env:SNX_ARCEUS_EVENT_CAMERA_FIX=''')
q.write_text(s)
for fps in (60,120):
 Path(f'outputs/Test-Arceus-Blink-Camera-{fps}FPS.cmd').write_text('@echo off\nsetlocal\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Capture-Arceus-Timing.ps1" -ExperimentalFps '+str(fps)+' -TimingFixes -EventCameraFix -BlinkFix -CameraInputFix -DialogueTrace -CaptureDisplay\npause\n')
