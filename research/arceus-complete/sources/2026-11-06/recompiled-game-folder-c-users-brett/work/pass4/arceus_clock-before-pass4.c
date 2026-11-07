/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_clock.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/options.h"
#include "nx/runtime/arceus_step.h"
#include "nx/runtime/arceus_camera.h"
#include "nx/runtime/guest_jit.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static GuestFn originals[3];
static u64 counts[3];
static u64 main_base;
static int experimental, tracing, research_observing;
static u64 ui_clock_research_calls;
static ArceusStep step;
static PMutex step_lock = PMUTEX_INIT;
static GuestFn message_original;
static u64 message_count;
static u64 water_step_address;
static int clocks_installed;
static u64 blink_remainder;
static unsigned blink_ticks;

static void water_step_write(u64 ns) {
    if (!water_step_address) return;
    float seconds = (float)((double)ns / 1000000000.0);
    u32 bits;
    memcpy(&bits, &seconds, sizeof bits);
    gw32(water_step_address, bits);
}

static void adjust_duration(CPU* c) {
    static const u64 callers[4] = {0x278150c, 0x2781540, 0x2781574, 0x27815a8};
    unsigned channel = (unsigned)c->x[1];
    u64 value;
    if (!experimental || c->x[1] > 3 || c->x[30] != main_base + callers[channel]) return;
    if (c->x[2] != 33333334) {
        WARN_LIMIT(4, "arceus-clock: unexpected main-loop duration %llu, left unchanged\n", (unsigned long long)c->x[2]);
        return;
    }
    pm_lock(&step_lock);
    if (arceus_step(&step, plat_ns(), c->x[0], channel, &value)) {
        c->x[2] = value;
        if (channel == 0) {
            water_step_write(value);
            /* Shared 30-Hz cadence for the isolated integer blink state machine. */
            blink_remainder += value * 30;
            blink_ticks = (unsigned)(blink_remainder / 1000000000);
            blink_remainder %= 1000000000;
        }
    }
    else WARN_LIMIT(4, "arceus-clock: clock update order changed, left unchanged\n");
    pm_unlock(&step_lock);
}

static int readable(u64 address, u64 size) {
    MemBlock block;
    u64 end = address + size;
    if (address < AS_BASE || end < address || end > AS_SIZE) return 0;
    while (address < end) {
        mem_query(address, &block);
        if (!(block.perm & PERM_R) || block.addr + block.size <= address) return 0;
        address = block.addr + block.size;
    }
    return 1;
}

static void trace_call(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], channel = c->x[1], input = c->x[2], caller = c->x[30];
    u64 count = __atomic_add_fetch(&counts[kind], 1, __ATOMIC_RELAXED);
    if (kind == 2) adjust_duration(c);
    originals[kind](c, entry);
    /* At this exact call site X19 is the UI animation owner and X21 is its
       signed clock-mode index; both are callee-saved by the clock getter.
       Observe the real denominator, without altering animation progress. */
    if (research_observing && kind == 0 && caller == main_base + 0x2b4344c && c->pc == caller) {
        const u64 ui_count = __atomic_add_fetch(&ui_clock_research_calls, 1, __ATOMIC_RELAXED);
        if ((ui_count <= 8 || ui_count % 600 == 0) && readable(c->x[0], 16)) {
            const u64 mode = c->x[21], table = main_base + 0x39848e0;
            const u64 nominal = mode < 5 && readable(table + mode * 8, 8) ? gr64(table + mode * 8) : 0;
            LOG("arceus-ui-clock: count=%llu ns=%llu object=%llx channel=%llu mode=%lld duration_ns=%llu frame_ns=%llu\n",
                (unsigned long long)ui_count, (unsigned long long)plat_ns(),
                (unsigned long long)c->x[19], (unsigned long long)channel, (long long)mode,
                (unsigned long long)gr64(c->x[0] + 8), (unsigned long long)(nominal ? nominal - 1 : 0));
        }
    }
    if (tracing && (count <= 40 || count % (kind == 0 ? 60000 : 600) == 0)) {
        u64 record = kind == 0 ? c->x[0] : object + 8 + channel * 16;
        u64 duration = readable(record, 16) ? gr64(record + 8) : 0;
        LOG("arceus-clock: kind=%u count=%llu ns=%llu object=%llx channel=%llu input=%llu duration=%llu caller=main+%llx\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)channel, (unsigned long long)input,
            (unsigned long long)duration, (unsigned long long)(caller - main_base));
    }
}
static void read_clock(CPU* c, u64 entry) { trace_call(c, entry, 0); }
static void set_mode(CPU* c, u64 entry) { trace_call(c, entry, 1); }
static void set_duration(CPU* c, u64 entry) { trace_call(c, entry, 2); }

/* Candidate MessageView update, identified through the MessageView string references
   and its vtable. State meanings are not established: observe, never alter them. */
static void message_view_probe(CPU* c, u64 entry) {
    u64 object = c->x[0], caller = c->x[30], begin = plat_ns();
    u64 count = __atomic_add_fetch(&message_count, 1, __ATOMIC_RELAXED);
    u32 before = readable(object, 0xf8) ? gr32(object + 0xf0) : ~0u;
    message_original(c, entry);
    u32 after = readable(object, 0xf8) ? gr32(object + 0xf0) : ~0u;
    if (count <= 10 || before != after || count % 60 == 0)
        LOG("arceus-message: count=%llu ns=%llu object=%llx state_before=%u state_after=%u caller=main+%llx\n",
            (unsigned long long)count, (unsigned long long)begin,
            (unsigned long long)object, before, after, (unsigned long long)(caller - main_base));
}

/* The default probe only observes virtual calls. The optional experiment changes the four verified
   main-loop duration writes. Build and instruction guards precede both hooks and the 60 FPS pacing override. */
void arceus_clock_init(void) {
    static const u8 id[32] = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                              0xd9,0xf3,0xc5,0x1e,0,0,0,0,0,0,0,0,0,0,0,0};
    static const u64 offsets[3] = {0x278303c, 0x2783048, 0x277bef4};
    static const u32 words[3] = {0x8b21d008, 0xf0008fc8, 0x8b21d008};
    static const GuestFn wrappers[3] = {read_clock, set_mode, set_duration};
    const char* enabled = getenv("SNX_ARCEUS_CLOCK_TRACE");
    const char* fps60 = getenv("SNX_ARCEUS_FPS60");
    const char* target = getenv("SNX_ARCEUS_TARGET_FPS");
    int target_fps = fps60 && !strcmp(fps60, "1") ? 60 : 0;
    if (target && *target) {
        if (!strcmp(target, "60")) target_fps = 60;
        else if (!strcmp(target, "120")) target_fps = 120;
        else {
            WARN("arceus-clock: refused unsupported target FPS (use 60 or 120)\n");
            return;
        }
    }
    const char* message_flag = getenv("SNX_ARCEUS_DIALOGUE_TRACE");
    int message_trace = message_flag && !strcmp(message_flag, "1");
    const SnxModInfo* mod = NULL;
    tracing = enabled && !strcmp(enabled, "1");
    experimental = target_fps != 0;
    if (!tracing && !experimental && !message_trace) return;
    for (u32 i = 0; g_mod && i < g_mod->nmods; i++)
        if (!strcmp(g_mod->mods[i].name, "main")) mod = &g_mod->mods[i];
    if (!mod || memcmp(mod->build_id, id, sizeof id)) {
        WARN("arceus-clock: refused unsupported main build\n");
        return;
    }
    main_base = mod->base;
    if (message_trace) {
        u64 pc = main_base + 0x120ae38;
        if (pc >= g_code_lo && pc < g_code_hi && readable(pc, 4) && gr32(pc) == 0xa9ba6ffc &&
            g_disp[(pc - g_code_lo) / 4]) {
            message_original = g_disp[(pc - g_code_lo) / 4];
            g_disp[(pc - g_code_lo) / 4] = message_view_probe;
            LOG("arceus-message: read-only candidate MessageView probe installed\n");
        } else WARN("arceus-message: refused missing or changed MessageView entry\n");
    }
    if (!tracing && !experimental) return;
    for (unsigned i = 0; i < 3; i++) {
        u64 pc = mod->base + offsets[i];
        if (pc < g_code_lo || pc >= g_code_hi || !readable(pc, 4) || gr32(pc) != words[i] ||
            !g_disp[(pc - g_code_lo) / 4]) {
            WARN("arceus-clock: refused missing or changed clock entry %u\n", i);
            return;
        }
        originals[i] = g_disp[(pc - g_code_lo) / 4];
    }
    main_base = mod->base;
    for (unsigned i = 0; i < 3; i++)
        g_disp[(main_base + offsets[i] - g_code_lo) / 4] = wrappers[i];
    if (experimental) {
        step.initial_ns = (UINT64_C(1000000000) + target_fps / 2) / target_fps;
        __atomic_store_n(&g_opt_fps, target_fps, __ATOMIC_RELAXED);
        clocks_installed = 1;
    }
    LOG("arceus-clock: Arceus 1.1.1 %s, target_fps=%d, tracing=%d\n",
        experimental ? "EXPERIMENT: elapsed-time clocks" : "read-only probe", target_fps, tracing);
}

/* WaterCurrentTime's verified update adds 1/30 second per rendered frame. Replace
   only its two constant-building instructions with a load of the shared elapsed
   seconds. Mark the containing AOT function stale so direct/inlined calls also
   execute the changed instructions through the runtime's existing JIT path. */
static void water_fix_init(void) {
    u64 pc = main_base + 0x29faf8c;
    if (!readable(pc, 8) || gr32(pc) != 0x52911128 || gr32(pc + 4) != 0x72a7a108 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-water: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] <= pc && g_mod->fn_lo[i] + g_mod->fn_len[i] >= pc + 8) covered = 1;
    if (!covered) {
        WARN("arceus-water: refused patch without AOT function coverage\n");
        return;
    }
    u64 address = mem_find_free(main_base + 0x5000000, main_base + 0x40000000, 4096, 4096);
    if (!address || mem_map(address, 4096, MS_STATIC, PERM_RW)) {
        WARN("arceus-water: could not allocate elapsed-time cell\n");
        return;
    }
    water_step_address = address;
    water_step_write(step.initial_ns);
    /* ADRP X8, cell; LDR W8,[X8,#0]. The cell is page aligned and within ADRP range. */
    u64 pages = ((address >> 12) - (pc >> 12)) & 0x1fffff;
    u32 adrp = 0x90000008 | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5);
    gw32(pc, adrp);
    gw32(pc + 4, 0xb9400108);
    jit_code_changed(pc, 8);
    LOG("arceus-water: elapsed-time increment installed, cell=%llx\n", (unsigned long long)address);
}

static void effects_fix_init(void) {
    u64 pc = main_base + 0x2b90040;
    /* The original converts ns/16666666.0 to an integer and back to float.
       At high FPS that drops the entire update. The downstream effect-group
       calculation accepts float progress, so preserve the fractional value. */
    if (!readable(pc, 16) || gr32(pc) != 0x1e780008 || gr32(pc + 4) != 0xbd402280 ||
        gr32(pc + 8) != 0x1e220101 || gr32(pc + 12) != 0x1e210808 ||
        !g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) {
        WARN("arceus-effects: refused instruction or AOT invalidation guard\n");
        return;
    }
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] <= pc && g_mod->fn_lo[i] + g_mod->fn_len[i] >= pc + 16) covered = 1;
    if (!covered) {
        WARN("arceus-effects: refused patch without AOT function coverage\n");
        return;
    }
    gw32(pc, 0x1e624001); /* FCVT S1,D0 instead of FCVTZS W8,D0 */
    gw32(pc + 8, 0xd503201f); /* NOP instead of SCVTF S1,W8; W8 is overwritten before its next use */
    jit_code_changed(pc, 12);
    LOG("arceus-effects: fractional effect-group progress installed\n");
}


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
    /* Equivalent B -> CBZ XZR at the final back edge prevents a hash-matched
       original AOT variant being selected after invalidation. Both instructions
       always branch to pc+0x70. Invalidate direct/inlined AOT callers, compile
       this equivalent body, then install the wrapper before guest threads run.
       Compilation failure leaves normal JIT dispatch without the correction. */
    gw32(pc + 0x194, 0xb4fff6ff);
    jit_code_changed(pc, 0x198);
    event_camera_original = jit_wait(pc);
    if (!event_camera_original) {
        WARN("arceus-event-camera: original body did not compile; correction not installed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], event_camera_update, __ATOMIC_RELEASE);
    LOG("arceus-event-camera: experimental elapsed-time increment installed\n");
}


static GuestFn blink_original[3];
static u64 blink_calls[3], blink_updates[3];
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

static void blink_inlined(CPU* c, u64 entry) { blink_update(c, entry, 2); }

/* Three entries implement the same blink countdown, with the second
   accepting the base subobject at +48. Only this small state machine is paced;
   the skeletal/face animation system continues to update every rendered frame. */
static void blink_fix_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_BLINK_FIX");
    if (!enabled || strcmp(enabled, "1")) return;
    const u64 offsets[3] = {0x194f110, 0x194f20c, 0x1aaf0b4};
    const u32 sizes[3] = {0xfc, 0x100, 0x2a0};
    const u32 prologues[3] = {0xf81e0ff3, 0xa9be4ff4, 0xa9ba6ffc};
    const u64 decrements[3] = {0x194f150, 0x194f250, 0x1aaf12c};
    const u32 tails[3] = {0x17ffffd5, 0x17ffffd5, 0x17ffffe7};
    const u32 replacements[3] = {0xb4fffabf, 0xb4fffabf, 0xb4fffcff};
    const GuestFn wrappers[3] = {blink_primary, blink_secondary, blink_inlined};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (unsigned kind = 0; kind < 3; kind++) {
        u64 pc = main_base + offsets[kind];
        u64 tail = pc + sizes[kind] - 4;
        int covered = 0;
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == sizes[kind]) covered = 1;
        if (!covered || pc < g_code_lo || tail >= g_code_hi || !readable(pc, sizes[kind]) ||
            gr32(pc) != prologues[kind] || gr32(tail) != tails[kind] ||
            gr32(main_base + decrements[kind]) != 0x71000508) {
            WARN("arceus-blink: refused instruction/function guard for kind=%u\n", kind);
            continue;
        }
        /* Equivalent CBZ XZR in place of the final B, avoiding original AOT variants. */
        gw32(tail, replacements[kind]);
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


static GuestFn camera_input_original;
static u64 camera_input_calls;
static int camera_trace_only;
static float camera_speed = 1.0f;
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
    if (camera_trace_only) ns = 33333334;
    double remaining = (double)ns / 33333334.0;
    float raw_yaw = guest_float(object + 0x38c), raw_pitch = guest_float(object + 0x390);
    float deadzone = readable(parameters, 4) ? guest_float(parameters) : 0.0f;
    int active = (gr32(object + 0x380) & 0xff) != 0;
    do {
        float old_yaw = guest_float(object + 0x434), old_pitch = guest_float(object + 0x438);
        c->x[0] = object; c->x[1] = parameters; c->x[30] = return_pc; c->pc = entry;
        camera_input_original(c, entry);
        if (c->pc != return_pc) return;
        if (remaining < 1.0) {
            float yaw = guest_float(object + 0x434), pitch = guest_float(object + 0x438);
            /* The authored release is a linear subtract followed by a clamp.
               Interpolating the already-clamped result leaves an exponential
               tail. Full-stick saturation has the corresponding upper-end bug. */
            float old[2] = {old_yaw, old_pitch}, next[2] = {yaw, pitch};
            float raw[2] = {raw_yaw, raw_pitch};
            for (unsigned axis = 0; axis < 2; axis++) {
                float value = arceus_camera_fraction(old[axis], next[axis], raw[axis], deadzone, active,
                    guest_float(object + 0x424 + axis * 8), guest_float(object + 0x428 + axis * 8), remaining);
                guest_float_write(object + 0x434 + axis * 4, value);
            }
        }
        remaining -= 1.0;
    } while (remaining > 0.0);
    u64 count = __atomic_add_fetch(&camera_input_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 60 == 0)
        LOG("arceus-camera-input: count=%llu step_ns=%llu yaw=%g pitch=%g ns=%llu object=%llx caller=main+%llx raw=%g,%g deadzone=%g accel=%g,%g decel=%g,%g\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)guest_float(object + 0x434), (double)guest_float(object + 0x438),
            (unsigned long long)plat_ns(), (unsigned long long)object,
            (unsigned long long)(return_pc - main_base), (double)raw_yaw, (double)raw_pitch, (double)deadzone,
            (double)guest_float(object + 0x424), (double)guest_float(object + 0x42c),
            (double)guest_float(object + 0x428), (double)guest_float(object + 0x430));
}
static void camera_input_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;
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
    LOG("arceus-camera-input: response hook installed, read_only=%d\n", camera_trace_only);
}


/* The field camera's two manual-rotation callers multiply by this settings
   rate once per update. Pace that angular integration as well as the response
   above; otherwise full-stick turning still runs at the render frequency. */
static GuestFn camera_rate_original;
static u64 camera_rate_calls;
static float camera_last_authored = -1.0f;
static void camera_rate_update(CPU* c, u64 entry) {
    u64 return_pc = c->x[30], camera_object = c->x[19];
    camera_rate_original(c, entry);
    if (c->pc != return_pc) return;
    pm_lock(&step_lock);
    u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float authored = c->v[0].f[0];
    if (!camera_trace_only) c->v[0].f[0] = (float)(authored * camera_speed * ((double)ns / 33333334.0));
    u64 count = __atomic_add_fetch(&camera_rate_calls, 1, __ATOMIC_RELAXED);
    int changed = authored != camera_last_authored;
    camera_last_authored = authored;
    if (changed || count <= 8 || count % 60 == 0) {
        LOG("arceus-camera-rate: count=%llu step_ns=%llu authored=%g scaled=%g ns=%llu caller=main+%llx speed=%g\n",
            (unsigned long long)count, (unsigned long long)ns,
            (double)authored, (double)c->v[0].f[0], (unsigned long long)plat_ns(),
            (unsigned long long)(return_pc - main_base), (double)camera_speed);
        if (return_pc == main_base + 0x193d270 && readable(camera_object, 0x70c))
            LOG("arceus-camera-field: object=%llx yaw=%g yaw_step=%g pitch_step=%g\n",
                (unsigned long long)camera_object, (double)guest_float(camera_object + 0x360),
                (double)guest_float(camera_object + 0x420), (double)guest_float(camera_object + 0x708));
    }
}
static void camera_rate_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CAMERA_INPUT_FIX");
    if ((!enabled || strcmp(enabled, "1")) && !camera_trace_only) return;
    const char* speed = getenv("SNX_ARCEUS_CAMERA_SPEED");
    if (speed && *speed) {
        char* end; float value = strtof(speed, &end);
        if (*end || !isfinite(value) || value < 0.25f || value > 4.0f)
            WARN("arceus-camera-rate: invalid camera speed, using 1\n");
        else camera_speed = value;
    }
    u64 pc = main_base + 0x193c2c4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0xea8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0xea8 > g_code_hi || !readable(pc, 0xea8) ||
        gr32(pc) != 0xfc1d0fe8 || gr32(pc + 0xea4) != 0x17fffec1) {
        WARN("arceus-camera-rate: refused instruction/function guard\n"); return;
    }
    gw32(pc + 0xea4, 0xb4ffd83f); /* Equivalent CBZ XZR to pc+9a8. */
    jit_code_changed(pc, 0xea8);
    camera_rate_original = jit_wait(pc);
    if (!camera_rate_original) { WARN("arceus-camera-rate: original body did not compile\n"); return; }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], camera_rate_update, __ATOMIC_RELEASE);
    LOG("arceus-camera-rate: rotation hook installed, read_only=%d speed=%g\n", camera_trace_only, (double)camera_speed);
}

/* This collector updates held/pressed/released masks every render. Only its
   autorepeat countdown is authored in 30-Hz ticks. Do not throttle the collector
   itself: that would lose short presses and slow analog movement/camera input. */
static GuestFn pad_repeat_original;
static u64 pad_repeat_calls;
static void pad_repeat_update(CPU* c, u64 entry) {
    u64 object = c->x[0], return_pc = c->x[30];
    pm_lock(&step_lock);
    unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    pad_repeat_original(c, entry);
    if (c->pc != return_pc || !readable(object, 0x104)) return;
    u32 counter = gr32(object + 0x18);
    u64 repeat = gr64(object + 0xb8);
    if (!ticks) {
        /* Undo exactly the one decrement executed by the original collector,
           including a countdown it just initialized after a direction change.
           +A8 is the separate fresh-press mask and remains untouched. */
        gw32(object + 0x18, counter + 1);
        gw64(object + 0xb8, 0);
    }
    /* At most one repeat per render even after a stall: no catch-up burst. */
    u64 count = __atomic_add_fetch(&pad_repeat_calls, 1, __ATOMIC_RELAXED);
    if (count <= 8 || count % 600 == 0)
        LOG("arceus-pad-repeat: count=%llu ticks=%u object=%llx initial=%u interval=%u counter=%u pressed=%llx repeat_before=%llx repeat_after=%llx\n",
            (unsigned long long)count, ticks, (unsigned long long)object,
            gr32(object+0x10), gr32(object+0x14), gr32(object+0x18),
            (unsigned long long)gr64(object+0xa8), (unsigned long long)repeat,
            (unsigned long long)gr64(object+0xb8));
}
static void pad_repeat_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_MENU_REPEAT_FIX");
    if (!enabled || strcmp(enabled,"1")) return;
    u64 pc = main_base + 0xc9a9b4;
    int covered = 0;
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i=0; i<g_mod->nfns; i++)
        if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==0x8e8) covered=1;
    if (!covered || pc<g_code_lo || pc+0x8e8>g_code_hi || !readable(pc,0x8e8) ||
        gr32(pc)!=0x6db923e9 || gr32(main_base+0xc9ac44)!=0x51000530 ||
        gr32(pc+0x8e4)!=0x17ffffea) {
        WARN("arceus-pad-repeat: refused instruction/function guard\n"); return;
    }
    gw32(pc+0x8e4,0xb4fffd5f); /* Equivalent final B -> CBZ XZR. */
    jit_code_changed(pc,0x8e8);
    pad_repeat_original=jit_wait(pc);
    if (!pad_repeat_original) { WARN("arceus-pad-repeat: compile failed\n"); return; }
    __atomic_store_n(&g_disp[(pc-g_code_lo)/4],pad_repeat_update,__ATOMIC_RELEASE);
    LOG("arceus-pad-repeat: 30-Hz autorepeat countdown installed; fresh presses unchanged\n");
}

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

/* Read-only cadence/mode observations. Do not change either physics dt here:
   a fixed-step subsystem may have its own scheduler. */
static GuestFn timing_research_original[2];
static u64 timing_research_calls[2];
static void timing_research_update(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], caller = c->x[30];
    u64 count = __atomic_add_fetch(&timing_research_calls[kind], 1, __ATOMIC_RELAXED);
    if ((count <= 4 || count % 300 == 0) && readable(object, 0x154)) {
        unsigned mode = gr32(object + (kind ? 0x120 : 0x150));
        mode = kind ? (mode >> 8) & 255 : mode & 255;
        LOG("arceus-timing-research: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx incoming_dt=%g variable_mode=%u rate=%g\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)c->v[0].f[0], mode, kind ? (double)guest_float(object+0x124) : 1.0);
    }
    timing_research_original[kind](c, entry);
}
static void navigation_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 0); }
static void physics_probe(CPU* c, u64 entry) { timing_research_update(c, entry, 1); }

/* These probes preserve the guest's counters. In particular +88 is a script
   context's accumulated frames since its scheduler last reset it, not a global
   frame number. Compare it with the paired seconds at +84 before choosing a
   correction. The ride callbacks are separate copies of the blink countdown;
   their live cadence has not yet been measured. */
static GuestFn engine_clock_original[4];
static u64 engine_clock_calls[4];
static void engine_clock_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object = c->x[0], caller = c->x[30];
    const u64 count = __atomic_add_fetch(&engine_clock_calls[kind], 1, __ATOMIC_RELAXED);
    const int sample = count <= 8 || count % (kind ? 12000 : 600) == 0;
    const u32 field = kind == 0 ? 0x88 : kind == 1 ? 0x68c : 0x644;
    const int valid = sample && readable(object, field + 4);
    const u32 before = valid ? gr32(object + field) : 0;
    const float seconds = valid && !kind ? guest_float(object + 0x84) : 0.0f;
    const u32 flags = valid && !kind ? gr32(object + 0x80) : 0;
    const u64 now = valid ? plat_ns() : 0;
    engine_clock_original[kind](c, entry);
    if (valid) {
        const u32 after = !kind ? (u32)c->x[0] : readable(object, field + 4) ? gr32(object + field) : ~0u;
        LOG("arceus-engine-clock: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx before=%u after=%u seconds=%g flags=%x\n",
            kind, (unsigned long long)count, (unsigned long long)now,
            (unsigned long long)object, (unsigned long long)(caller-main_base), before, after,
            (double)seconds, flags);
    }
}
static void script_frames_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 0); }
static void ride_blink_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 1); }
static void ride_blink_subobject_probe(CPU* c, u64 entry) { engine_clock_probe(c, entry, 2); }
static void physics_world_step_probe(CPU* c, u64 entry) {
    const u64 count=__atomic_add_fetch(&engine_clock_calls[3],1,__ATOMIC_RELAXED);
    const u64 object=c->x[0], caller=c->x[30];
    const int sample=(count<=8 || count%300==0) && readable(c->x[1],12);
    const u64 now=sample ? plat_ns() : 0;
    const float step=sample ? guest_float(c->x[1]) : 0.0f;
    const u32 word4=sample ? gr32(c->x[1]+4) : 0, word8=sample ? gr32(c->x[1]+8) : 0;
    engine_clock_original[3](c,entry);
    if (sample) {
        /* Observe the solver storage after task setup. The alternate-world path
           can own different data, so record its presence rather than assuming
           these fields were written on every invocation. Never alter StepInfo. */
        LOG("arceus-physics-step: count=%llu ns=%llu object=%llx caller=main+%llx step_seconds=%g raw_word4=%u raw_word8=%u\n",
            (unsigned long long)count, (unsigned long long)now,
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)step, word4, word8);
        if (readable(object,0x650))
            LOG("arceus-physics-solver: count=%llu ns=%llu object=%llx input_seconds=%g stored_seconds=%g inverse_seconds=%g substep_seconds=%g substeps=%u alternate_world=%u\n",
                (unsigned long long)count, (unsigned long long)now, (unsigned long long)object,
                (double)step, (double)guest_float(object+0x540), (double)guest_float(object+0x548),
                (double)guest_float(object+0x568), gr32(object+0x5a0), gr64(object+0x648)!=0);
    }
}
static void engine_clock_research_init(void) {
    static const struct { u64 offset; u32 size, head, patch, word, replacement, extra, extra_word; } sites[4] = {
        /* LDR W0,[X0,#0x88] -> LDUR W0,[X0,#0x88], same address and result. */
        {0x1059a78,0x8,0xb9408800,0x0,0xb9408800,0xb8488000,0x4,0xd65f03c0},
        /* Same unconditional branch via CBZ XZR, so no original AOT variant is reused. */
        {0x2518ba8,0xfc,0xf81e0ff3,0xf8,0x17ffffd5,0xb4fffabf,0x40,0x71000508},
        {0x2518ca4,0xfc,0xf81e0ff3,0xf8,0x17ffffd5,0xb4fffabf,0x40,0x71000508},
        /* Havok collision task setup copies the StepInfo at X1, before solving. */
        {0x2f39630,0xed8,0xa9ba6ffc,0xed4,0x17ffffc4,0xb4fff89f,0xd4,0xf94002a8}
    };
    const GuestFn wrappers[4] = {script_frames_probe, ride_blink_probe, ride_blink_subobject_probe, physics_world_step_probe};
    for (unsigned k=0; k<4; k++) {
        const u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0; i<g_mod->nfns; i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
            gr32(pc+sites[k].extra)!=sites[k].extra_word) {
            WARN("arceus-engine-clock: refused instruction/function guard kind=%u\n",k);continue;
        }
        gw32(pc+sites[k].patch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        engine_clock_original[k]=jit_wait(pc);
        if (!engine_clock_original[k]) {
            WARN("arceus-engine-clock: compile failed kind=%u\n",k);continue;
        }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-engine-clock: observation probe installed kind=%u\n",k);
    }
}
/* Further observation-only frame counters. Throwable states share +328 but
   advance it with different authored increments; never scale the whole object
   updater, which also handles lifetime, input and transform work. */
static GuestFn frame_timer_original[5];
static u64 frame_timer_calls[5], frame_timer_changes[5];
static void frame_timer_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object=c->x[0], caller=c->x[30];
    const u32 field=kind==0 ? 0x414 : kind==1 ? 0x238 : 0x328;
    const int valid=readable(object,field+4);
    const u32 before=valid ? gr32(object+field) : 0;
    const u32 state=valid ? (kind==0 ? *GP(object+0x3f9) :
        kind==1 ? *GP(object+0x220) : gr32(object+0x320)) : 0;
    const u64 count=__atomic_add_fetch(&frame_timer_calls[kind],1,__ATOMIC_RELAXED);
    frame_timer_original[kind](c,entry);
    if (!valid || !readable(object,field+4)) return;
    const u32 after=gr32(object+field);
    if (before==after) return;
    const u64 changed=__atomic_add_fetch(&frame_timer_changes[kind],1,__ATOMIC_RELAXED);
    if (changed>8 && changed%120) return;
    double a=(int)before, b=(int)after;
    if (kind>=2) { float x,y; memcpy(&x,&before,4); memcpy(&y,&after,4); a=x; b=y; }
    LOG("arceus-frame-timer: kind=%u count=%llu changed=%llu ns=%llu object=%llx caller=main+%llx state=%u before=%g after=%g\n",
        kind,(unsigned long long)count,(unsigned long long)changed,(unsigned long long)plat_ns(),
        (unsigned long long)object,(unsigned long long)(caller-main_base),state,a,b);
}
static void recenter_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,0); }
static void battle_idle_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,1); }
static void throwable_one_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,2); }
static void throwable_two_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,3); }
static void throwable_three_timer_probe(CPU* c,u64 e) { frame_timer_probe(c,e,4); }
static void frame_timer_research_init(void) {
    static const struct { u64 offset; u32 size,head,patch,word,replacement,extra,extra_word; } sites[5]={
        /* MOV Xd,X0 -> ADD Xd,X0,#0: equivalent, flags unchanged. */
        {0x193633c,0x3c8,0xd10343ff,0x3c,0xaa0003f3,0x91000013,0x110,0x11000529},
        {0x9bad68,0x4dc,0xd10203ff,0xcc,0xaa0003f5,0x91000015,0x2c,0x71000508},
        {0x1d482b0,0xe94,0xd10483ff,0xc4,0xaa0003f4,0x91000014,0x68c,0x1e212800},
        {0x1d49144,0x770,0xd10283ff,0x58,0xaa0003f3,0x91000013,0x704,0x1e212800},
        {0x1d498b4,0x7dc,0xd102c3ff,0xe8,0xaa0003f3,0x91000013,0x654,0x1e212800}
    };
    const GuestFn wrappers[5]={recenter_timer_probe,battle_idle_timer_probe,
        throwable_one_timer_probe,throwable_two_timer_probe,throwable_three_timer_probe};
    for (unsigned k=0;k<5;k++) {
        const u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0;i<g_mod->nfns;i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
            gr32(pc+sites[k].extra)!=sites[k].extra_word) {
            WARN("arceus-frame-timer: refused instruction/function guard kind=%u\n",k);continue;
        }
        gw32(pc+sites[k].patch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        frame_timer_original[k]=jit_wait(pc);
        if (!frame_timer_original[k]) {
            WARN("arceus-frame-timer: compile failed kind=%u\n",k);continue;
        }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-frame-timer: observation probe installed kind=%u\n",k);
    }
}
/* Common fades and capture stages have separate integer clocks. Capture's X0
   is its manager; X1 points to the holder of the actual sequence object.
   State transitions can reset/reload these clocks, so log both sides. */
static GuestFn sequence_clock_original[2];
static u64 sequence_clock_calls[2], sequence_clock_changes[2];
static void sequence_clock_probe(CPU* c, u64 entry, unsigned kind) {
    const u64 object = kind ? (readable(c->x[1], 8) ? gr64(c->x[1]) : 0) : c->x[0];
    const u64 caller = c->x[30];
    const u32 extent = kind ? 0x1e0 : 0x540;
    const u32 timer = kind ? 0x1dc : 0x538, duration = kind ? 0x1d8 : 0x53c;
    const u32 state = kind ? 0xf0 : 0x504;
    const int valid = readable(object, extent);
    const u32 before = valid ? gr32(object + timer) : 0;
    const u32 state_before = valid ? gr32(object + state) : 0;
    const u32 duration_before = valid ? gr32(object + duration) : 0;
    const u32 mode_before = valid && !kind ? gr32(object + 0x500) : 0;
    const u64 count = __atomic_add_fetch(&sequence_clock_calls[kind], 1, __ATOMIC_RELAXED);
    const u64 now = valid ? plat_ns() : 0;
    sequence_clock_original[kind](c, entry);
    if (!valid || c->pc != caller || !readable(object, extent)) return;
    const u32 after = gr32(object + timer), state_after = gr32(object + state);
    const u32 duration_after = gr32(object + duration);
    const u32 mode_after = !kind ? gr32(object + 0x500) : 0;
    const int transition = state_before != state_after || duration_before != duration_after || mode_before != mode_after;
    const int changed = before != after || transition;
    const u64 changes = changed ? __atomic_add_fetch(&sequence_clock_changes[kind], 1, __ATOMIC_RELAXED) : 0;
    if (!(transition || count <= 8 || (changed && (changes <= 8 || changes % 30 == 0)) || count % 600 == 0)) return;
    LOG("arceus-sequence-clock: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx transition=%u state_before=%u state_after=%u mode_before=%u mode_after=%u duration_before=%u duration_after=%u before=%u after=%u\n",
        kind, (unsigned long long)count, (unsigned long long)now,
        (unsigned long long)object, (unsigned long long)(caller-main_base), transition,
        state_before, state_after, mode_before, mode_after, duration_before, duration_after, before, after);
}
static void fade_sequence_probe(CPU* c, u64 e) { sequence_clock_probe(c, e, 0); }
static void capture_sequence_probe(CPU* c, u64 e) { sequence_clock_probe(c, e, 1); }
static void sequence_clock_research_init(void) {
    static const struct { u64 offset; u32 size,head,patch,word,replacement,extra,extra_word; } sites[2]={
        /* Equivalent MOV Xd,X0 -> ADD Xd,X0,#0 selects an independent JIT body. */
        {0xc21490,0x300,0xd10143ff,0x3c,0xaa0003f3,0x91000013,0xd0,0x1100054a},
        {0x180c338,0x427c,0x6db82beb,0x30,0xaa0003f6,0x91000016,0x61c,0x71000508}
    };
    const GuestFn wrappers[2]={fade_sequence_probe,capture_sequence_probe};
    for (unsigned k=0;k<2;k++) {
        const u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0;i<g_mod->nfns;i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
            gr32(pc+sites[k].extra)!=sites[k].extra_word) {
            WARN("arceus-sequence-clock: refused instruction/function guard kind=%u\n",k);continue;
        }
        gw32(pc+sites[k].patch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        sequence_clock_original[k]=jit_wait(pc);
        if (!sequence_clock_original[k]) {
            WARN("arceus-sequence-clock: compile failed kind=%u\n",k);continue;
        }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-sequence-clock: observation probe installed kind=%u\n",k);
    }
}
static void timing_research_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    if (!main_base || !enabled || strcmp(enabled,"1")) return;
    static const struct { u64 offset; u32 size, head, branch, word, replacement; } sites[2] = {
        {0x2795204,0x928,0xd10283ff,0x924,0x17ffff7b,0xb4ffef7f},
        {0x2af25b4,0x6b8,0xfc190fe8,0x6a8,0x17ffff40,0xb4ffe81f}
    };
    const GuestFn wrappers[2] = {navigation_probe, physics_probe};
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    research_observing = 1;
    engine_clock_research_init();
    frame_timer_research_init();
    sequence_clock_research_init();
    for (unsigned k=0; k<2; k++) {
        u64 pc=main_base+sites[k].offset;
        int covered=0;
        for (u32 i=0; i<g_mod->nfns; i++)
            if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
        if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
            gr32(pc)!=sites[k].head || gr32(pc+sites[k].branch)!=sites[k].word) {
            WARN("arceus-timing-research: refused guard kind=%u\n", k); continue;
        }
        gw32(pc+sites[k].branch,sites[k].replacement);
        jit_code_changed(pc,sites[k].size);
        timing_research_original[k]=jit_wait(pc);
        if (!timing_research_original[k]) { WARN("arceus-timing-research: compile failed kind=%u\n",k); continue; }
        __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
        LOG("arceus-timing-research: read-only probe installed kind=%u\n",k);
    }
}

/* Arceus 1.1.1's 3D resolution governor (3DBufferWidthScale/HeightScale).
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

void arceus_clock_post_jit(void) {
    fixed_resolution_init();
    timing_research_init();
    const char* probe = getenv("SNX_ARCEUS_CAMERA_TRACE");
    if (main_base && !experimental && probe && !strcmp(probe, "1")) {
        camera_trace_only = 1;
        camera_input_init();
        camera_rate_init();
        return;
    }
    const char* enabled = getenv("SNX_ARCEUS_TIMING_FIXES");
    if (!enabled || strcmp(enabled, "1") || !clocks_installed) return;
    water_fix_init();
    effects_fix_init();
    event_camera_init();
    blink_fix_init();
    pad_repeat_init();
    camera_input_init();
    camera_rate_init();
    field_message_probe_init();
}

