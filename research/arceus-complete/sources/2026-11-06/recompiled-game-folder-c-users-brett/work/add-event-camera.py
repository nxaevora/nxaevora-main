from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c')
s=p.read_text()
insert=r'''
/* EventCameraMove keeps a nominal per-30-Hz-update increment at +4b4.
   Its update only reads that field and never destroys the object. Preserve the
   nominal rate between calls; scale only the value seen by this update. */
static GuestFn event_camera_original;
static u64 event_camera_count;
static void event_camera_update(CPU* c, u64 entry) {
    u64 object = c->x[0];
    MemBlock block;
    if (!readable(object, 0x4c0) || gr32(object + 0x3e4) != 1) {
        event_camera_original(c, entry);
        return;
    }
    mem_query(object + 0x4b4, &block);
    if (!(block.perm & PERM_W) || block.addr + block.size < object + 0x4b8) {
        event_camera_original(c, entry);
        return;
    }
    u32 nominal_bits = gr32(object + 0x4b4), adjusted_bits;
    float nominal, adjusted;
    memcpy(&nominal, &nominal_bits, sizeof nominal);
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    adjusted = (float)((double)nominal * (double)ns / 33333334.0);
    memcpy(&adjusted_bits, &adjusted, sizeof adjusted_bits);
    gw32(object + 0x4b4, adjusted_bits);
    event_camera_original(c, entry);
    gw32(object + 0x4b4, nominal_bits);
    u64 count = __atomic_add_fetch(&event_camera_count, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 120 == 0)
        LOG("arceus-event-camera: count=%llu step_ns=%llu nominal=%g adjusted=%g\n",
            (unsigned long long)count, (unsigned long long)ns, (double)nominal, (double)adjusted);
}

static void event_camera_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_EVENT_CAMERA_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x19b2140;
    if (pc < g_code_lo || pc + 0x198 > g_code_hi || !readable(pc, 0x198) ||
        gr32(pc) != 0xd101c3ff || gr32(pc + 0x34) != 0xbd44b400 ||
        gr32(pc + 0x44) != 0x1e212800 || gr32(pc + 0x194) != 0x17ffffb7 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-event-camera: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] >= 0x198) covered = 1;
    if (!covered) {
        WARN("arceus-event-camera: refused missing AOT function coverage\n");
        return;
    }
    /* Invalidate the AOT body, including direct/inlined callers. Compile the
       unchanged guest body first, then dispatch through our wrapper. No guest
       threads are running yet. Failure leaves the original body to normal JIT. */
    jit_code_changed(pc, 4);
    event_camera_original = jit_wait(pc);
    if (!event_camera_original) {
        WARN("arceus-event-camera: original body did not compile; correction not installed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], event_camera_update, __ATOMIC_RELEASE);
    LOG("arceus-event-camera: experimental elapsed-time increment installed\n");
}

'''
s=s.replace('void arceus_clock_post_jit(void) {',insert+'void arceus_clock_post_jit(void) {')
s=s.replace('    effects_fix_init();\n}', '    effects_fix_init();\n    event_camera_init();\n}')
# Read exact guard words from local binary, validating expected decoded locations.
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for offset in [0,0x34,0x44,0x194]:
 print(hex(offset),hex(struct.unpack_from('<I',b,0x19b2140+offset)[0]))
p.write_text(s)
