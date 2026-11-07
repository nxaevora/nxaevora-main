from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text()
s=s.replace('static int clocks_installed;', 'static int clocks_installed;\nstatic u64 blink_remainder;\nstatic unsigned blink_ticks;')
s=s.replace('if (channel == 0) water_step_write(value);', '''if (channel == 0) {
            water_step_write(value);
            /* Shared 30-Hz cadence for the isolated integer blink state machine. */
            blink_remainder += value * 30;
            blink_ticks = (unsigned)(blink_remainder / 1000000000);
            blink_remainder %= 1000000000;
        }''')
insert=r'''
static GuestFn blink_original[2];
static u64 blink_calls[2], blink_updates[2];
static void blink_update(CPU* c, u64 entry, unsigned kind) {
    pm_lock(&step_lock);
    unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    u64 object = c->x[0], return_pc = c->x[30];
    u64 count = __atomic_add_fetch(&blink_calls[kind], 1, __ATOMIC_RELAXED);
    if (!ticks) c->pc = return_pc;
    for (unsigned i = 0; i < ticks; i++) {
        c->x[0] = object;
        c->x[30] = return_pc;
        c->pc = entry;
        blink_original[kind](c, entry);
        __atomic_add_fetch(&blink_updates[kind], 1, __ATOMIC_RELAXED);
        if (c->pc != return_pc) break;
    }
    if (count <= 4 || count % 12000 == 0)
        LOG("arceus-blink: kind=%u calls=%llu updates=%llu ticks=%u\n", kind,
            (unsigned long long)count,
            (unsigned long long)__atomic_load_n(&blink_updates[kind], __ATOMIC_RELAXED), ticks);
}
static void blink_primary(CPU* c, u64 entry) { blink_update(c, entry, 0); }
static void blink_secondary(CPU* c, u64 entry) { blink_update(c, entry, 1); }

/* Both vtable entries implement the same blink countdown, with the second
   accepting the base subobject at +48. Only this small state machine is paced;
   the skeletal/face animation system continues to update every rendered frame. */
static void blink_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_BLINK_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    const u64 offsets[2] = {0x194f110, 0x194f20c};
    const u32 sizes[2] = {0xfc, 0x100};
    const u32 prologues[2] = {0xf81e0ff3, 0xa9be4ff4};
    const GuestFn wrappers[2] = {blink_primary, blink_secondary};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (unsigned kind = 0; kind < 2; kind++) {
        u64 pc = main_base + offsets[kind];
        u64 tail = pc + sizes[kind] - 4;
        int covered = 0;
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == sizes[kind]) covered = 1;
        if (!covered || pc < g_code_lo || tail >= g_code_hi || !readable(pc, sizes[kind]) ||
            gr32(pc) != prologues[kind] || gr32(tail) != 0x17ffffd5 ||
            gr32(main_base + (kind ? 0x194f250 : 0x194f150)) != 0x71000508) {
            WARN("arceus-blink: refused instruction/function guard for kind=%u\n", kind);
            continue;
        }
        /* Equivalent CBZ XZR in place of the final B, avoiding original AOT variants. */
        gw32(tail, 0xb4fffabf);
        jit_code_changed(pc, sizes[kind]);
        blink_original[kind] = jit_wait(pc);
        if (!blink_original[kind]) {
            WARN("arceus-blink: could not compile kind=%u; correction not installed\n", kind);
            continue;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[kind], __ATOMIC_RELEASE);
        LOG("arceus-blink: 30-Hz countdown experiment installed, kind=%u\n", kind);
    }
}

'''
s=s.replace('void arceus_clock_post_jit(void) {',insert+'void arceus_clock_post_jit(void) {').replace('    event_camera_init();','    event_camera_init();\n    blink_fix_init();')
p.write_text(s)
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for o in [0x194f110,0x194f20c,0x194f208,0x194f308,0x194f150,0x194f250]: print(hex(o),hex(struct.unpack_from('<I',b,o)[0]))
