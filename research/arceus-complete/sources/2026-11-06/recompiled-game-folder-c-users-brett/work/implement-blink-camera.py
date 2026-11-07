from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
s=p.read_text()
s=s.replace('blink_original[2]', 'blink_original[3]').replace('blink_calls[2], blink_updates[2]', 'blink_calls[3], blink_updates[3]')
s=s.replace('/* Both vtable entries implement the same blink countdown, with the second', 'static void blink_inlined(CPU* c, u64 entry) { blink_update(c, entry, 2); }\n\n/* Three entries implement the same blink countdown, with the second')
s=s.replace('const u64 offsets[2] = {0x194f110, 0x194f20c};', 'const u64 offsets[3] = {0x194f110, 0x194f20c, 0x1aaf0b4};')
s=s.replace('const u32 sizes[2] = {0xfc, 0x100};', 'const u32 sizes[3] = {0xfc, 0x100, 0x2a0};')
s=s.replace('const u32 prologues[2] = {0xf81e0ff3, 0xa9be4ff4};', 'const u32 prologues[3] = {0xf81e0ff3, 0xa9be4ff4, 0xa9ba6ffc};\n    const u64 decrements[3] = {0x194f150, 0x194f250, 0x1aaf12c};\n    const u32 tails[3] = {0x17ffffd5, 0x17ffffd5, 0x17ffffe7};\n    const u32 replacements[3] = {0xb4fffabf, 0xb4fffabf, 0xb4fffcff};')
s=s.replace('const GuestFn wrappers[2] = {blink_primary, blink_secondary};', 'const GuestFn wrappers[3] = {blink_primary, blink_secondary, blink_inlined};')
s=s.replace('kind < 2;', 'kind < 3;').replace('gr32(tail) != 0x17ffffd5', 'gr32(tail) != tails[kind]').replace('gr32(main_base + (kind ? 0x194f250 : 0x194f150))', 'gr32(main_base + decrements[kind])').replace('gw32(tail, 0xb4fffabf);', 'gw32(tail, replacements[kind]);')
insert='''/* The field camera's two manual-rotation callers multiply by this settings
   rate once per update. Pace that angular integration as well as the response
   above; otherwise full-stick turning still runs at the render frequency. */
static GuestFn camera_rate_original;
static u64 camera_rate_calls;
static void camera_rate_update(CPU* c, u64 entry) {
    u64 return_pc = c->x[30];
    camera_rate_original(c, entry);
    if (c->pc != return_pc) return;
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float authored = c->v[0].f[0];
    c->v[0].f[0] = (float)(authored * ((double)ns / 33333334.0));
    u64 count = __atomic_add_fetch(&camera_rate_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 600 == 0)
        LOG("arceus-camera-rate: count=%llu step_ns=%llu authored=%g scaled=%g\\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)authored, (double)c->v[0].f[0]);
}
static void camera_rate_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x193c2c4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0xea8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0xea8 > g_code_hi || !readable(pc, 0xea8) ||
        gr32(pc) != 0xfc1d0fe8 || gr32(pc + 0xea4) != 0x17fffec1) {
        WARN("arceus-camera-rate: refused instruction/function guard\\n"); return;
    }
    gw32(pc + 0xea4, 0xb4ffd83f); /* Equivalent CBZ XZR to pc+9a8. */
    jit_code_changed(pc, 0xea8);
    camera_rate_original = jit_wait(pc);
    if (!camera_rate_original) { WARN("arceus-camera-rate: original body did not compile\\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], camera_rate_update, __ATOMIC_RELEASE);
    LOG("arceus-camera-rate: elapsed-time manual rotation experiment installed\\n");
}

'''
s=s.replace('static GuestFn field_message_original;',insert+'static GuestFn field_message_original;')
s=s.replace('    camera_input_init();','    camera_input_init();\n    camera_rate_init();')
p.write_text(s)
