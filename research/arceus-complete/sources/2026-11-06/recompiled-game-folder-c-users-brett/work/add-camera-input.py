from pathlib import Path
p=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/src/nx/runtime/arceus_clock.c');s=p.read_text()
insert=r'''
static GuestFn camera_input_original;
static u64 camera_input_calls;
static float guest_float(u64 address) {
    u32 bits = gr32(address); float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
static void guest_float_write(u64 address, float value) {
    u32 bits; memcpy(&bits, &value, sizeof bits); gw32(address, bits);
}
/* Fractional authored updates preserve the original stick response and its
   steady-state value. Scaling only the acceleration coefficients would change
   the equilibrium for partial stick input, which also multiplies the state. */
static void camera_input_update(CPU* c, u64 entry) {
    u64 object = c->x[0], parameters = c->x[1], return_pc = c->x[30];
    if (!readable(object, 0x444)) { camera_input_original(c, entry); return; }
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    double remaining = (double)ns / 33333334.0;
    do {
        float old_yaw = guest_float(object + 0x434), old_pitch = guest_float(object + 0x438);
        c->x[0] = object; c->x[1] = parameters; c->x[30] = return_pc; c->pc = entry;
        camera_input_original(c, entry);
        if (c->pc != return_pc) return;
        if (remaining < 1.0) {
            float yaw = guest_float(object + 0x434), pitch = guest_float(object + 0x438);
            guest_float_write(object + 0x434, (float)(old_yaw + (yaw - old_yaw) * remaining));
            guest_float_write(object + 0x438, (float)(old_pitch + (pitch - old_pitch) * remaining));
        }
        remaining -= 1.0;
    } while (remaining > 0.0);
    u64 count = __atomic_add_fetch(&camera_input_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 600 == 0)
        LOG("arceus-camera-input: count=%llu step_ns=%llu yaw=%g pitch=%g\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)guest_float(object + 0x434), (double)guest_float(object + 0x438));
}
static void camera_input_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    u64 pc = main_base + 0x193c04c;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x278) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x278 > g_code_hi || !readable(pc, 0x278) ||
        gr32(pc) != 0xfc1b0fec || gr32(pc + 0x130) != 0x1e222862 ||
        gr32(pc + 0x274) != 0x17ffffee) {
        WARN("arceus-camera-input: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0x274, 0xb4fffddf); /* Equivalent CBZ XZR, pc+22c. */
    jit_code_changed(pc, 0x278);
    camera_input_original = jit_wait(pc);
    if (!camera_input_original) { WARN("arceus-camera-input: original body did not compile\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], camera_input_update, __ATOMIC_RELEASE);
    LOG("arceus-camera-input: fractional acceleration/deceleration experiment installed\n");
}

'''
s=s.replace('void arceus_clock_post_jit(void) {',insert+'void arceus_clock_post_jit(void) {').replace('    blink_fix_init();','    blink_fix_init();\n    camera_input_init();')
p.write_text(s)
import struct
b=Path(r'C:/Users/brett/Documents/Codex/work/arceus-cache/scarletnx/module-dumps/0244d4dd2474e625/main.bin').read_bytes()
for o in [0x193c04c,0x193c17c,0x193c2c0]:print(hex(o),hex(struct.unpack_from('<I',b,o)[0]))
