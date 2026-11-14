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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GuestFn originals[3];
static u64 counts[3];
static u64 main_base;
static int experimental, tracing, research_observing;
static int timing_enabled;
static int target_fps_explicit;
static u64 ui_clock_research_calls;
static ArceusStep step;
static PMutex step_lock = PMUTEX_INIT;
static GuestFn message_original;
static u64 message_count;
static u64 water_step_address;
static int clocks_installed;
/* Optional catch-up for integer timers. Bound recovery to one second after sustained slow frames. */
#define TIMER_CARRY_LIMIT_NS UINT64_C(1000000000)
static int timer_carry_fix;
/* Nanoseconds below one whole step (ARCEUS_WHOLE_STEP_NS), not seconds. */
static u64 blink_remainder;
static unsigned blink_ticks;
/* Shared 30-Hz cadence for the isolated integer blink state machine and the shot blend. The
   game's own step is one tick and leaves no part of one behind. */
static void blink_cadence_add(u64 ns) {
    blink_remainder = ns == ARCEUS_WHOLE_STEP_NS ? ns : blink_remainder + ns;
    blink_ticks = (unsigned)(blink_remainder / ARCEUS_WHOLE_STEP_NS);
    blink_remainder %= ARCEUS_WHOLE_STEP_NS;
}

typedef struct {
    int master, invalid, mode, target_fps, write_fps, runtime_rate_override, skip_runtime_limit;
    u64 initial_ns;
} ArceusClockConfig;

static int arceus_switch_enabled(const char* name, int default_on) {
    const char* value = getenv(name);
    return value ? !strcmp(value, "1") : default_on;
}
static unsigned long movement_mask_value(void) {
    const char* value = getenv("SNX_ARCEUS_MOVEMENT_SITES");
    if (!value) return 0x5fcul;
    return *value ? strtoul(value, NULL, 16) : ~0ul;
}
static int climb_step_value(void) {
    const char* value = getenv("SNX_ARCEUS_CLIMB_STEP");
    if (!value) return 2;
    return !strcmp(value, "1") ? 1 : !strcmp(value, "2") ? 2 : 0;
}
static float camera_speed_value(int* invalid) {
    const char* speed = getenv("SNX_ARCEUS_CAMERA_SPEED");
    if (invalid) *invalid = 0;
    if (!speed || !*speed) return 1.0f;
    char* end;
    const float value = strtof(speed, &end);
    if (*end || !isfinite(value) || value < 0.25f || value > 4.0f) {
        if (invalid) *invalid = 1;
        return 1.0f;
    }
    return value;
}
static u64 initial_step_for_limit(int fps) {
    if (fps == 0) return ARCEUS_UNLOCKED_INITIAL_STEP_NS;
    if (fps == SNX_FPS_GAME) fps = 30;
    if (fps < 1 || fps > 1000) return ARCEUS_INITIAL_STEP_NS;
    return (UINT64_C(1000000000) + (u64)fps / 2) / (u64)fps;
}
static ArceusClockConfig arceus_clock_config(int runtime_fps) {
    ArceusClockConfig config = {.master = arceus_switch_enabled("SNX_ARCEUS_TIMING_FIXES", 1)};
    if (!config.master) return config;
    const char* legacy = getenv("SNX_ARCEUS_FPS60");
    const char* target = getenv("SNX_ARCEUS_TARGET_FPS");
    config.mode = 0;
    if (legacy && !strcmp(legacy, "1")) config.mode = 1, config.target_fps = 60, config.write_fps = 1;
    if (target && *target) {
        if (!strcmp(target, "60") || !strcmp(target, "120"))
            config.mode = 1, config.target_fps = atoi(target), config.write_fps = 1;
        else if (!strcmp(target, "unlocked") || !strcmp(target, "0"))
            config.mode = 2, config.target_fps = 0, config.write_fps = 1;
        else if (!strcmp(target, "game"))
            /* The runtime's own limit stays, even with the legacy SNX_ARCEUS_FPS60=1. */
            config.mode = 0, config.target_fps = 0, config.write_fps = 0, config.runtime_rate_override = 1;
        else config.invalid = 1;
    }
    config.skip_runtime_limit = !config.invalid && !config.write_fps &&
        !config.runtime_rate_override &&
        (runtime_fps == SNX_FPS_GAME || (runtime_fps > 0 && runtime_fps <= 30));
    if (!config.invalid)
        config.initial_ns = config.mode == 2 ? ARCEUS_UNLOCKED_INITIAL_STEP_NS
                            : config.mode == 1 ? initial_step_for_limit(config.target_fps)
                                               : initial_step_for_limit(runtime_fps);
    return config;
}
static void arceus_clock_config_apply(ArceusClockConfig config) {
    if (!config.master || config.invalid || config.skip_runtime_limit) return;
    /* On by default: a measured step near 1/30 s is reported as the game's own step. */
    step.measured_only = !arceus_switch_enabled("SNX_ARCEUS_WHOLE_STEP", 1);
    step.initial_ns = step.measured_only ? config.initial_ns : arceus_whole_step(config.initial_ns);
    if (config.write_fps) __atomic_store_n(&g_opt_fps, config.target_fps, __ATOMIC_RELAXED);
    experimental = 1;
    clocks_installed = timing_enabled = 1;
}

static void water_step_write(u64 ns) {
    if (!water_step_address) return;
    float seconds = (float)((double)ns / 1000000000.0);
    u32 bits;
    memcpy(&bits, &seconds, sizeof bits);
    gw32(water_step_address, bits);
}

/* Guest cell for world_timers_init: +0 holds min(1/30 s, frame step) and +4 its
   negation. Patched sites load it in place of their authored +/-1/30 constants. */
#define NOMINAL_STEP_SECONDS (1.0f / 30.0f)
static u64 frame_step_address;
/* One per main-loop frame: an engine frame identity for per-frame pairing. */
static u64 frame_step_frames;
/* The cell's words: the step in seconds, its negative, and three derived words. */
#define FRAME_STEP_CELL_WORDS 6
static void frame_step_cell_write(float seconds) {
    const float negative = -seconds;
    u32 bits;
    memcpy(&bits, &seconds, sizeof bits);
    gw32(frame_step_address, bits);
    memcpy(&bits, &negative, sizeof bits);
    gw32(frame_step_address + 4, bits);
    /* Derived words for the sky cloud fix: -10/step at +8, pi at +16 and 1/(30 x step) at +20.
       At the game's own step they are the game's constants, -300 and 1, so its results are
       the same bit for bit. */
    const float derived[3] = {seconds == NOMINAL_STEP_SECONDS ? -300.0f : -10.0f / seconds, 3.14159274f,
                              1.0f / (30.0f * seconds)};
    const u32 at[3] = {8, 16, 20};
    for (unsigned k = 0; k < 3; k++) {
        memcpy(&bits, &derived[k], sizeof bits);
        gw32(frame_step_address + at[k], bits);
    }
}
static void frame_step_write(u64 ns) {
    if (!frame_step_address) return;
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
    float seconds = ns ? (float)((double)ns / 1000000000.0) : NOMINAL_STEP_SECONDS;
    if (!(seconds < NOMINAL_STEP_SECONDS)) seconds = NOMINAL_STEP_SECONDS;
    frame_step_cell_write(seconds);
}

static void battle_sequence_dispatch_watch(u64 now);
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
            battle_sequence_dispatch_watch(plat_ns());
            water_step_write(value);
            frame_step_write(value);
            blink_cadence_add(value);
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
    const ArceusClockConfig config = arceus_clock_config(__atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED));
    timing_enabled = clocks_installed = experimental = tracing = 0;
    target_fps_explicit = config.write_fps;
    if (!config.master) return;
    if (config.invalid) {
        WARN("arceus-clock: refused unsupported target FPS (use 60, 120 or unlocked)\n");
        return;
    }
    if (config.skip_runtime_limit) {
        LOG("arceus-clock: timing layer skipped at fps_limit=%d (game pace or 30 FPS and below)\n",
            __atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED));
        return;
    }
    const char* enabled = getenv("SNX_ARCEUS_CLOCK_TRACE");
    const char* message_flag = getenv("SNX_ARCEUS_DIALOGUE_TRACE");
    int message_trace = message_flag && !strcmp(message_flag, "1");
    const SnxModInfo* mod = NULL;
    tracing = enabled && !strcmp(enabled, "1");
    experimental = 1;
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
    arceus_clock_config_apply(config);
    if (config.mode == 2)
        LOG("arceus-clock: Arceus 1.1.1 EXPERIMENT: elapsed-time clocks, target_fps=unlocked, tracing=%d\n", tracing);
    else if (config.mode == 0 && config.runtime_rate_override)
        LOG("arceus-clock: Arceus 1.1.1 EXPERIMENT: elapsed-time clocks, target_fps=game, fps_limit=%d, tracing=%d\n",
            __atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED), tracing);
    else if (config.mode == 0)
        LOG("arceus-clock: Arceus 1.1.1 EXPERIMENT: elapsed-time clocks, fps_limit=%d, tracing=%d\n",
            __atomic_load_n(&g_opt_fps, __ATOMIC_RELAXED), tracing);
    else
        LOG("arceus-clock: Arceus 1.1.1 %s, target_fps=%d, tracing=%d\n",
            "EXPERIMENT: elapsed-time clocks", config.target_fps, tracing);
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
    if ( !arceus_switch_enabled("SNX_ARCEUS_EVENT_CAMERA_FIX", 1)) return;
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
    if ( !arceus_switch_enabled("SNX_ARCEUS_BLINK_FIX", 1)) return;
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
    if (!arceus_switch_enabled("SNX_ARCEUS_CAMERA_INPUT_FIX", 1) && !camera_trace_only) return;
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
    if (!arceus_switch_enabled("SNX_ARCEUS_CAMERA_INPUT_FIX", 1) && !camera_trace_only) return;
    int invalid_speed;
    camera_speed = camera_speed_value(&invalid_speed);
    if (invalid_speed) WARN("arceus-camera-rate: invalid camera speed, using 1\n");
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
    if ( !arceus_switch_enabled("SNX_ARCEUS_MENU_REPEAT_FIX", 1)) return;
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
static int physics_timestep_active, navigation_timestep_active;
static u32 physics_input_bits; /* Last main-world elapsed input, research probe only. */
static void cadence_census_report(u64 frames);
static void timing_research_update(CPU* c, u64 entry, unsigned kind) {
    u64 object = c->x[0], caller = c->x[30];
    u64 count = __atomic_add_fetch(&timing_research_calls[kind], 1, __ATOMIC_RELAXED);
    if (kind) {
        __atomic_store_n(&physics_input_bits, c->v[0].s[0], __ATOMIC_RELAXED);
        cadence_census_report(count);
    }
    if ((count <= 4 || count % 300 == 0) && readable(object, 0x154)) {
        unsigned mode = gr32(object + (kind ? 0x120 : 0x150));
        mode = kind ? (mode >> 8) & 255 : mode & 255;
        /* The timestep correction leaves the guest's +121 byte alone, so
           variable_mode stays 0; timestep_fix records which dt was selected. */
        LOG("arceus-timing-research: kind=%u count=%llu ns=%llu object=%llx caller=main+%llx incoming_dt=%g variable_mode=%u rate=%g timestep_fix=%d\n",
            kind, (unsigned long long)count, (unsigned long long)plat_ns(),
            (unsigned long long)object, (unsigned long long)(caller-main_base),
            (double)c->v[0].f[0], mode, kind ? (double)guest_float(object+0x124) : 1.0,
            kind ? physics_timestep_active : navigation_timestep_active);
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
        if (readable(object,0x650)) {
            /* StepInfo+0x88 is the world's post-solve serial. Its modular
               progress can distinguish setup frequency from completed steps;
               it is not a body count or a lifetime identity. Observation only. */
            LOG("arceus-physics-solver: count=%llu ns=%llu object=%llx input_seconds=%g stored_seconds=%g inverse_seconds=%g substep_seconds=%g substeps=%u alternate_world=%u postsolve_serial=%u\n",
                (unsigned long long)count, (unsigned long long)now, (unsigned long long)object,
                (double)step, (double)guest_float(object+0x540), (double)guest_float(object+0x548),
                (double)guest_float(object+0x568), gr32(object+0x5a0), gr64(object+0x648)!=0,
                gr32(object+0x5b8));
            /* The single-threaded implementation solves inline before checking
               task-list counts. Empty queues therefore do not establish that
               physics was skipped. Identify the implementation without adding
               a solver hook or treating setup as completed integration. */
            const u64 simulation=gr64(object+0x4a8);
            if (readable(simulation,0x20)) {
                const u64 vt=gr64(simulation);
                const u64 solve=readable(vt,0x20) ? gr64(vt+0x18) : 0;
                const u32 tag=gr32(simulation+0x18);
                const char* path=vt==main_base+0x41b5000 && solve==main_base+0x2fe2604 && tag==0 ? "single" :
                    vt==main_base+0x41b3ac8 && solve==main_base+0x2f9158c && tag==1 ? "multi" : "unknown";
                LOG("arceus-physics-simulation: count=%llu ns=%llu object=%llx simulation=%llx vtable=%llx solve_entry=%llx tag=%u path=%s alternate_world=%u\n",
                    (unsigned long long)count,(unsigned long long)now,(unsigned long long)object,
                    (unsigned long long)simulation,(unsigned long long)vt,(unsigned long long)solve,
                    tag,path,gr64(object+0x648)!=0);
            }
        }
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
/* Common fade C21490, mode 0 (linear color). Each active call adds one to frame +538 and the
   delta +550 to color +540, so fades ran 4x fast at 120 FPS. With SNX_ARCEUS_FADE_FIX a call
   adds delta x elapsed x 30, and the frame only advances once a whole 1/30 s has built up,
   by staging +538 one lower. Completion, snaps and callbacks stay the original's. */
static int writable(u64 address, u64 size);
#define FADE_SLOTS 16
static int fade_fix_active;
static PMutex fade_lock = PMUTEX_INIT;
/* generation counts the fade starts seen for the object, so a call that a start interrupted
   leaves the new fade's history and delta alone. */
static struct { u64 object; float progress; u32 frame, duration, state, generation; } fade_slots[FADE_SLOTS];
static u64 fade_calls, fade_staged, fade_full;
typedef struct { u64 object; int slot, staged_delta; u32 delta[4], staged[4], generation; float progress; } FadeStage;
static float fade_word_float(u32 bits) {
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
/* The object's slot or a free one, -1 when all are in use (the call then runs as it is).
   A slot that no longer matches the fade (start, completion, other writer) starts at zero. */
static int fade_slot(u64 object, u32 frame, u32 duration, u32 state, float* progress) {
    int found = -1, free_slot = -1;
    for (int k = 0; k < FADE_SLOTS && found < 0; k++) {
        if (fade_slots[k].object == object) found = k;
        if (!fade_slots[k].object && free_slot < 0) free_slot = k;
    }
    if (found < 0) {
        if (free_slot < 0) return -1;
        found = free_slot;
        fade_slots[found].object = object;
        fade_slots[found].progress = 0.0f;
        fade_slots[found].frame = ~0u;
        fade_slots[found].generation = 0;
    }
    if (fade_slots[found].frame != frame || fade_slots[found].duration != duration ||
        fade_slots[found].state != state)
        fade_slots[found].progress = 0.0f;
    *progress = fade_slots[found].progress;
    return found;
}
static void fade_before(u64 object, FadeStage* st) {
    st->slot = -1;
    st->staged_delta = 0;
    if (!fade_fix_active || !writable(object, 0x570) || gr32(object + 0x500) != 0) return;
    const u32 state = gr32(object + 0x504);
    if (state != 2 && state != 3) return;
    /* The cell holds min(elapsed, 1/30), so want is at most 1. */
    const float want = guest_float(frame_step_address) / NOMINAL_STEP_SECONDS;
    if (!(want > 0.0f && want <= 1.0f)) return;
    const int32_t frame = (int32_t)gr32(object + 0x538), duration = (int32_t)gr32(object + 0x53c);
    /* A fade-in at its end only resets and republishes the target: leave that call alone.
       The object goes idle in it, so free its slot here, as no later call will. */
    if (frame >= duration && state == 2) {
        pm_lock(&fade_lock);
        for (int k = 0; k < FADE_SLOTS; k++)
            if (fade_slots[k].object == object) fade_slots[k].object = 0;
        pm_unlock(&fade_lock);
        return;
    }
    pm_lock(&fade_lock);
    float progress = 0.0f;
    st->slot = fade_slot(object, (u32)frame, (u32)duration, state, &progress);
    if (st->slot >= 0) st->generation = fade_slots[st->slot].generation;
    else fade_full++;
    pm_unlock(&fade_lock);
    if (st->slot < 0) return;
    st->object = object;
    st->progress = progress + want;
    const int whole = st->progress >= 1.0f;
    for (unsigned k = 0; k < 4; k++) st->delta[k] = gr32(object + 0x550 + 4 * k);
    if (frame < duration) {
        /* Each call adds its share of the delta, only up to the end on the last frame.
           Only a call that completes a frame advances it. */
        const float share = whole && frame + 1 >= duration ? 1.0f - progress : want;
        for (unsigned k = 0; k < 4; k++) {
            const float scaled = fade_word_float(st->delta[k]) * share;
            memcpy(&st->staged[k], &scaled, 4);
        }
        if (!whole) gw32(object + 0x538, (u32)(frame - 1));
    } else {
        /* Fade-out finishes one frame after its last: hold it with a zero step until then. */
        if (whole) return;
        memset(st->staged, 0, sizeof st->staged);
        gw32(object + 0x538, (u32)(duration - 1));
    }
    for (unsigned k = 0; k < 4; k++) gw32(object + 0x550 + 4 * k, st->staged[k]);
    st->staged_delta = 1;
    __atomic_add_fetch(&fade_staged, 1, __ATOMIC_RELAXED);
}
static void fade_after(const FadeStage* st) {
    if (st->slot < 0) return;
    const u64 object = st->object;
    const u64 calls = __atomic_add_fetch(&fade_calls, 1, __ATOMIC_RELAXED);
    pm_lock(&fade_lock);
    const int current = fade_slots[st->slot].object == object &&
                        fade_slots[st->slot].generation == st->generation;
    pm_unlock(&fade_lock);
    /* A fade start during the call owns the object now: no restore, no history. */
    if (!current || !writable(object, 0x570)) return;
    if (st->staged_delta) {
        /* Restore the authored delta unless the call (or a callback) replaced it. */
        int ours = 1;
        for (unsigned k = 0; k < 4; k++)
            if (gr32(object + 0x550 + 4 * k) != st->staged[k]) ours = 0;
        if (ours)
            for (unsigned k = 0; k < 4; k++) gw32(object + 0x550 + 4 * k, st->delta[k]);
    }
    const u32 frame = gr32(object + 0x538), duration = gr32(object + 0x53c), state = gr32(object + 0x504);
    pm_lock(&fade_lock);
    if (fade_slots[st->slot].object == object && fade_slots[st->slot].generation == st->generation) {
        if (state != 2 && state != 3) {
            fade_slots[st->slot].object = 0; /* Finished: the slot is free again. */
        } else {
            fade_slots[st->slot].progress = st->progress >= 1.0f ? st->progress - 1.0f : st->progress;
            fade_slots[st->slot].frame = frame;
            fade_slots[st->slot].duration = duration;
            fade_slots[st->slot].state = state;
        }
    }
    pm_unlock(&fade_lock);
    if (calls <= 4 || calls % 1200 == 0)
        LOG("arceus-fade: calls=%llu staged=%llu full=%llu object=%llx frame=%d duration=%d state=%u progress=%g\n",
            (unsigned long long)calls, (unsigned long long)__atomic_load_n(&fade_staged, __ATOMIC_RELAXED),
            (unsigned long long)fade_full, (unsigned long long)object, (int)frame, (int)duration, state,
            (double)st->progress);
}
/* Common fades and capture stages have separate integer clocks. Capture's X0
   is its manager; X1 points to the holder of the actual sequence object.
   State transitions can reset/reload these clocks, so log both sides. */
static GuestFn sequence_clock_original[2];
static u64 sequence_clock_calls[2], sequence_clock_changes[2];
static int sequence_clock_logging, capture_fix_active;
/* Capture waiting stages 3, 5, 7, 8, 9, 14 and 16 subtract one from +1DC per call.
   With the fix, a call only counts down once 1/30 s of frame steps has built up for
   that object. Otherwise +1DC is raised by one first so the call leaves it unchanged. */
#define CAPTURE_SLOTS 8
#define CAPTURE_TICK_NS ((int64_t)ARCEUS_WHOLE_STEP_NS)
static struct { u64 object; int64_t ns; } capture_slots[CAPTURE_SLOTS];
static unsigned capture_next_slot;
static int capture_waiting(u32 stage) {
    return stage == 3 || stage == 5 || stage == 7 || stage == 8 || stage == 9 || stage == 14 || stage == 16;
}
/* The director skips its body until every enabled effect handle pair is ready, the same
   checks as 180C558 to 180C614. Their start-up takes a fixed number of frames. */
static int capture_ready(u64 object) {
    static const u32 flags[4] = {0x210, 0x248, 0x280, 0x2b8}, pairs[4] = {0x1e0, 0x218, 0x250, 0x288};
    if (!readable(object, 0x2c0)) return 1;
    for (unsigned k = 0; k < 4; k++) {
        if (!*GP(object + flags[k])) continue;
        for (u32 h = pairs[k]; h <= pairs[k] + 0x10; h += 0x10) {
            const u64 p = gr64(object + h);
            if (!p || !readable(p + 0xc, 4) || !gr32(p + 0xc)) return 0;
        }
    }
    return 1;
}
/* A not-ready call counts as one whole 1/30 s frame, as it takes at 30 FPS, so the
   countdown afterwards waits until elapsed time catches up. The debt is capped at 1 s. */
static int capture_tick(u64 object, int ready) {
    pm_lock(&step_lock);
    const int64_t ns = (int64_t)(step.step ? step.step : step.initial_ns);
    pm_unlock(&step_lock);
    unsigned k = 0;
    while (k < CAPTURE_SLOTS && capture_slots[k].object != object) k++;
    if (k == CAPTURE_SLOTS) {
        k = capture_next_slot++ % CAPTURE_SLOTS;
        capture_slots[k].object = object;
        capture_slots[k].ns = 0;
    }
    capture_slots[k].ns += ready ? ns : ns - CAPTURE_TICK_NS;
    if (capture_slots[k].ns < -1000000000ll) capture_slots[k].ns = -1000000000ll;
    if (!ready || capture_slots[k].ns < CAPTURE_TICK_NS) return 0;
    capture_slots[k].ns -= CAPTURE_TICK_NS;
    if (timer_carry_fix) {
        if (capture_slots[k].ns > (int64_t)TIMER_CARRY_LIMIT_NS)
            capture_slots[k].ns = (int64_t)TIMER_CARRY_LIMIT_NS;
    } else if (capture_slots[k].ns >= CAPTURE_TICK_NS) capture_slots[k].ns = 0;
    return 1;
}
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
    const int hold = kind && capture_fix_active && valid && before && capture_waiting(state_before) &&
                     !capture_tick(object, capture_ready(object));
    if (hold) gw32(object + timer, before + 1);
    FadeStage fade;
    fade.slot = -1;
    if (!kind && valid) fade_before(object, &fade);
    sequence_clock_original[kind](c, entry);
    if (!kind) fade_after(&fade);
    if (hold && readable(object, extent) && gr32(object + state) == state_before &&
        gr32(object + timer) == before + 1)
        gw32(object + timer, before);
    if (!sequence_clock_logging || !valid || c->pc != caller || !readable(object, extent)) return;
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
static int sequence_clock_install(unsigned k) {
    static const struct { u64 offset; u32 size,head,patch,word,replacement,extra,extra_word; } sites[2]={
        /* Equivalent MOV Xd,X0 -> ADD Xd,X0,#0 selects an independent JIT body. */
        {0xc21490,0x300,0xd10143ff,0x3c,0xaa0003f3,0x91000013,0xd0,0x1100054a},
        {0x180c338,0x427c,0x6db82beb,0x30,0xaa0003f6,0x91000016,0x61c,0x71000508}
    };
    const GuestFn wrappers[2]={fade_sequence_probe,capture_sequence_probe};
    if (sequence_clock_original[k]) return 1;
    const u64 pc=main_base+sites[k].offset;
    int covered=0;
    for (u32 i=0;i<g_mod->nfns;i++)
        if (g_mod->fn_lo[i]==pc && g_mod->fn_len[i]==sites[k].size) covered=1;
    if (!covered || pc<g_code_lo || pc+sites[k].size>g_code_hi || !readable(pc,sites[k].size) ||
        gr32(pc)!=sites[k].head || gr32(pc+sites[k].patch)!=sites[k].word ||
        gr32(pc+sites[k].extra)!=sites[k].extra_word) {
        WARN("arceus-sequence-clock: refused instruction/function guard kind=%u\n",k);return 0;
    }
    gw32(pc+sites[k].patch,sites[k].replacement);
    jit_code_changed(pc,sites[k].size);
    sequence_clock_original[k]=jit_wait(pc);
    if (!sequence_clock_original[k]) {
        WARN("arceus-sequence-clock: compile failed kind=%u\n",k);return 0;
    }
    __atomic_store_n(&g_disp[(pc-g_code_lo)/4],wrappers[k],__ATOMIC_RELEASE);
    return 1;
}
static void sequence_clock_research_init(void) {
    sequence_clock_logging = 1;
    for (unsigned k=0;k<2;k++)
        if (sequence_clock_install(k)) LOG("arceus-sequence-clock: observation probe installed kind=%u\n",k);
}
/* Runs after the research probes, so it reuses the capture wrapper when they installed it. */
static void capture_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_CAPTURE_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-capture-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!sequence_clock_install(1)) return;
    capture_fix_active = 1;
    LOG("arceus-capture-fix: EXPERIMENT: capture waiting stages count down at 30 per second of elapsed time\n");
}
/* Battle effect sequences (the .wbin player, update 9DBEAC) add their speed at +F0 to the
   frame at +E8 once per call, so they ran 4 times fast at 120 FPS. The fix passes speed x 30
   x elapsed, cut so the frame lands exactly on each whole frame for the exact command-end
   compares. Speeds outside (0, 4] only update once 1/30 s has built up. */
#define SEQUENCE_SLOTS 64
/* A clamped 100 ms step at speed 4 needs at most 13 boundary updates.
   Allow extra catch-up work, retaining excess budget if the guard is reached. */
static GuestFn battle_sequence_original;
static PMutex battle_sequence_lock = PMUTEX_INIT;
/* cut: the sequence has had a step that was not the game's own, so its frame may be cut. */
/* calls, start_ns and first follow one sequence from its first playing call to its end. */
static struct { u64 object; u64 ns; float pending; int cut; u32 calls; u64 start_ns; float first; }
    sequence_slots[SEQUENCE_SLOTS];
static u64 battle_sequence_ended;
static unsigned sequence_next_slot;
static u64 battle_sequence_calls, battle_sequence_skips, battle_sequence_partial;
/* Read-only research: playing calls per speed, to see how many sequences still step whole frames. */
#define SEQUENCE_SPEEDS 8
static struct { float speed; u64 calls; } sequence_speeds[SEQUENCE_SPEEDS];
static u64 sequence_speed_other;
/* The largest step not past the next whole frame, nudged so frame + step rounds onto it. */
static float sequence_step(float frame, float want) {
    const float whole = floorf(frame) + 1.0f;
    if (frame + want < whole) return want;
    float step = whole - frame;
    for (unsigned i = 0; i < 4 && frame + step != whole; i++)
        step = nextafterf(step, frame + step < whole ? INFINITY : 0.0f);
    return step;
}
/* The even landing, the default (SNX_ARCEUS_BATTLE_SEQUENCE_FIX unset or 2). Value 1 keeps
   the plain landing for comparison. The rule above cuts a step at the next
   whole frame and keeps the rest for the next call. At 60 FPS, where a step is half a frame,
   that gave the frame 8.50, 8.99, 9.00, 10.00: a hold and a jump on every second rendered
   frame, which the battle camera shows (its commands are stepped from +E8 by 9DB748). With
   value 2 a step that would end within half an advance before a whole frame goes onto it
   and the difference is carried, signed. Nothing else changes: a step still ends on every
   whole frame and the rest waits for the next rendered frame. The game needs both. The
   commands' step slot (9DB748, once per rendered frame) acts on exact frames it must see,
   and the update makes its end checks when it is entered on one. Two earlier forms moved on
   from the whole frame in the same rendered frame and broke one or the other. */
static int battle_sequence_even;
#define SEQUENCE_SNAP_SHARE 0.5f
#define SEQUENCE_SNAP_MAX 0.5f
/* The step from frame for a budget of want: onto the next whole frame when the budget
   reaches it or ends within snap before it, else the budget itself. */
static float sequence_even_step(float frame, float want, float snap) {
    const float whole = floorf(frame) + 1.0f;
    if (frame + want < whole - snap) return want;
    return sequence_step(frame, whole - frame + 1.0f);
}
static int battle_sequence_playing(u64 o) {
    return readable(o, 0x308) && *GP(o + 0x302) && *GP(o + 0x301) && *GP(o + 0x303) &&
           guest_float(o + 0xf0) != 0.0f && guest_float(o + 0xe8) < guest_float(o + 0xec);
}
static void battle_sequence_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const u64 count = __atomic_add_fetch(&battle_sequence_calls, 1, __ATOMIC_RELAXED);
    int skip = 0;
    float speed = 0.0f, partial = 0.0f, snap = 0.0f;
    unsigned slot = SEQUENCE_SLOTS;
    if (battle_sequence_playing(object)) {
        pm_lock(&step_lock);
        const u64 ns = step.step ? step.step : step.initial_ns;
        pm_unlock(&step_lock);
        speed = guest_float(object + 0xf0);
        pm_lock(&battle_sequence_lock);
        unsigned s = 0;
        while (s < SEQUENCE_SPEEDS && sequence_speeds[s].calls && sequence_speeds[s].speed != speed) s++;
        if (s == SEQUENCE_SPEEDS) sequence_speed_other++;
        else sequence_speeds[s].speed = speed, sequence_speeds[s].calls++;
        unsigned k = 0;
        while (k < SEQUENCE_SLOTS && sequence_slots[k].object != object) k++;
        if (k == SEQUENCE_SLOTS) {
            k = sequence_next_slot++ % SEQUENCE_SLOTS;
            sequence_slots[k].object = object;
            sequence_slots[k].ns = 0;
            sequence_slots[k].pending = 0.0f;
            sequence_slots[k].calls = 0;
            /* A sequence first seen between two whole frames was cut before. */
            sequence_slots[k].cut = guest_float(object + 0xe8) != floorf(guest_float(object + 0xe8));
        }
        if (!sequence_slots[k].calls++) {
            sequence_slots[k].start_ns = plat_ns();
            sequence_slots[k].first = guest_float(object + 0xe8);
        }
        slot = k;
        if (ns != ARCEUS_WHOLE_STEP_NS) sequence_slots[k].cut = 1;
        /* At the game's own step a sequence that was never cut runs the game's update as it is. */
        const int cut = sequence_slots[k].cut;
        if (cut && speed > 0.0f && speed <= 4.0f) {
            const float advance = speed * (float)((double)ns / (double)ARCEUS_WHOLE_STEP_NS);
            /* The game steps once per update, by the speed. The budget keeps at most one such
               step beyond this frame's own, so a hitch is not caught up, as in the game. */
            const float cap = (speed > 1.0f ? speed : 1.0f) + advance;
            float pending = sequence_slots[k].pending + advance;
            if (pending > cap) pending = cap;
            if (battle_sequence_even) {
                snap = SEQUENCE_SNAP_SHARE * advance < SEQUENCE_SNAP_MAX ? SEQUENCE_SNAP_SHARE * advance : SEQUENCE_SNAP_MAX;
                partial = sequence_even_step(guest_float(object + 0xe8), pending, snap);
            } else
                partial = sequence_step(guest_float(object + 0xe8), pending);
            /* A skipped update keeps the whole budget, a negative carry from an earlier snap too. */
            if (partial <= 0.0f) skip = 1, partial = 0.0f;
            sequence_slots[k].pending = pending - partial;
            slot = k;
        } else if (cut) {
            sequence_slots[k].ns += ns;
            if (sequence_slots[k].ns < ARCEUS_WHOLE_STEP_NS) skip = 1;
            else {
                sequence_slots[k].ns -= ARCEUS_WHOLE_STEP_NS;
                if (sequence_slots[k].ns >= ARCEUS_WHOLE_STEP_NS) sequence_slots[k].ns = 0;
            }
        }
        pm_unlock(&battle_sequence_lock);
    }
    if (skip) {
        __atomic_add_fetch(&battle_sequence_skips, 1, __ATOMIC_RELAXED);
        c->x[0] = 0;
        c->pc = c->x[30];
    } else if (partial > 0.0f) {
        /* Speed is read only at 9DC3E0 inside the update, so pass the step there and restore it. */
        const u64 return_pc = c->x[30];
        /* The update takes only this in X0. Re-enter with the same argument and
           return address; leave the final call's return registers intact. */
        /* No more updates than whole frames in the game's own step. With a speed up to 1 that is
           one: a second update would pass a whole frame that no rendered frame ended on, and
           the commands' step slot (9DB748, once per rendered frame) would never see it. At
           30 FPS, where a step is a hair under or over a frame, that happened all the time. */
        const unsigned limit = speed > 1.0f ? (unsigned)ceilf(speed) : 1;
        for (unsigned updates = 0; updates < limit; updates++) {
            const float before = guest_float(object + 0xe8);
            const u64 first = gr64(object);
            __atomic_add_fetch(&battle_sequence_partial, 1, __ATOMIC_RELAXED);
            guest_float_write(object + 0xf0, partial);
            battle_sequence_original(c, entry);
            /* An object the update let go of, or one with another first word, is not ours. */
            if (!readable(object, 0x308) || gr64(object) != first || guest_float(object + 0xf0) != partial) break;
            guest_float_write(object + 0xf0, speed);
            if (c->pc != return_pc || !battle_sequence_playing(object) ||
                !(guest_float(object + 0xe8) > before) || updates + 1 == limit) break;
            pm_lock(&battle_sequence_lock);
            /* A slot evicted by another sequence no longer owns our budget. */
            if (sequence_slots[slot].object == object) {
                const float frame = guest_float(object + 0xe8);
                const float pending = sequence_slots[slot].pending;
                /* Catch up whole boundaries only. A fractional tail waits for
                   the next rendered update, preserving the original cadence of
                   command callbacks rather than immediately updating them again. */
                partial = frame + pending >= floorf(frame) + 1.0f ? sequence_step(frame, pending) : 0.0f;
                sequence_slots[slot].pending = pending - partial;
            } else partial = 0.0f;
            pm_unlock(&battle_sequence_lock);
            if (!(partial > 0.0f)) break;
            c->x[0] = object;
            c->x[30] = return_pc;
            c->pc = entry;
        }
        /* The iteration guard retains any unconsumed budget in the slot. */
    } else battle_sequence_original(c, entry);
    /* A sequence that was playing and is not any more has ended. With the research switch one
       line says how long it took: an authored frame is 1/30 s at any frame rate. */
    if (slot < SEQUENCE_SLOTS && !battle_sequence_playing(object)) {
        pm_lock(&battle_sequence_lock);
        const int ours = sequence_slots[slot].object == object && sequence_slots[slot].calls;
        const u32 slot_calls = sequence_slots[slot].calls;
        const u64 start_ns = sequence_slots[slot].start_ns;
        const float first = sequence_slots[slot].first;
        if (ours) sequence_slots[slot].calls = 0;
        pm_unlock(&battle_sequence_lock);
        if (ours) {
            __atomic_add_fetch(&battle_sequence_ended, 1, __ATOMIC_RELAXED);
            if (research_observing && readable(object, 0x308))
                LOG("arceus-battle-sequence: ended object=%llx frames=%.2f end=%.2f calls=%u seconds=%.3f landing=%d speed=%g total_calls=%llu\n",
                    (unsigned long long)object, (double)(guest_float(object + 0xe8) - first),
                    (double)guest_float(object + 0xec), slot_calls, (double)(plat_ns() - start_ns) / 1e9,
                    battle_sequence_even ? 2 : 1, (double)speed, (unsigned long long)count);
        }
    }
    if (count <= 4 || count % 2000 == 0 || (research_observing && count % 250 == 0)) {
        char speeds[256];
        int n = 0;
        speeds[0] = 0;
        pm_lock(&battle_sequence_lock);
        for (unsigned s = 0; s < SEQUENCE_SPEEDS && sequence_speeds[s].calls && n >= 0 && n < (int)sizeof speeds; s++)
            n += snprintf(speeds + n, sizeof speeds - n, " %g:%llu", sequence_speeds[s].speed,
                          (unsigned long long)sequence_speeds[s].calls);
        const u64 other = sequence_speed_other;
        pm_unlock(&battle_sequence_lock);
        LOG("arceus-battle-sequence: calls=%llu partial=%llu skipped=%llu speeds=[%s ] other=%llu\n",
            (unsigned long long)count,
            (unsigned long long)__atomic_load_n(&battle_sequence_partial, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&battle_sequence_skips, __ATOMIC_RELAXED),
            speeds, (unsigned long long)other);
    }
}
/* With the research switch, once a second from the frame clock: whether the dispatch entry of
   9DBEAC still is the wrapper. If something replaced it the sequences run the game's update
   with no fix, and no line of the wrapper would say so. */
static u64 battle_sequence_watch_ns, battle_sequence_watch_lines, battle_sequence_displaced;
static void battle_sequence_dispatch_watch(u64 now) {
    if (!research_observing || !battle_sequence_original || !main_base) return;
    if (now - battle_sequence_watch_ns < 1000000000ull) return;
    battle_sequence_watch_ns = now;
    const GuestFn entry = __atomic_load_n(&g_disp[(main_base + 0x9dbeac - g_code_lo) / 4], __ATOMIC_ACQUIRE);
    const int ours = entry == battle_sequence_update;
    if (!ours) battle_sequence_displaced++;
    if (++battle_sequence_watch_lines <= 900)
        LOG("arceus-battle-sequence: dispatch=%s calls=%llu ended=%llu displaced_seconds=%llu ns=%llu\n",
            ours ? "wrapper" : "OTHER", (unsigned long long)__atomic_load_n(&battle_sequence_calls, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&battle_sequence_ended, __ATOMIC_RELAXED),
            (unsigned long long)battle_sequence_displaced, (unsigned long long)now);
}
/* 2 the even landing (also when unset), 1 the plain landing, 0 off. */
static int battle_sequence_value(void) {
    const char* value = getenv("SNX_ARCEUS_BATTLE_SEQUENCE_FIX");
    if (!value || !strcmp(value, "2")) return 2;
    return !strcmp(value, "1");
}
static void battle_sequence_fix_init(void) {
    const int value = battle_sequence_value();
    if (!main_base || !value) return;
    battle_sequence_even = value == 2;
    if (!clocks_installed) {
        WARN("arceus-battle-sequence: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x9dbeac;
    int covered = 0;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x598) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x598 > g_code_hi || !readable(pc, 0x598) ||
        gr32(pc) != 0x6db923e9 || gr32(pc + 0x2c) != 0xaa0003f3 ||
        gr32(pc + 0x534) != 0xbd40f262 || gr32(pc + 0x538) != 0x1e222821) {
        WARN("arceus-battle-sequence: refused instruction/function guard\n");
        return;
    }
    /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
    gw32(pc + 0x2c, 0x91000013);
    jit_code_changed(pc, 0x598);
    battle_sequence_original = jit_wait(pc);
    if (!battle_sequence_original) {
        WARN("arceus-battle-sequence: compile failed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], battle_sequence_update, __ATOMIC_RELEASE);
    LOG("arceus-battle-sequence: EXPERIMENT: battle effect sequences advance 30 frames per second of elapsed time%s\n",
        battle_sequence_even ? ", even landing" : "");
}
/* Map cursors 120F1F4 (field map) and 1245DB8 (world map) add stick x speed to the position
   per call and count held frames in two counters up to 9. The fix scales the speed by
   elapsed / (1/30) for the call and lets the counters rise only once 1/30 s has built up. */
typedef struct { u32 offset, size, head, mov, speed_at, speed_word, speed, held; const char* label; } MapCursorSite;
static const MapCursorSite map_cursor_sites[2] = {
    {0x120f1f4, 0x55c, 0xd101c3ff, 0x54, 0x270, 0xbd4d7262, 0xd70, 0xd78, "field map"},
    {0x1245db8, 0x828, 0xfc1a0fea, 0x5c, 0x518, 0xbd497262, 0x970, 0x978, "world map"},
};
static GuestFn map_cursor_original[2];
static PMutex map_cursor_lock = PMUTEX_INIT;
static u64 map_cursor_object[2], map_cursor_ns[2], map_cursor_calls[2];
static void map_cursor_update(CPU* c, u64 entry, unsigned kind) {
    const MapCursorSite* s = &map_cursor_sites[kind];
    const u64 object = c->x[0];
    const u64 count = __atomic_add_fetch(&map_cursor_calls[kind], 1, __ATOMIC_RELAXED);
    if (!readable(object, s->held + 8)) {
        map_cursor_original[kind](c, entry);
        return;
    }
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    pm_lock(&map_cursor_lock);
    if (map_cursor_object[kind] != object) map_cursor_object[kind] = object, map_cursor_ns[kind] = 0;
    map_cursor_ns[kind] += ns;
    const int tick = map_cursor_ns[kind] >= ARCEUS_WHOLE_STEP_NS;
    if (tick) {
        map_cursor_ns[kind] -= ARCEUS_WHOLE_STEP_NS;
        if (timer_carry_fix) {
            if (map_cursor_ns[kind] > TIMER_CARRY_LIMIT_NS) map_cursor_ns[kind] = TIMER_CARRY_LIMIT_NS;
        } else if (map_cursor_ns[kind] >= ARCEUS_WHOLE_STEP_NS) map_cursor_ns[kind] = 0;
    }
    pm_unlock(&map_cursor_lock);
    const float speed = guest_float(object + s->speed);
    const float scaled = speed * (float)((double)ns / (double)ARCEUS_WHOLE_STEP_NS);
    const u32 held_a = gr32(object + s->held), held_b = gr32(object + s->held + 4);
    guest_float_write(object + s->speed, scaled);
    map_cursor_original[kind](c, entry);
    if (!readable(object, s->held + 8)) return;
    if (guest_float(object + s->speed) == scaled) guest_float_write(object + s->speed, speed);
    if (!tick) {
        if (gr32(object + s->held) == held_a + 1) gw32(object + s->held, held_a);
        if (gr32(object + s->held + 4) == held_b + 1) gw32(object + s->held + 4, held_b);
    }
    if (count <= 2 || count % 6000 == 0)
        LOG("arceus-map-cursor: %s calls=%llu speed=%g scaled=%g\n", s->label, (unsigned long long)count,
            (double)speed, (double)scaled);
}
static void field_map_cursor_update(CPU* c, u64 e) { map_cursor_update(c, e, 0); }
static void world_map_cursor_update(CPU* c, u64 e) { map_cursor_update(c, e, 1); }
static void map_cursor_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_MAP_CURSOR_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-map-cursor: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const GuestFn wrappers[2] = {field_map_cursor_update, world_map_cursor_update};
    unsigned installed = 0;
    for (unsigned k = 0; k < 2; k++) {
        const MapCursorSite* s = &map_cursor_sites[k];
        const u64 pc = main_base + s->offset;
        int covered = 0;
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == s->size) covered = 1;
        if (!covered || pc < g_code_lo || pc + s->size > g_code_hi || !readable(pc, s->size) ||
            gr32(pc) != s->head || gr32(pc + s->mov) != 0xaa0003f3 || gr32(pc + s->speed_at) != s->speed_word) {
            WARN("arceus-map-cursor: refused %s guard\n", s->label);
            continue;
        }
        /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
        gw32(pc + s->mov, 0x91000013);
        jit_code_changed(pc, s->size);
        map_cursor_original[k] = jit_wait(pc);
        if (!map_cursor_original[k]) {
            WARN("arceus-map-cursor: %s compile failed\n", s->label);
            continue;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
        installed++;
    }
    LOG("arceus-map-cursor: EXPERIMENT: %u of 2 map cursors follow elapsed time\n", installed);
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

/* The main rigid-body wrapper runs once per rendered frame and normally selects
   a fixed 1/30 s step, so its world simulates at FPS/30 speed. Replace the
   selection with the smaller of 1/30 and the elapsed input. Above 30 FPS that
   is real time; at 30 FPS or below it keeps the authored step and the original
   slowdown instead of asking the solver for a longer one. A NaN input also
   falls back to 1/30, and the existing 2^-23 gate at 2AF2A20 still skips the
   solve for a zero step. Worlds stepped through other callers are not touched. */
static void physics_timestep_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_PHYSICS_TIMESTEP_FIX", 1)) return;
    const u64 pc = main_base + 0x2af25b4;
    int covered = 0;
    if (!clocks_installed) {
        WARN("arceus-physics-timestep: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x6b8) covered = 1;
    if (!covered || pc < g_code_lo || pc + 0x6b8 > g_code_hi || !readable(pc, 0x6b8) ||
        gr32(pc) != 0xfc190fe8 || gr32(pc + 0x38) != 0x39448688 ||
        gr32(pc + 0x48) != 0x1e270100 || gr32(pc + 0x6c) != 0x1e280c08) {
        WARN("arceus-physics-timestep: refused instruction/function guard\n");
        return;
    }
    /* FCSEL S8,S0,S8,EQ -> FMINNM S8,S0,S8. S0 holds 1/30 and S8 the input. */
    gw32(pc + 0x6c, 0x1e287808);
    jit_code_changed(pc, 0x6b8);
    if (!jit_wait(pc)) {
        WARN("arceus-physics-timestep: wrapper JIT failed\n");
        return;
    }
    physics_timestep_active = 1;
    LOG("arceus-physics-timestep: EXPERIMENT: main rigid-body step is now min(1/30, elapsed)\n");
}

/* 2795204 steps AI navigation (hkaiWorld::stepWorld) once per rendered frame, but mode
   +150 stays 0 and selects a fixed 1/30 s, so navigation ran at FPS/30 speed. Use
   min(1/30, elapsed) like the physics step. arceus_step never yields a zero step and
   FMINNM maps NaN to 1/30. A nonzero mode, never observed, would also be capped. */
static void navigation_timestep_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX", 1)) return;
    const u64 pc = main_base + 0x2795204, caller = main_base + 0x279a6d0;
    int covered = 0;
    if (!clocks_installed) {
        WARN("arceus-navigation-timestep: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == 0x928) covered = 1;
    /* Prologue, the +150 mode load, the FCSEL, and the caller's tail branch. */
    if (!covered || pc < g_code_lo || pc + 0x928 > g_code_hi || !readable(pc, 0x928) ||
        !readable(caller, 0x50) || gr32(pc) != 0xd10283ff || gr32(pc + 0x24) != 0x39454008 ||
        gr32(pc + 0x40) != 0x1e200c28 || gr32(caller) != 0xf81e0ff3 || gr32(caller + 0x4c) != 0x17ffeaba) {
        WARN("arceus-navigation-timestep: refused instruction/function guard\n");
        return;
    }
    /* FCSEL S8,S1,S0,EQ -> FMINNM S8,S1,S0. S1 holds 1/30 and S0 the input. */
    gw32(pc + 0x40, 0x1e207828);
    jit_code_changed(pc, 0x928);
    if (!jit_wait(pc)) {
        WARN("arceus-navigation-timestep: JIT failed\n");
        return;
    }
    navigation_timestep_active = 1;
    LOG("arceus-navigation-timestep: EXPERIMENT: AI navigation step is now min(1/30, elapsed)\n");
}

/* Owners that the cadence census measured once per rendered frame, each adding a fixed
   +/-1/30 s per call: the day clock feed (in-game minutes), event hold timers and
   weather blends. Each builds the constant with MOV/MOVK W8, which becomes ADRP X8 and
   LDR W8 from the frame-step cell, as for the water clock. */
typedef struct { u32 owner, size, head, site; u8 negative; const char* label; } FrameStepSite;
static const FrameStepSite frame_step_sites[] = {
    {0x1d5d59c,0x528,0xd10203ff,0x1d5d684,0,"world time"},
    {0x167f988,0x650,0xd103c3ff,0x167ff0c,0,"event hold +190"},
    {0x168300c,0x51c,0xd10383ff,0x16832c0,0,"event timer +1A0"},
    {0x168300c,0x51c,0xd10383ff,0x16832e4,0,"event timer +198"},
    {0x1683528,0x35c,0xd10103ff,0x16836d0,0,"event timer +19C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47f5c,1,"weather fade +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47f94,1,"weather fade +250"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b47fcc,1,"weather fade +254"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b48a48,0,"weather rise +254"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b48a94,0,"weather rise +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b49004,0,"weather rise +24C"},
    {0x1b47d10,0x1a70,0xd10343ff,0x1b493f4,0,"weather rise +250"},
};
#define FRAME_STEP_SITES (sizeof frame_step_sites / sizeof frame_step_sites[0])

static int covers_exactly(u64 pc, u64 size) {
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == size) return 1;
    return 0;
}
static int writable(u64 address, u64 size) {
    MemBlock block;
    if (!readable(address, size)) return 0;
    mem_query(address, &block);
    return (block.perm & PERM_W) && block.addr + block.size >= address + size;
}
static float frame_step_ratio(void) {
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float r = (float)((double)ns / 33333334.0);
    if (!(r <= 1.0f)) r = 1.0f;
    /* The floor only keeps the ratio above zero. It binds above 30000 FPS, not at unlocked rates. */
    return r < ARCEUS_MIN_STEP_RATIO ? ARCEUS_MIN_STEP_RATIO : r;
}

/* 1E1A25C eases cave lighting through record x0+5E640: target +0, active byte +4,
   progress +8 and increment +C, which a change sets to 1/120 and then adds once per
   frame. Scale the increment by step/(1/30) for each call while a change is active,
   and restore the authored value if the call left ours in place. */
#define CAVE_LIGHT_RECORD 0x5e640
static GuestFn cave_light_original;
static void cave_light_update(CPU* c, u64 entry) {
    const u64 record = c->x[0] + CAVE_LIGHT_RECORD;
    if (!writable(record, 16) || !(gr32(record + 4) & 0xff)) {
        cave_light_original(c, entry);
        return;
    }
    const u32 authored = gr32(record + 0xc);
    float increment;
    memcpy(&increment, &authored, sizeof increment);
    increment *= frame_step_ratio();
    u32 seen;
    memcpy(&seen, &increment, sizeof seen);
    gw32(record + 0xc, seen);
    cave_light_original(c, entry);
    if (gr32(record + 0xc) == seen) gw32(record + 0xc, authored);
}

static unsigned frame_step_sites_init(const FrameStepSite* sites, unsigned n, const char* tag) {
    unsigned installed = 0;
    for (unsigned k = 0; k < n; k++) {
        const u64 owner = main_base + sites[k].owner, pc = main_base + sites[k].site;
        const u32 movk = sites[k].negative ? 0x72b7a108 : 0x72a7a108;
        if (!covers_exactly(owner, sites[k].size) || !readable(owner, sites[k].size) ||
            gr32(owner) != sites[k].head || gr32(pc) != 0x52911128 || gr32(pc + 4) != movk) {
            WARN("%s: refused guard %s at main+%x\n", tag, sites[k].label, sites[k].site);
            continue;
        }
        /* ADRP X8, cell; LDR W8, [X8, #0 or #4]. The next instruction moves W8 to a float. */
        const u64 pages = ((frame_step_address >> 12) - (pc >> 12)) & 0x1fffff;
        gw32(pc, 0x90000008 | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5));
        gw32(pc + 4, sites[k].negative ? 0xb9400508 : 0xb9400108);
        jit_code_changed(pc, 8);
        installed++;
    }
    return installed;
}

/* One page holds the frame-step cell for every fix that loads it. The first user maps it. */
static int frame_step_cell_init(const char* tag) {
    if (frame_step_address) return 1;
    const u64 address = mem_find_free(main_base + 0x5000000, main_base + 0x40000000, 4096, 4096);
    if (!address || mem_map(address, 4096, MS_STATIC, PERM_RW)) {
        WARN("%s: could not allocate the frame-step cell\n", tag);
        return 0;
    }
    pm_lock(&step_lock);
    frame_step_address = address;
    frame_step_write(step.step ? step.step : step.initial_ns);
    pm_unlock(&step_lock);
    return 1;
}

static int cave_light_init(void) {
    const u64 pc = main_base + 0x1e1a25c;
    /* Prologue, the record offset build, the 1/120 build, the progress load and add, and the B at +11C. */
    if (!covers_exactly(pc, 0x17dc) || pc < g_code_lo || pc + 0x17dc > g_code_hi || !readable(pc, 0x17dc) ||
        gr32(pc) != 0xd105c3ff || gr32(pc + 0xc4) != 0x529cc808 || gr32(pc + 0xc8) != 0x72a000a8 ||
        gr32(pc + 0x101c) != 0x5291112a || gr32(pc + 0x1020) != 0x72a7810a ||
        gr32(pc + 0x1050) != 0x2d410520 || gr32(pc + 0x1054) != 0x1e202820 || gr32(pc + 0x11c) != 0x14000005) {
        WARN("arceus-world-timers: refused cave light guard\n");
        return 0;
    }
    gw32(pc + 0x11c, 0xb40000bf); /* Equivalent B -> CBZ XZR to pc+0x130. */
    jit_code_changed(pc, 0x17dc);
    cave_light_original = jit_wait(pc);
    if (!cave_light_original) {
        WARN("arceus-world-timers: cave light body did not compile\n");
        return 0;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], cave_light_update, __ATOMIC_RELEASE);
    return 1;
}

static void world_timers_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_WORLD_TIMERS_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-world-timers: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!frame_step_cell_init("arceus-world-timers")) return;
    const unsigned sites = frame_step_sites_init(frame_step_sites, FRAME_STEP_SITES, "arceus-world-timers");
    const int cave = cave_light_init();
    LOG("arceus-world-timers: EXPERIMENT: %u of %u fixed-step sites and cave light=%d use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned)FRAME_STEP_SITES, cave, (unsigned long long)frame_step_address);
}

/* 198D7E4 airborne branch: the velocity gains -g x factor x rate x 1/30 per call, and the
   census and gravity probe saw one call per rendered frame, so falls sped up with FPS.
   The -1/30 is built with MOV/MOVK W8 and moved to S1 at +2B4. Load the negated
   frame step instead, as the world timers do. */
static const FrameStepSite gravity_sites[] = {
    {0x198d7e4,0x998,0xd10503ff,0x198da8c,1,"airborne gravity"},
};
/* Character movement steps that add accel x 1/30 (or subtract x/30) once per call, which
   runs once per rendered frame. Gravity alone was fixed, so airborne sideways motion stayed
   4 times fast at 120 FPS and jumps looked floaty. Each pair is a MOV/MOVK W8 of +-1/30. */
static const FrameStepSite movement_sites[] = {
    {0x198d7e4,0x998,0xd10503ff,0x198e020,0,"shared airborne force"},
    {0x19843a0,0xb18,0xd102c3ff,0x1984a70,1,"shared vertical"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0bee4,0,"player velocity accel"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0baf0,0,"player speed accel"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x1a0b3ac,1,"player countdown +750"},
    {0x1a0e0ec,0x118c,0xa9ba6ffc,0x1a0eea0,0,"player airborne force"},
    {0x1a14f80,0xc54,0xd10283ff,0x1a158c8,1,"player vertical"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1a17b10,0,"player state accel"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1a17f3c,0,"player state force"},
};
#define MOVEMENT_SITES (sizeof movement_sites / sizeof movement_sites[0])
/* Bit 9 belongs to capped_fall_init. Bit 10 changes the player's +118 horizontal
   position step from v/30 to v*elapsed, after its caller has integrated acceleration. */
static const FrameStepSite horizontal_move_site =
    {0x1a0d9dc,0x11c,0xd10203ff,0x1a0da20,0,"player horizontal displacement"};
static void movement_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_MOVEMENT_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-movement-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!frame_step_cell_init("arceus-movement-fix")) return;
    /* Optional hex mask over movement_sites (bit 0 is the first) to test sites one by one. */
    const unsigned long mask = movement_mask_value();
    FrameStepSite chosen[MOVEMENT_SITES + 1];
    unsigned n = 0;
    for (unsigned k = 0; k < MOVEMENT_SITES; k++)
        if (mask >> k & 1) chosen[n++] = movement_sites[k];
    if (mask & 0x400ul) chosen[n++] = horizontal_move_site;
    const unsigned sites = frame_step_sites_init(chosen, n, "arceus-movement-fix");
    LOG("arceus-movement-fix: EXPERIMENT: %u of %u selected character movement sites (mask %lx) use min(1/30, elapsed), cell=%llx\n",
        sites, n, mask, (unsigned long long)frame_step_address);
}
/* 257AF70 advances a controller angle by param/30 up to +690 each call.
   Change only that accumulator's step, preserving the authored rate and cap. */
static const FrameStepSite ride_ramp_sites[] = {
    {0x257af70,0x20d0,0xd10383ff,0x257c9f4,0,"ride angle ramp"},
};
static void ride_ramp_fix_init(void) {
    if (!main_base || !arceus_switch_enabled("SNX_ARCEUS_RIDE_RAMP_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-ride-ramp: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* The constant feeds FMOV S0,W8, without changing a pointer. */
    if (!readable(main_base + 0x257c9fc, 4) || gr32(main_base + 0x257c9fc) != 0x1e270100) {
        WARN("arceus-ride-ramp: refused float-use guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-ride-ramp")) return;
    const unsigned sites = frame_step_sites_init(ride_ramp_sites, 1, "arceus-ride-ramp");
    LOG("arceus-ride-ramp: EXPERIMENT: %u of 1 ride angle ramp sites use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned long long)frame_step_address);
}
static void gravity_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_GRAVITY_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-gravity-fix: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* FMOV S1, W8 right after the pair, so only the constant changes. */
    const u64 next = main_base + gravity_sites[0].site + 0xc;
    if (!readable(next, 4) || gr32(next) != 0x1e270101) {
        WARN("arceus-gravity-fix: refused guard at main+%x\n", gravity_sites[0].site + 0xc);
        return;
    }
    if (!frame_step_cell_init("arceus-gravity-fix")) return;
    const unsigned sites = frame_step_sites_init(gravity_sites, 1, "arceus-gravity-fix");
    LOG("arceus-gravity-fix: EXPERIMENT: %u of 1 airborne gravity sites use min(1/30, elapsed), cell=%llx\n",
        sites, (unsigned long long)frame_step_address);
}

/* 19843A0 queues a replacement velocity with one -g*factor/30 contribution.
   Adjust only that contribution on a new queue for the player and ride classes.
   Other classes, unchanged queues and the horizontal components keep their values. */
static GuestFn capped_fall_original;
static u64 capped_fall_calls, capped_fall_adjusted;
/* 19843A0 queues direction x config+D8 x stick at +6F0 (flag +6E0) and the consumer adds
   it to the velocity once. The only caller seen, the script native at 1FF4F44, calls it
   once per frame, so air steering accelerated with the frame rate. Scale a new queue entry
   by step / (1/30). An entry this call did not write keeps its value. */
#define AIR_STEERING_CALLER 0x1ff4f44
/* Ride table 41629B8 has its own slot +128, 252B124, which calls 19843A0 at 252B970. */
#define AIR_STEERING_RIDE_CALLER 0x252b974
static int capped_fall_fix, air_steering_fix;
static u64 air_steering_adjusted;
static int air_steering_scale(u64 object, int queued, const u32 previous[3], float seconds) {
    if (!(gr32(object + 0x6e0) & 0xff) || !(seconds > 0.0f) || seconds >= NOMINAL_STEP_SECONDS) return 0;
    int changed = !queued;
    float value[3];
    for (unsigned k = 0; k < 3; k++) {
        if (previous[k] != gr32(object + 0x6f0 + 4 * k)) changed = 1;
        value[k] = guest_float(object + 0x6f0 + 4 * k);
        if (!isfinite(value[k])) return 0;
    }
    if (!changed) return 0;
    for (unsigned k = 0; k < 3; k++)
        guest_float_write(object + 0x6f0 + 4 * k, value[k] * (seconds / NOMINAL_STEP_SECONDS));
    return 1;
}
static int capped_fall_class(u64 object) {
    if (!readable(object, 8)) return 0;
    const u64 table = gr64(object);
    if (!readable(table, 0x3c0)) return 0;
    if (table == main_base + 0x413db58)
        return gr64(table + 0x128) == main_base + 0x19843a0 &&
               gr64(table + 0x270) == main_base + 0x1a145ac &&
               gr64(table + 0x2a0) == main_base + 0x1a08560;
    const u32 ride_tables[] = {0x41629b8, 0x41642a0, 0x4164dc0, 0x4165cd0, 0x4166ff8};
    for (unsigned k = 0; k < sizeof ride_tables / sizeof ride_tables[0]; k++)
        if (table == main_base + ride_tables[k])
            return gr64(table + 0x270) == main_base + 0x198d7e4 &&
                   gr64(table + 0x2a0) == main_base + 0x257af70 &&
                   gr64(table + 0x3b8) == main_base + 0x257abb4;
    return 0;
}
static void capped_fall_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const int eligible = capped_fall_class(object) && writable(object, 0xbec);
    const int queued = eligible && (gr32(object + 0x6a0) & 0xff);
    u32 previous[4] = {0};
    for (unsigned k = 0; eligible && k < 4; k++) previous[k] = gr32(object + 0x6b0 + 4 * k);
    const float factor = eligible ? guest_float(object + 0xbe8) : 0.0f;
    const float seconds = guest_float(frame_step_address);
    const int steering = air_steering_fix && eligible && (c->x[30] == main_base + AIR_STEERING_CALLER ||
                                                          c->x[30] == main_base + AIR_STEERING_RIDE_CALLER);
    const int steering_queued = steering && (gr32(object + 0x6e0) & 0xff);
    u32 steering_previous[3] = {0};
    for (unsigned k = 0; steering && k < 3; k++) steering_previous[k] = gr32(object + 0x6f0 + 4 * k);
    capped_fall_original(c, entry);
    const u64 calls = __atomic_add_fetch(&capped_fall_calls, 1, __ATOMIC_RELAXED);
    if (steering && capped_fall_class(object) && writable(object, 0xbec) &&
        air_steering_scale(object, steering_queued, steering_previous, seconds)) {
        const u64 adjusted = __atomic_add_fetch(&air_steering_adjusted, 1, __ATOMIC_RELAXED);
        if (adjusted <= 4 || adjusted % 1000 == 0)
            LOG("arceus-air-steering-fix: adjusted=%llu object=%llx table=main+%llx xz=%g,%g step=%g\n",
                (unsigned long long)adjusted, (unsigned long long)object,
                (unsigned long long)(gr64(object) - main_base), (double)guest_float(object + 0x6f0),
                (double)guest_float(object + 0x6f8), (double)seconds);
    }
    if (capped_fall_fix && eligible && capped_fall_class(object) && writable(object, 0xbec) &&
        (gr32(object + 0x6a0) & 0xff) && guest_float(object + 0xbe8) == factor) {
        const float g = guest_float(main_base + 0x413a0f8);
        const float y = guest_float(object + 0x6b4);
        int changed = !queued;
        for (unsigned k = 0; k < 4; k++)
            if (previous[k] != gr32(object + 0x6b0 + 4 * k)) changed = 1;
        if (changed && isfinite(y) && isfinite(g) && isfinite(factor) && g > 0.0f && factor > 0.0f &&
            seconds > 0.0f && seconds <= NOMINAL_STEP_SECONDS) {
            guest_float_write(object + 0x6b4, y + (g * factor) * (NOMINAL_STEP_SECONDS - seconds));
            const u64 adjusted = __atomic_add_fetch(&capped_fall_adjusted, 1, __ATOMIC_RELAXED);
            if (adjusted <= 4 || adjusted % 1000 == 0)
                LOG("arceus-capped-fall: adjusted=%llu object=%llx table=main+%llx factor=%g queued_y=%g step=%g\n",
                    (unsigned long long)adjusted, (unsigned long long)object,
                    (unsigned long long)(gr64(object) - main_base), (double)factor, (double)y, (double)seconds);
        }
    }
    if (calls <= 4 || calls % 1000 == 0)
        LOG("arceus-capped-fall: calls=%llu adjusted=%llu object=%llx eligible=%d step=%g\n",
            (unsigned long long)calls,
            (unsigned long long)__atomic_load_n(&capped_fall_adjusted, __ATOMIC_RELAXED),
            (unsigned long long)object, eligible, (double)seconds);
}
/* The two words at +6D0 that build the queued -1/30: 1 when they are the game's own, 2 when movement
   bit 1 loads the negative frame step from this cell, 0 otherwise. */
static int capped_fall_body(u64 pc, u32 first, u32 second) {
    if (first == 0x52911128 && second == 0x72b7a108) return 1;
    if (!frame_step_address || (first & 0x9f00001f) != 0x90000008 ||
        (second & 0xffc003ff) != 0xb9400108) return 0;
    const u64 immlo = (first >> 29) & 3;
    const u64 immhi = (first >> 5) & 0x7ffff;
    const u32 imm21 = (u32)((immhi << 2) | immlo);
    const int64_t pages = (imm21 & 0x100000) ? (int64_t)imm21 - 0x200000 : (int64_t)imm21;
    const int64_t page_delta = pages * 4096;
    const u64 page = (pc & ~UINT64_C(0xfff)) + (u64)page_delta;
    const u64 offset = (u64)((second >> 10) & 0xfff) * 4;
    if (page + offset == frame_step_address + 4) return 2;
    return 0;
}
static void capped_fall_init(void) {
    const int enabled = arceus_switch_enabled("SNX_ARCEUS_CAPPED_FALL_FIX", 1);
    const int movement = arceus_switch_enabled("SNX_ARCEUS_MOVEMENT_FIX", 1);
    const unsigned long mask = movement_mask_value();
    /* Explicit mask bit 9 also enables this fix through an existing launcher setting. */
    const char* mask_text = getenv("SNX_ARCEUS_MOVEMENT_SITES");
    const int selected = movement && mask_text && (mask & 0x200ul);
    const int steer = arceus_switch_enabled("SNX_ARCEUS_AIR_STEERING_FIX", 1);
    int fall = enabled || selected;
    if (!main_base || (!fall && !steer)) return;
    if (!clocks_installed) {
        WARN("arceus-capped-fall: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x19843a0;
    if (!covers_exactly(pc, 0xb18) || pc < g_code_lo || pc + 0xb18 > g_code_hi ||
        !readable(pc, 0xb18) || gr32(pc) != 0xd102c3ff ||
        gr32(pc + 0xfc) != 0x14000002 || !capped_fall_body(pc + 0x6d0, gr32(pc + 0x6d0), gr32(pc + 0x6d4)) ||
        gr32(pc + 0x700) != 0x3d81ae60 || gr32(pc + 0x70c) != 0x391a8268) {
        WARN("arceus-capped-fall: refused guard\n");
        return;
    }
    /* Movement bit 1 already steps this queued gravity, so only air steering is installed then. */
    if (fall && capped_fall_body(pc + 0x6d0, gr32(pc + 0x6d0), gr32(pc + 0x6d4)) == 2) {
        WARN("arceus-capped-fall: off, movement bit 1 already steps the queued gravity\n");
        fall = 0;
        if (!steer) return;
    }
    if (!frame_step_cell_init("arceus-capped-fall")) return;
    /* Match the census marker so it leaves this wrapper in place. */
    gw32(pc + 0xfc, 0xb400005f);
    jit_code_changed(pc, 0xb18);
    capped_fall_original = jit_wait(pc);
    if (!capped_fall_original) {
        WARN("arceus-capped-fall: compile failed\n");
        return;
    }
    capped_fall_fix = fall;
    air_steering_fix = steer;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], capped_fall_update, __ATOMIC_RELEASE);
    if (fall)
        LOG("arceus-capped-fall: EXPERIMENT: player/ride queued gravity uses elapsed time, other classes unchanged\n");
    if (steer)
        LOG("arceus-air-steering-fix: EXPERIMENT: player/ride air steering from main+%x follows elapsed time\n",
            AIR_STEERING_CALLER);
}

/* The AI context scheduler 1050E00 adds 1 to every script context's frame count +88 per
   pass, so wild Pokemon vigilance, hunger, sleep, WaitFrame and turns ran 2x fast at 60 FPS.
   With SNX_ARCEUS_AI_TICK_FIX=1 a pass adds 1 only when 1/30 s has built up, else 0. */
#define AI_TICK_NS 33333333ll
#define AI_TICK_SLOTS 8
static GuestFn ai_tick_original, ai_turn_original[2];
static PMutex ai_tick_lock = PMUTEX_INIT;
static struct { u64 scheduler; int64_t ns; } ai_tick_slots[AI_TICK_SLOTS];
static unsigned ai_tick_next_slot, ai_tick_inflight;
static u64 ai_tick_calls, ai_tick_ticks, ai_tick_overlaps, ai_turn_holds[2], ai_turn_runs[2];
/* Passes and ticks of every scheduler so far. */
static u64 ai_pass_total, ai_tick_total;
#define AI_TURN_NODES 32
static struct { u64 node, passes, ticks; u32 count, pending, first; float share, credit; } ai_turn_nodes[AI_TURN_NODES];
/* A step of 30 ms or more is one whole tick, so 30 FPS keeps one per pass. Shorter steps
   add up. A tick may come up to 1 ms early and owes that time, which keeps 0,1,0,1 at a
   jittery 60 FPS. */
static unsigned ai_tick_count(u64 scheduler, u64 ns) {
    unsigned n = 0;
    pm_lock(&ai_tick_lock);
    unsigned k = 0;
    while (k < AI_TICK_SLOTS && ai_tick_slots[k].scheduler != scheduler) k++;
    if (k == AI_TICK_SLOTS) {
        k = ai_tick_next_slot++ % AI_TICK_SLOTS;
        ai_tick_slots[k].scheduler = scheduler;
        ai_tick_slots[k].ns = 0;
    }
    if (ns >= 30000000) n = 1;
    else {
        ai_tick_slots[k].ns += (int64_t)ns;
        if (ai_tick_slots[k].ns >= AI_TICK_NS - 1000000) n = 1, ai_tick_slots[k].ns -= AI_TICK_NS;
    }
    ai_pass_total++;
    ai_tick_total += n;
    pm_unlock(&ai_tick_lock);
    return n;
}
static void ai_tick_update(CPU* c, u64 entry) {
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    const unsigned n = ai_tick_count(c->x[0], ns);
    /* The patched body adds V9.S[1] to +88. 1050E00 uses no D9, and callees keep it. */
    const u32 saved[2] = {c->v[9].s[0], c->v[9].s[1]};
    c->v[9].s[0] = 0;
    c->v[9].s[1] = n;
    if (__atomic_add_fetch(&ai_tick_inflight, 1, __ATOMIC_RELAXED) > 1)
        __atomic_add_fetch(&ai_tick_overlaps, 1, __ATOMIC_RELAXED);
    ai_tick_original(c, entry);
    __atomic_sub_fetch(&ai_tick_inflight, 1, __ATOMIC_RELAXED);
    c->v[9].s[0] = saved[0];
    c->v[9].s[1] = saved[1];
    const u64 ticks = __atomic_add_fetch(&ai_tick_ticks, n, __ATOMIC_RELAXED);
    const u64 calls = __atomic_add_fetch(&ai_tick_calls, 1, __ATOMIC_RELAXED);
    if (calls <= 4 || calls % 1800 == 0) {
        unsigned schedulers = 0;
        pm_lock(&ai_tick_lock);
        for (unsigned k = 0; k < AI_TICK_SLOTS; k++) schedulers += ai_tick_slots[k].scheduler != 0;
        pm_unlock(&ai_tick_lock);
        LOG("arceus-ai-tick: calls=%llu ticks=%llu ns=%llu schedulers=%u overlaps=%llu turn_holds=%llu turnkey_holds=%llu turn_runs=%llu turnkey_runs=%llu\n",
            (unsigned long long)calls, (unsigned long long)ticks, (unsigned long long)plat_ns(), schedulers,
            (unsigned long long)__atomic_load_n(&ai_tick_overlaps, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&ai_turn_holds[0], __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&ai_turn_holds[1], __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&ai_turn_runs[0], __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&ai_turn_runs[1], __ATOMIC_RELAXED));
    }
}
/* Turn B338DC and TurnKey B32CD4 turn one whole step per call and finish by the frame
   count. The actor calls them once per pass on a forced context and once per bucket round
   otherwise, outside 1050E00. A call earns ticks / passes since the node's last call, and
   the original runs when a whole step is earned. Held frames are added before that run. */
static int ai_turn_step(u64 node, u32 count, u32 frames, int forced, u32* pending) {
    pm_lock(&ai_tick_lock);
    unsigned k = 0, oldest = 0;
    for (; k < AI_TURN_NODES && ai_turn_nodes[k].node != node; k++)
        if (ai_turn_nodes[k].passes < ai_turn_nodes[oldest].passes) oldest = k;
    const int known = k < AI_TURN_NODES && ai_turn_nodes[k].count == count &&
                      ai_pass_total - ai_turn_nodes[k].passes <= 64;
    if (k == AI_TURN_NODES) k = oldest;
    /* A forced context is reset every pass, so its frames are this pass's own tick. */
    float share = forced ? (frames ? 1.0f : 0.0f) : known ? ai_turn_nodes[k].share : 1.0f;
    if (!forced && known && ai_pass_total != ai_turn_nodes[k].passes)
        share = (float)(ai_tick_total - ai_turn_nodes[k].ticks) / (float)(ai_pass_total - ai_turn_nodes[k].passes);
    if (!known) {
        ai_turn_nodes[k].node = node;
        ai_turn_nodes[k].pending = 0;
        ai_turn_nodes[k].credit = 0.0f;
    }
    /* A turn's first call runs, as at 30 FPS, and owes the part of a step it had not earned.
       An ordinary context's first share is not known yet, so the next call's stands for it. */
    if (!known) ai_turn_nodes[k].credit = forced ? share : 1.0f;
    else if (ai_turn_nodes[k].first) ai_turn_nodes[k].credit += 2.0f * share - 1.0f;
    else ai_turn_nodes[k].credit += share;
    ai_turn_nodes[k].first = !known && !forced;
    ai_turn_nodes[k].share = share;
    ai_turn_nodes[k].passes = ai_pass_total;
    ai_turn_nodes[k].ticks = ai_tick_total;
    const int run = !known || ai_turn_nodes[k].credit >= 0.999f;
    if (run) {
        ai_turn_nodes[k].credit -= 1.0f;
        *pending = ai_turn_nodes[k].pending;
        ai_turn_nodes[k].pending = 0;
        ai_turn_nodes[k].count = count + *pending + frames;
    } else {
        ai_turn_nodes[k].pending += frames;
        ai_turn_nodes[k].count = count;
    }
    pm_unlock(&ai_tick_lock);
    return run;
}
static void ai_turn_update(CPU* c, u64 entry, unsigned kind) {
    const u64 node = c->x[0], out = c->x[8], status = main_base + 0x40e7c04;
    const u64 owner = readable(node, 0x148) ? gr64(node + 0x58) : 0;
    const u64 context = readable(owner, 0x98) ? gr64(owner + 0x90) : 0;
    if (context && readable(context, 0x8c) && gr32(node + (kind ? 0x140 : 0x130)) &&
        readable(out, 4) && readable(status, 4) && gr32(status) == 4) {
        const u64 at = node + (kind ? 0x144 : 0x134);
        const int forced = gr16(context + 0x81) != 0;
        u32 pending = 0;
        if (!ai_turn_step(node, gr32(at), gr32(context + 0x88), forced, &pending)) {
            __atomic_add_fetch(&ai_turn_holds[kind], 1, __ATOMIC_RELAXED);
            gw32(out, 4);
            c->pc = c->x[30];
            return;
        }
        __atomic_add_fetch(&ai_turn_runs[kind], 1, __ATOMIC_RELAXED);
        if (pending) gw32(at, gr32(at) + pending);
    }
    ai_turn_original[kind](c, entry);
}
static void ai_turn(CPU* c, u64 entry) { ai_turn_update(c, entry, 0); }
static void ai_turn_key(CPU* c, u64 entry) { ai_turn_update(c, entry, 1); }
static void ai_tick_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_AI_TICK_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-ai-tick: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* LDR S0 +84, LDR W8 +88, ADD W8 #1, FADD S0 S8, STR W8 +88, LDR X8 +58, ADD X9, STR S0 +84. */
    static const u32 old[8] = {0xbd408400, 0xb9408808, 0x11000508, 0x1e282800,
                               0xb9008808, 0xf9402c08, 0x91003109, 0xbd008400};
    /* The same with MOV V0.S[1],W8, ADD V0.2S,V0.2S,V9.2S and one STUR D0 +84 as the store. */
    static const u32 new[8] = {0xbd408400, 0x1e282800, 0xb9408808, 0x4e0c1d00,
                               0x0ea98400, 0xfc084000, 0xf9402c08, 0x91003109};
    /* Each turn guard: size, the duration load at +24, the count add, MOV X19,X8 at +20. */
    static const struct { u64 offset; u32 size, duration, add; } turns[2] = {
        {0xb338dc, 0x5a8, 0xb9413008, 0x1c8}, {0xb32cd4, 0x510, 0xb9414008, 0x130}};
    const u64 pc = main_base + 0x1050e00, site = pc + 0xa60;
    int ok = covers_exactly(pc, 0x2508) && pc >= g_code_lo && pc + 0x2508 <= g_code_hi && readable(pc, 0x2508) &&
             gr32(pc) == 0xd106c3ff && gr32(pc + 0x54) == 0x1e624008;
    for (unsigned k = 0; ok && k < 8; k++) ok = gr32(site + 4 * k) == old[k];
    for (unsigned k = 0; ok && k < 2; k++) {
        const u64 at = main_base + turns[k].offset;
        ok = covers_exactly(at, turns[k].size) && at >= g_code_lo && at + turns[k].size <= g_code_hi &&
             readable(at, turns[k].size) && gr32(at) == 0xd10243ff && gr32(at + 0x20) == 0xaa0803f3 &&
             gr32(at + 0x24) == turns[k].duration && gr32(at + turns[k].add) == 0x0b000108;
    }
    if (!ok || !readable(main_base + 0x40e7c04, 4)) {
        WARN("arceus-ai-tick: refused instruction/function guard\n");
        return;
    }
    const GuestFn wrappers[2] = {ai_turn, ai_turn_key};
    GuestFn compiled[2];
    for (unsigned k = 0; k < 2; k++) {
        const u64 at = main_base + turns[k].offset;
        /* Equivalent MOV X19,X8 -> ADD X19,X8,#0 selects an independent JIT body. */
        gw32(at + 0x20, 0x91000113);
        jit_code_changed(at, turns[k].size);
        compiled[k] = jit_wait(at);
    }
    for (unsigned k = 0; k < 8; k++) gw32(site + 4 * k, new[k]);
    jit_code_changed(pc, 0x2508);
    const GuestFn body = jit_wait(pc);
    if (!body || !compiled[0] || !compiled[1]) {
        /* Without its wrapper the new body would add a stray V9, so put the old words back. */
        for (unsigned k = 0; k < 8; k++) gw32(site + 4 * k, old[k]);
        jit_code_changed(pc, 0x2508);
        WARN("arceus-ai-tick: compile failed\n");
        return;
    }
    ai_tick_original = body;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], ai_tick_update, __ATOMIC_RELEASE);
    for (unsigned k = 0; k < 2; k++) {
        ai_turn_original[k] = compiled[k];
        __atomic_store_n(&g_disp[(main_base + turns[k].offset - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
    }
    LOG("arceus-ai-tick: EXPERIMENT: AI script context frames count 30 per second of elapsed time\n");
}
/* The orientation helper 1727DF4 turns a character by slerp(current, target, t) once per
   call, t = turn rate +268 x (1/30) / duration S0, so turning was 2x fast at 60 FPS. With
   SNX_ARCEUS_TURN_FIX=1 the call gets the duration that gives 1 - (1 - t)^(step x 30). */
static GuestFn turn_original;
static u64 turn_calls, turn_scaled;
static float turn_duration(float rate, float duration, float frames) {
    const float t = rate * NOMINAL_STEP_SECONDS / duration;
    /* A whole step (33333333 ns rounds just below 1), a snap (t >= 1) and the helper's own
       early outs stay as authored. */
    if (!(frames > 0.0f && frames < 0.999f) || !(duration >= 0x1p-23f) || !(t > 0.0f && t < 1.0f)) return duration;
    const float wanted = 1.0f - powf(1.0f - t, frames);
    return wanted > 0.0f ? rate * NOMINAL_STEP_SECONDS / wanted : duration;
}
static void turn_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const float authored = c->v[0].f[0];
    if (readable(object, 0x26c) && isfinite(authored)) {
        const float frames = guest_float(frame_step_address) / NOMINAL_STEP_SECONDS;
        c->v[0].f[0] = turn_duration(guest_float(object + 0x268), authored, frames);
    }
    const float passed = c->v[0].f[0];
    if (passed != authored) __atomic_add_fetch(&turn_scaled, 1, __ATOMIC_RELAXED);
    turn_original(c, entry);
    const u64 calls = __atomic_add_fetch(&turn_calls, 1, __ATOMIC_RELAXED);
    if (calls <= 4 || calls % 6000 == 0)
        LOG("arceus-turn: calls=%llu scaled=%llu object=%llx caller=main+%llx rate=%g duration=%g passed=%g\n",
            (unsigned long long)calls, (unsigned long long)__atomic_load_n(&turn_scaled, __ATOMIC_RELAXED),
            (unsigned long long)object, (unsigned long long)(c->x[30] - main_base),
            readable(object, 0x26c) ? (double)guest_float(object + 0x268) : -1.0, (double)authored, (double)passed);
}
static void turn_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_TURN_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-turn: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x1727df4;
    /* MOV/MOVK W8 of 1/30 at +260, LDR S0,[X19,#0x268] at +2FC, FDIV S0,S0,S8 at +304. */
    if (!covers_exactly(pc, 0x3ec) || pc < g_code_lo || pc + 0x3ec > g_code_hi || !readable(pc, 0x3ec) ||
        gr32(pc) != 0xd10143ff || gr32(pc + 0x260) != 0x52911128 || gr32(pc + 0x264) != 0x72a7a108 ||
        gr32(pc + 0x2fc) != 0xbd426a60 || gr32(pc + 0x304) != 0x1e281800 || gr32(pc + 0x36c) != 0x17ffffe8) {
        WARN("arceus-turn: refused instruction/function guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-turn")) return;
    /* The census's equivalent B -> CBZ XZR, so it sees this owner as wrapped. */
    gw32(pc + 0x36c, 0xb4fffd1f);
    jit_code_changed(pc, 0x3ec);
    turn_original = jit_wait(pc);
    if (!turn_original) {
        WARN("arceus-turn: compile failed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], turn_update, __ATOMIC_RELEASE);
    LOG("arceus-turn: EXPERIMENT: character turns ease by elapsed time\n");
}

/* The scrolling list keeps a scroller at +7F8 and updates it in 272F150 once per rendered frame.
   A scroll of one row is an ease counted in updates (+1BC up to +1C0), and when it ends with the
   direction still held, the list starts a free scroll that adds a speed (+1D8) per update. At
   150 FPS the ease ends inside one tap, so each tap ran the free scroll and jumped several rows. */
/* With SNX_ARCEUS_LIST_SCROLL_FIX=1 the scroller updates only on frames with a 30 Hz tick. */
static GuestFn list_scroll_original;
static u64 list_scroll_calls, list_scroll_held;
static void list_scroll_update(CPU* c, u64 entry) {
    pm_lock(&step_lock);
    const unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    const u64 calls = __atomic_add_fetch(&list_scroll_calls, 1, __ATOMIC_RELAXED);
    if (!ticks) {
        const u64 held = __atomic_add_fetch(&list_scroll_held, 1, __ATOMIC_RELAXED);
        if (held <= 2 || held % 6000 == 0)
            LOG("arceus-list-scroll: calls=%llu held=%llu\n", (unsigned long long)calls, (unsigned long long)held);
        c->pc = c->x[30];
        return;
    }
    list_scroll_original(c, entry);
}
static void list_scroll_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_LIST_SCROLL_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-list-scroll: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x272f150;
    /* STR X19,[SP,#-0x20]! at +0, LDRB W8,[X0,#0x1B4] at +C, MOV X19,X0 at +6C. */
    if (!covers_exactly(pc, 0xa0) || pc < g_code_lo || pc + 0xa0 > g_code_hi || !readable(pc, 0xa0) ||
        gr32(pc) != 0xf81e0ff3 || gr32(pc + 0xc) != 0x3946d008 || gr32(pc + 0x6c) != 0xaa0003f3) {
        WARN("arceus-list-scroll: refused instruction/function guard\n");
        return;
    }
    /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
    gw32(pc + 0x6c, 0x91000013);
    jit_code_changed(pc, 0xa0);
    list_scroll_original = jit_wait(pc);
    if (!list_scroll_original) {
        WARN("arceus-list-scroll: compile failed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], list_scroll_update, __ATOMIC_RELEASE);
    LOG("arceus-list-scroll: EXPERIMENT: UI list scroller steps 30 times per second of elapsed time\n");
}

/* The ride controller's update 2537B28 (table 4163488 slot +90) runs once per rendered frame
   and counts five waits down by one per call: +2BC, +4A0, +2C0, +3C0 (set from a config
   count by 2542834) and +2A8 (set to 15). SNX_ARCEUS_RIDE_SWAP_FIX=1 lets them count 30
   times a second: on the other frames a running count goes in one higher and comes out as
   it was. Counts set or ended by a call are logged, at most 600 lines. */
static const u32 ride_swap_fields[5] = {0x2bc, 0x4a0, 0x2c0, 0x3c0, 0x2a8};
static GuestFn ride_swap_original;
static int ride_swap_fix, ride_swap_holding;
static float ride_swap_fraction;
static u64 ride_swap_frame = ~0ull, ride_swap_calls, ride_swap_held, ride_swap_lines;
/* 1 on a frame whose calls must not count. One decision per engine frame. */
static int ride_swap_hold(u64 frame, float r) {
    if (frame == ride_swap_frame) return ride_swap_holding;
    ride_swap_frame = frame;
    ride_swap_fraction += r;
    ride_swap_holding = ride_swap_fraction < 0.9999f;
    if (!ride_swap_holding) ride_swap_fraction = ride_swap_fraction > 1.0f ? ride_swap_fraction - 1.0f : 0.0f;
    return ride_swap_holding;
}
static void ride_swap_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const int known = writable(object, 0x4a4);
    const int hold = ride_swap_fix && known &&
        ride_swap_hold(__atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE), frame_step_ratio());
    int before[5] = {0, 0, 0, 0, 0};
    for (unsigned k = 0; known && k < 5; k++) {
        before[k] = (int)gr32(object + ride_swap_fields[k]);
        if (hold && before[k] > 0) gw32(object + ride_swap_fields[k], (u32)(before[k] + 1));
    }
    ride_swap_original(c, entry);
    __atomic_add_fetch(&ride_swap_calls, 1, __ATOMIC_RELAXED);
    if (hold) __atomic_add_fetch(&ride_swap_held, 1, __ATOMIC_RELAXED);
    if (!known || !writable(object, 0x4a4)) return;
    for (unsigned k = 0; k < 5; k++) {
        int after = (int)gr32(object + ride_swap_fields[k]);
        /* A path that did not count leaves the raised value. Put the game's own back. */
        if (hold && before[k] > 0 && after == before[k] + 1) gw32(object + ride_swap_fields[k], (u32)(after = before[k]));
        if ((after > before[k] || (!after && before[k] > 0)) &&
            __atomic_add_fetch(&ride_swap_lines, 1, __ATOMIC_RELAXED) <= 600)
            LOG("arceus-ride-swap: field=+%x before=%d after=%d ns=%llu frame=%llu calls=%llu held=%llu\n",
                ride_swap_fields[k], before[k], after, (unsigned long long)plat_ns(),
                (unsigned long long)__atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE),
                (unsigned long long)__atomic_load_n(&ride_swap_calls, __ATOMIC_RELAXED),
                (unsigned long long)__atomic_load_n(&ride_swap_held, __ATOMIC_RELAXED));
    }
}
static void ride_swap_init(void) {
    const u64 pc = main_base + 0x2537b28;
    if (!main_base || !arceus_switch_enabled("SNX_ARCEUS_RIDE_SWAP_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-ride-swap: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* STR D8,[SP,#-0x70]! at the head, the first count's LDR W8,[X19,#0x2BC] and SUBS at +148
       and +14C, the +3C0 load at +390, and MOV X19,X0 at +48 swapped for an equal ADD. */
    if (ride_swap_original || !covers_exactly(pc, 0x7964) || !readable(pc, 0x7964) || gr32(pc) != 0xfc190fe8 ||
        gr32(pc + 0x48) != 0xaa0003f3 || gr32(pc + 0x148) != 0xb942be68 || gr32(pc + 0x14c) != 0x71000508 ||
        gr32(pc + 0x390) != 0xb943c268) {
        WARN("arceus-ride-swap: refused guard at main+2537b28\n");
        return;
    }
    gw32(pc + 0x48, 0x91000013);
    jit_code_changed(pc, 0x7964);
    ride_swap_original = jit_wait(pc);
    if (!ride_swap_original) {
        WARN("arceus-ride-swap: compile failed at main+2537b28\n");
        return;
    }
    ride_swap_fix = 1;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], ride_swap_update, __ATOMIC_RELEASE);
    LOG("arceus-ride-swap: EXPERIMENT: the ride controller's frame waits count 30 times per second\n");
}


/* C21D00 (fade in) and C227BC (fade out) start a fade: its fractional progress starts over. */
static GuestFn fade_start_original[2];
static void fade_start_update(CPU* c, u64 entry, unsigned kind) {
    const u64 object = c->x[0];
    fade_start_original[kind](c, entry);
    pm_lock(&fade_lock);
    for (int k = 0; k < FADE_SLOTS; k++)
        if (fade_slots[k].object == object) {
            fade_slots[k].frame = ~0u;
            fade_slots[k].progress = 0.0f;
            fade_slots[k].generation++;
        }
    pm_unlock(&fade_lock);
}
static void fade_in_start(CPU* c, u64 e) { fade_start_update(c, e, 0); }
static void fade_out_start(CPU* c, u64 e) { fade_start_update(c, e, 1); }
static void fade_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_FADE_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-fade: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* The mode 0 step words: LDR W10 +538, ADD W10 #1, LDR Q1 +550, FADD, STR W10 +538. */
    const u64 update = main_base + 0xc21490;
    if (!readable(update, 0x300) || gr32(update + 0x44) != 0xb9453a6a || gr32(update + 0xd0) != 0x1100054a ||
        gr32(update + 0xd8) != 0x3dc15661 || gr32(update + 0xdc) != 0x4e21d400 || gr32(update + 0xe4) != 0xb9053a6a) {
        WARN("arceus-fade: refused update guard\n");
        return;
    }
    static const struct { u32 offset, size, head, mov; const char* label; } starts[2] = {
        {0xc21d00, 0xa50, 0xd10203ff, 0x2c, "fade in C21D00"},
        {0xc227bc, 0x888, 0xd101c3ff, 0x20, "fade out C227BC"},
    };
    const GuestFn wrappers[2] = {fade_in_start, fade_out_start};
    for (unsigned k = 0; k < 2; k++) {
        const u64 pc = main_base + starts[k].offset;
        if (!covers_exactly(pc, starts[k].size) || pc < g_code_lo || pc + starts[k].size > g_code_hi ||
            !readable(pc, starts[k].size) || gr32(pc) != starts[k].head || gr32(pc + starts[k].mov) != 0xaa0003f3) {
            WARN("arceus-fade: refused %s guard\n", starts[k].label);
            return;
        }
    }
    if (!frame_step_cell_init("arceus-fade") || !sequence_clock_install(0)) return;
    for (unsigned k = 0; k < 2; k++) {
        const u64 pc = main_base + starts[k].offset;
        /* Equivalent MOV X19,X0 -> ADD X19,X0,#0 selects an independent JIT body. */
        gw32(pc + starts[k].mov, 0x91000013);
        jit_code_changed(pc, starts[k].size);
        fade_start_original[k] = jit_wait(pc);
        if (!fade_start_original[k]) {
            WARN("arceus-fade: %s compile failed\n", starts[k].label);
            return;
        }
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
    }
    fade_fix_active = 1;
    LOG("arceus-fade: EXPERIMENT: common fades (mode 0) follow elapsed time\n");
}
/* 1A7191C is a thrown-object flight update, a virtual shared by three classes
   (vtables 41375C0, 413C5D0, 4141950). In flight (+5F8 == 1) each call adds S/30
   to elapsed +618, ends the planned flight when +618/+61C exceeds 1, then
   applies drag +5A0 and lift +5A4 to the queued velocity. S is a nominal-1
   ratio, so all three assume one call per 1/30 s. With the elapsed-time physics
   step, scale them for this call only by r = step/(1/30): +618 and +61C are
   divided by r so the guest's own end test sees (old + r*S/30)/T, then +618 is
   multiplied back and +61C restored. Lift is additive per call and scales by r;
   drag uses 1-(1-d)^r, exact for S = 1. Each field is restored only if the call
   left our value in place. Without the timestep fix the wrapper only logs. */
static GuestFn ball_flight_original;
static int ball_flight_correct;
static u64 ball_flight_calls, ball_flight_logs;
static u32 float_bits(float value) { u32 bits; memcpy(&bits, &value, sizeof bits); return bits; }
static void ball_flight_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    MemBlock block;
    if (!readable(object, 0x620)) { ball_flight_original(c, entry); return; }
    mem_query(object + 0x5a0, &block);
    if (!(block.perm & PERM_W) || block.addr + block.size < object + 0x620) {
        ball_flight_original(c, entry);
        return;
    }
    const u32 state = gr32(object + 0x5f8);
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    float r = (float)((double)ns / 33333334.0);
    if (!(r <= 1.0f)) r = 1.0f;
    if (r < 0.0625f) r = 0.0625f;
    const int correct = ball_flight_correct && state == 1 && r < 1.0f;
    const u32 drag_bits = gr32(object + 0x5a0), lift_bits = gr32(object + 0x5a4);
    const u32 planned_bits = gr32(object + 0x61c);
    const float elapsed0 = guest_float(object + 0x618), planned = guest_float(object + 0x61c);
    const float drag = guest_float(object + 0x5a0), lift = guest_float(object + 0x5a4);
    float elapsed_seen = elapsed0, planned_seen = planned, drag_seen = drag, lift_seen = lift;
    if (correct) {
        elapsed_seen = elapsed0 / r;
        planned_seen = planned / r;
        drag_seen = drag >= 0.0f && drag < 1.0f ? 1.0f - powf(1.0f - drag, r) : drag * r;
        lift_seen = lift * r;
        guest_float_write(object + 0x618, elapsed_seen);
        guest_float_write(object + 0x61c, planned_seen);
        guest_float_write(object + 0x5a0, drag_seen);
        guest_float_write(object + 0x5a4, lift_seen);
    }
    const u64 now = plat_ns();
    ball_flight_original(c, entry);
    const u32 state_after = gr32(object + 0x5f8);
    float elapsed1 = guest_float(object + 0x618);
    /* Valid only while the call stayed in flight: the guest added S/30. */
    const float implied_s = 30.0f * (elapsed1 - elapsed_seen);
    if (correct) {
        if (gr32(object + 0x5a0) == float_bits(drag_seen)) gw32(object + 0x5a0, drag_bits);
        if (gr32(object + 0x5a4) == float_bits(lift_seen)) gw32(object + 0x5a4, lift_bits);
        if (gr32(object + 0x61c) == float_bits(planned_seen)) {
            gw32(object + 0x61c, planned_bits);
            elapsed1 *= r;
            guest_float_write(object + 0x618, elapsed1);
        }
    }
    const u64 count = __atomic_add_fetch(&ball_flight_calls, 1, __ATOMIC_RELAXED);
    const int transition = state != state_after;
    if (state != 1 && !transition && count % 600) return;
    /* Every launch and end, the first 600 in-flight calls, then 1 in 30. */
    const u64 logs = __atomic_add_fetch(&ball_flight_logs, 1, __ATOMIC_RELAXED);
    if (!transition && logs > 600 && count % 30) return;
    u32 input_bits = __atomic_load_n(&physics_input_bits, __ATOMIC_RELAXED);
    float input; memcpy(&input, &input_bits, sizeof input);
    LOG("arceus-ball-flight: count=%llu ns=%llu object=%llx state=%u->%u step_ns=%llu r=%g corrected=%d physics_input=%g elapsed=%g->%g planned=%g implied_s=%g drag=%g->%g lift=%g->%g\n",
        (unsigned long long)count, (unsigned long long)now, (unsigned long long)object, state, state_after,
        (unsigned long long)ns, (double)r, correct, (double)input, (double)elapsed0, (double)elapsed1,
        (double)guest_float(object + 0x61c), (double)implied_s, (double)drag, (double)drag_seen,
        (double)lift, (double)lift_seen);
}

static void ball_flight_init(void) {
    const char* research = getenv("SNX_ARCEUS_TIMING_RESEARCH");
    const int observe = research && !strcmp(research, "1");
    if (!main_base || (!physics_timestep_active && !observe)) return;
    const u64 pc = main_base + 0x1a7191c;
    int covered = 0;
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    for (u32 i = 0; i < g_mod->nfns; i++)
        if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] >= 0x60c) covered = 1;
    /* Prologue, the +618 load/store, the +61C load, the drag and lift loads,
       the drag multiply and the B at +FC that is rewritten below. */
    if (!covered || pc < g_code_lo || pc + 0x60c > g_code_hi || !readable(pc, 0x60c) ||
        gr32(pc) != 0xd10203ff || gr32(pc + 0x13c) != 0xbd461e60 ||
        gr32(pc + 0x160) != 0xbd461a62 || gr32(pc + 0x178) != 0xbd061a60 ||
        gr32(pc + 0x274) != 0xbd45a268 || gr32(pc + 0x278) != 0xbd45a669 ||
        gr32(pc + 0x2c8) != 0x1e280841 || gr32(pc + 0xfc) != 0x17ffffda) {
        WARN("arceus-ball-flight: refused instruction/function guard\n");
        return;
    }
    /* Equivalent B -> CBZ XZR, both to pc+0x64, so a hash-matched original AOT
       body is not selected after invalidation (same scheme as the event camera). */
    gw32(pc + 0xfc, 0xb4fffb5f);
    jit_code_changed(pc, 0x60c);
    ball_flight_original = jit_wait(pc);
    if (!ball_flight_original) {
        WARN("arceus-ball-flight: original body did not compile; wrapper not installed\n");
        return;
    }
    ball_flight_correct = physics_timestep_active;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], ball_flight_update, __ATOMIC_RELEASE);
    LOG("arceus-ball-flight: wrapper installed correct=%d\n", ball_flight_correct);
}

/* Read-only cadence census for the owners of the audited fixed 1/30 (and 1/60,
   1/120) sites. A fixed step is only a high-FPS bug if its owner runs once per
   rendered frame, which the static audit could not establish. Every 5 s this
   logs, per owner, total calls/s and the calls/s of one sampled x0 object
   (many objects at 30 Hz and one object per frame both give large totals),
   beside main-world physics calls/s as the frame reference. Labels are the
   audit's provisional names. Owners already wrapped by other fixes are left out. */
static const struct { u32 offset, size, head, patch, word, replacement; const char* label; } census_sites[] = {
    {0xafb898,0x3fc,0xd10283ff,0x168,0x17ffffe6,0xb4fffcdf,"script char motion"},
    {0x13b32fc,0x2e4,0xd10203ff,0xf0,0x17fffff4,0xb4fffe9f,"credits timer"},
    {0x1458300,0x1154,0xd10343ff,0x1d8,0x14000069,0xb4000d3f,"movie skip fade"},
    {0x167f988,0x650,0xd103c3ff,0x440,0x17fffefa,0xb4ffdf5f,"event coord 167F988"},
    {0x168300c,0x51c,0xd10383ff,0x1fc,0x14000033,0xb400067f,"event coord 168300C"},
    {0x1683528,0x35c,0xd10103ff,0x16c,0x14000032,0xb400065f,"event coord 1683528"},
    {0x1717594,0x2d4,0xd100c3ff,0x2d0,0x17fffffc,0xb4ffff9f,"orientation helper"},
    {0x1727df4,0x3ec,0xd10143ff,0x36c,0x17ffffe8,0xb4fffd1f,"orientation helper 2"},
    {0x1982408,0xdd4,0xd10403ff,0x178,0x14000055,0xb4000abf,"shared char move"},
    {0x19843a0,0xb18,0xd102c3ff,0xfc,0x14000002,0xb400005f,"shared char vertical"},
    {0x198d7e4,0x998,0xd10503ff,0xb0,0x14000114,0xb400229f,"shared char accel"},
    {0x198eb04,0x2f8,0xd10503ff,0x204,0x17ffffd9,0xb4fffb3f,"char collision predict"},
    {0x1a08560,0x4bb0,0xd103c3ff,0x3a0,0x14000007,0xb40000ff,"player controller"},
    {0x1a0d9dc,0x11c,0xd10203ff,0xfc,0x17ffffd2,0xb4fffa5f,"player move helper"},
    {0x1a0e0ec,0x118c,0xa9ba6ffc,0x510,0x14000007,0xb40000ff,"player move 1A0E0EC"},
    {0x1a0f278,0x334,0xd10203ff,0xd4,0x17ffffd7,0xb4fffaff,"player method 1A0F278"},
    {0x1a14f80,0xc54,0xd10283ff,0x140,0x14000002,0xb400005f,"player vertical"},
    {0x1a173b0,0x22a4,0xfc180fea,0x1b0,0x1400000b,0xb400017f,"player motion 1A173B0"},
    {0x1a19654,0x220,0xd10143ff,0xa0,0x1400003a,0xb400075f,"player alt helper"},
    {0x1aab534,0x3c0,0xa9ba6ffc,0x188,0x17ffffd8,0xb4fffb1f,"countdown 1AAB534"},
    {0x1b47d10,0x1a70,0xd10343ff,0x204,0x17ffff97,0xb4fff2ff,"weather blend"},
    {0x1d1b454,0x1f48,0x6db63bef,0x8fc,0x14000002,0xb400005f,"player-state predict"},
    {0x1d5d59c,0x528,0xd10203ff,0x1f8,0x17fffff4,0xb4fffe9f,"world time feed"},
    {0x1e1a25c,0x17dc,0xd105c3ff,0x11c,0x14000005,0xb40000bf,"cave light easing"},
    {0x24ac8c4,0x318,0xfc1d0fe8,0x1b8,0x17ffffab,0xb4fff57f,"world time update"},
    {0x2527e8c,0x4b8,0xd10183ff,0x240,0x14000007,0xb40000ff,"pokemon 2527E8C"},
    {0x252b124,0xc50,0xd10203ff,0x1b4,0x14000007,0xb40000ff,"pokemon 252B124"},
    {0x252d448,0x12b0,0xa9ba6ffc,0x4ac,0x14000007,0xb40000ff,"pokemon 252D448"},
    {0x257abb4,0x3bc,0xd10203ff,0x1a8,0x17ffffac,0xb4fff59f,"pokemon/ride motion"},
    {0x257af70,0x20d0,0xd10383ff,0x158,0x17fffff1,0xb4fffe3f,"pokemon/ride accel"},
    {0x25a8a84,0xa58,0xd10343ff,0x378,0x14000007,0xb40000ff,"pokemon/ride 25A8A84"},
    {0x25abd58,0xcfc,0xd10483ff,0x98,0x140000a0,0xb400141f,"pokemon/ride gravity"},
};
#define CENSUS_N (sizeof census_sites / sizeof census_sites[0])
static struct { u64 pc, calls, probe, probe_calls; GuestFn original; } census[CENSUS_N];
static int census_active;
/* Read-only gravity probe on the shared acceleration owner. Its airborne branch writes
   -g x factor x rate / 30 to the result's vertical term. One object at a time, while that
   term is negative, each call logs the velocity read from x1+120, the x2 position and the
   result at +40 with real time, so a fall shows whether velocity gains it once per frame. */
static int gravity_probe_active;
static u64 gravity_object, gravity_skip, gravity_skip_ns, gravity_start_ns, gravity_calls, gravity_lines;
#define GRAVITY_SEGMENT_NS 4000000000ull
#define GRAVITY_LINE_LIMIT 40000ull
static void gravity_end(u64 object, u64 now, const char* why) {
    LOG("arceus-gravity: end object=%llx calls=%llu seconds=%.4f reason=%s\n", (unsigned long long)object,
        (unsigned long long)gravity_calls, (double)(now - gravity_start_ns) / 1e9, why);
    __atomic_store_n(&gravity_object, 0, __ATOMIC_RELEASE);
}
static void gravity_probe(CPU* c, u64 entry, GuestFn original) {
    const u64 object = c->x[0], in = c->x[1], pose = c->x[2], out = c->x[8];
    float v[3] = {0}, p[3] = {0};
    const int inputs = in && pose && readable(in + 0x120, 12) && readable(pose, 12);
    for (int k = 0; inputs && k < 3; k++) {
        v[k] = guest_float(in + 0x120 + 4 * k);
        p[k] = guest_float(pose + 4 * k);
    }
    original(c, entry);
    if (!inputs || !out || !readable(out + 0x40, 12)) return;
    u64 owner = __atomic_load_n(&gravity_object, __ATOMIC_ACQUIRE);
    if (owner && owner != object) return;
    const float ox = guest_float(out + 0x40), oy = guest_float(out + 0x44), oz = guest_float(out + 0x48);
    const u64 now = plat_ns();
    if (!(oy < 0.0f)) {
        if (owner) gravity_end(object, now, "landed");
        return;
    }
    if (!owner) {
        /* An object that filled a whole segment waits 10 s, so others get a turn. */
        if (object == gravity_skip && now - gravity_skip_ns < 10000000000ull) return;
        if (__atomic_load_n(&gravity_lines, __ATOMIC_RELAXED) >= GRAVITY_LINE_LIMIT) return;
        if (!__atomic_compare_exchange_n(&gravity_object, &owner, object, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        gravity_start_ns = now;
        gravity_calls = 0;
        const u64 config = readable(object + 0x5c0, 8) ? gr64(object + 0x5c0) : 0;
        LOG("arceus-gravity: start object=%llx g=%g factor=%g max_fall=%g damping=%g\n",
            (unsigned long long)object, (double)guest_float(main_base + 0x413a0f8),
            (double)guest_float(object + 0xbe8),
            config && readable(config + 0x80, 4) ? (double)guest_float(config + 0x80) : 0.0,
            (double)guest_float(object + 0x920));
    }
    gravity_calls++;
    __atomic_add_fetch(&gravity_lines, 1, __ATOMIC_RELAXED);
    LOG("arceus-gravity: t=%.6f call=%llu v_in=%g,%g,%g pos=%g,%g,%g out=%g,%g,%g\n",
        (double)(now - gravity_start_ns) / 1e9, (unsigned long long)gravity_calls, (double)v[0], (double)v[1],
        (double)v[2], (double)p[0], (double)p[1], (double)p[2], (double)ox, (double)oy, (double)oz);
    if (now - gravity_start_ns >= GRAVITY_SEGMENT_NS) {
        gravity_skip = object;
        gravity_skip_ns = now;
        gravity_end(object, now, "segment limit");
    }
}
static void cadence_census_wrapper(CPU* c, u64 entry) {
    for (unsigned i = 0; i < CENSUS_N; i++) {
        if (census[i].pc != entry) continue;
        __atomic_add_fetch(&census[i].calls, 1, __ATOMIC_RELAXED);
        u64 probe = __atomic_load_n(&census[i].probe, __ATOMIC_RELAXED);
        /* On failure the exchange loads the winner into probe. */
        if (!probe && __atomic_compare_exchange_n(&census[i].probe, &probe, c->x[0], 0,
                                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            probe = c->x[0];
        if (probe == c->x[0]) __atomic_add_fetch(&census[i].probe_calls, 1, __ATOMIC_RELAXED);
        if (gravity_probe_active && census_sites[i].offset == 0x198d7e4)
            gravity_probe(c, entry, census[i].original);
        else
            census[i].original(c, entry);
        return;
    }
}
/* Called from the main-world physics probe only, so one thread owns the history. */
static void cadence_census_report(u64 frames) {
    static u64 last_ns, last_frames, last_calls[CENSUS_N], last_probe[CENSUS_N];
    if (!census_active) return;
    const u64 now = plat_ns();
    if (!last_ns) { last_ns = now; last_frames = frames; return; }
    if (now - last_ns < 5000000000ull) return;
    const double seconds = (double)(now - last_ns) / 1e9;
    LOG("arceus-cadence: window=%.2fs frames_per_s=%.1f\n", seconds, (double)(frames - last_frames) / seconds);
    for (unsigned i = 0; i < CENSUS_N; i++) {
        if (!census[i].original) continue;
        const u64 calls = __atomic_load_n(&census[i].calls, __ATOMIC_RELAXED);
        const u64 probe_calls = __atomic_load_n(&census[i].probe_calls, __ATOMIC_RELAXED);
        const u64 probe = __atomic_load_n(&census[i].probe, __ATOMIC_RELAXED);
        if (calls != last_calls[i])
            LOG("arceus-cadence: site=main+%x label=\"%s\" calls_per_s=%.1f object_calls_per_s=%.1f object=%llx\n",
                census_sites[i].offset, census_sites[i].label, (double)(calls - last_calls[i]) / seconds,
                (double)(probe_calls - last_probe[i]) / seconds, (unsigned long long)probe);
        /* The day clock keeps in-game minutes (0..1440) as a double at +70. */
        if (census_sites[i].offset == 0x24ac8c4 && probe && readable(probe + 0x70, 8)) {
            const u64 bits = gr64(probe + 0x70);
            double minutes;
            memcpy(&minutes, &bits, sizeof minutes);
            LOG("arceus-cadence: world_minutes=%.3f\n", minutes);
        }
        /* Resample next window if the sampled object went quiet (destroyed or idle). */
        if (probe_calls == last_probe[i]) __atomic_store_n(&census[i].probe, 0, __ATOMIC_RELAXED);
        last_calls[i] = calls;
        last_probe[i] = probe_calls;
    }
    last_ns = now;
    last_frames = frames;
}
static void cadence_census_init(void) {
    const char* enabled = getenv("SNX_ARCEUS_CADENCE_CENSUS");
    if (!main_base || !enabled || strcmp(enabled, "1") || !research_observing) return;
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    unsigned installed = 0, owned = 0;
    for (unsigned k = 0; k < CENSUS_N; k++) {
        const u64 pc = main_base + census_sites[k].offset, size = census_sites[k].size;
        int covered = 0;
        /* A fix already wraps this owner through the same rewritten branch. */
        if (readable(pc + census_sites[k].patch, 4) && gr32(pc + census_sites[k].patch) == census_sites[k].replacement) {
            owned++;
            continue;
        }
        for (u32 i = 0; i < g_mod->nfns; i++)
            if (g_mod->fn_lo[i] == pc && g_mod->fn_len[i] == size) covered = 1;
        if (!covered || pc < g_code_lo || pc + size > g_code_hi || !readable(pc, size) ||
            gr32(pc) != census_sites[k].head || gr32(pc + census_sites[k].patch) != census_sites[k].word) {
            WARN("arceus-cadence: refused guard site=main+%x\n", census_sites[k].offset);
            continue;
        }
        /* Equivalent B -> CBZ XZR to the same in-function target. */
        gw32(pc + census_sites[k].patch, census_sites[k].replacement);
        jit_code_changed(pc, size);
        GuestFn original = jit_wait(pc);
        if (!original) {
            WARN("arceus-cadence: compile failed site=main+%x\n", census_sites[k].offset);
            continue;
        }
        census[k].pc = pc;
        census[k].original = original;
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], cadence_census_wrapper, __ATOMIC_RELEASE);
        installed++;
    }
    census_active = installed != 0;
    LOG("arceus-cadence: read-only census installed %u of %u owners, %u wrapped by fixes\n",
        installed, (unsigned)CENSUS_N, owned);
    const char* gravity = getenv("SNX_ARCEUS_GRAVITY_PROBE");
    if (!gravity || strcmp(gravity, "1")) return;
    for (unsigned k = 0; k < CENSUS_N; k++)
        if (census_sites[k].offset == 0x198d7e4 && census[k].original) gravity_probe_active = 1;
    LOG("arceus-gravity: read-only probe %s\n", gravity_probe_active ? "installed" : "unavailable");
}

/* Sneasler wall climb: the climb mover 25540E8 moves direction x speed/30 per call, a cast
   distance with no elapsed time, so climbing ran four times fast at 120 FPS. With
   SNX_ARCEUS_CLIMB_STEP=1 its FMOV #30 and FDIV become a frame-step cell load and FMUL, room
   made by storing the +D20 copy with a second STUR instead of an LDP/STP pair through X9/X8. */
static const u32 climb_original_words[12] = {
    0x1e27d000, 0x1e201900, 0x3cd503a1, 0x4f809020, 0xaa1303e0, 0x3c9503a0,
    0x3d8027e0, 0x3dc34a60, 0x3c9403a0, 0xa97423a9, 0xa93323a9, 0x910d83e8,
};
/* ADRP X9 (filled in), LDR S0,[X9], FMUL S0,S8,S0, then the original stores and the +D20 copy. */
static const u32 climb_patched_words[12] = {
    0x90000009, 0xbd400120, 0x1e200900, 0x3cd503a1, 0x4f809020, 0xaa1303e0,
    0x3c9503a0, 0x3d8027e0, 0x3dc34a60, 0x3c9403a0, 0x3c9303a0, 0x910d83e8,
};
/* The climb animation 2549FA0 divides the eased distance |+D50| by speed/30 for its blend and
   playback ratio. At 254A8E0 FCMEQ, MVN, FSQRT, AND, FMOV #30, FDIV, FMOV #1, FDIV become FCMEQ,
   BIC (AND with NOT), FSQRT, ADRP X9, LDR S2, FMUL S2,S8,S2, FMOV #1, FDIV: speed x step. */
static const u32 climb_anim_words[8] = {
    0x4ea0d822, 0x6e205842, 0x6ea1f821, 0x4e221c00, 0x1e27d002, 0x1e221902, 0x1e2e1008, 0x1e22182a,
};
static const u32 climb_anim_patched[8] = {
    0x4ea0d822, 0x4e621c00, 0x6ea1f821, 0x90000009, 0xbd400122, 0x1e220902, 0x1e2e1008, 0x1e22182a,
};
/* The ADRP of a frame-step cell load at pc, for register rd. */
static u32 climb_adrp(u64 pc, u32 rd) {
    const u64 pages = ((frame_step_address >> 12) - (pc >> 12)) & 0x1fffff;
    return 0x90000000 | rd | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5);
}
/* SNX_ARCEUS_CLIMB_STEP=2 also fixes the climb's stop: in state +D00 == 2 the animation adds one
   to +D04 per call and eases over climbing_move_stop_frame calls. A call that does not complete
   a whole 1/30 s gets +D04 staged one lower, so the stop lasts its authored time. The fraction
   is kept per object and starts over with each stop, or when another writer changed +D04. */
#define CLIMB_STOP_SLOTS 4
static GuestFn climb_anim_original;
static int climb_stop_fix;
static struct { u64 object; float progress; u32 frame; } climb_stop_slots[CLIMB_STOP_SLOTS];
static int climb_stopping(u64 object) { return readable(object + 0xd00, 8) && gr32(object + 0xd00) == 2; }
/* Before the call: the object's slot when it was staged or counted, else -1. */
static int climb_stop_before(u64 object, float want) {
    if (!climb_stopping(object)) {
        for (int k = 0; k < CLIMB_STOP_SLOTS; k++)
            if (climb_stop_slots[k].object == object) climb_stop_slots[k].object = 0;
        return -1;
    }
    if (!writable(object + 0xd00, 8) || !(want > 0.0f && want <= 1.0f)) return -1;
    int slot = -1;
    for (int k = 0; k < CLIMB_STOP_SLOTS && slot < 0; k++)
        if (climb_stop_slots[k].object == object) slot = k;
    for (int k = 0; k < CLIMB_STOP_SLOTS && slot < 0; k++)
        if (!climb_stop_slots[k].object) slot = k;
    if (slot < 0) return -1;
    const u32 frame = gr32(object + 0xd04);
    if (climb_stop_slots[slot].object != object || climb_stop_slots[slot].frame != frame) {
        climb_stop_slots[slot].object = object;
        climb_stop_slots[slot].progress = 0.0f;
    }
    float progress = climb_stop_slots[slot].progress + want;
    if (progress < 1.0f) gw32(object + 0xd04, frame - 1);
    else progress -= 1.0f;
    climb_stop_slots[slot].progress = progress;
    return slot;
}
/* After the call: remember the count it left, or free the slot once the stop ended. */
static void climb_stop_after(u64 object, int slot) {
    if (slot < 0) return;
    if (climb_stopping(object)) climb_stop_slots[slot].frame = gr32(object + 0xd04);
    else climb_stop_slots[slot].object = 0;
}
static void climb_anim_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const int slot = climb_stop_fix ? climb_stop_before(object, guest_float(frame_step_address) / NOMINAL_STEP_SECONDS)
                                    : -1;
    climb_anim_original(c, entry);
    climb_stop_after(object, slot);
}
static void climb_step_init(void) {
    const int enabled = climb_step_value();
    if (!main_base || !enabled) return;
    if (!clocks_installed) {
        WARN("arceus-climb: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* The function head, the speed load before the pair, the BL after the twelve words and the words themselves. */
    const u64 owner = main_base + 0x25540e8, pc = owner + 0x2f4;
    int ok = covers_exactly(owner, 0x2c38) && readable(owner, 0x2c38) && gr32(owner) == 0x6db63bef &&
             gr32(owner + 0x2dc) == 0xbd403188 && (gr32(pc + 0x30) & 0xfc000000) == 0x94000000;
    for (unsigned k = 0; ok && k < 12; k++) ok = gr32(pc + 4 * k) == climb_original_words[k];
    if (!ok) {
        WARN("arceus-climb: refused mover guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-climb")) return;
    for (unsigned k = 0; k < 12; k++) gw32(pc + 4 * k, k ? climb_patched_words[k] : climb_adrp(pc, 9));
    jit_code_changed(pc, 0x30);
    /* Without the animation ratio fix the ratio would be 1/4 at 120 FPS, a near-still blend. */
    const u64 anim = main_base + 0x2549fa0, at = anim + 0x940;
    int anim_ok = covers_exactly(anim, 0xef4) && readable(anim, 0xef4) && gr32(anim) == 0xd10303ff &&
                  gr32(anim + 0x38) == 0xaa0003f3 && gr32(anim + 0x880) == 0xbd403188;
    for (unsigned k = 0; anim_ok && k < 8; k++) anim_ok = gr32(at + 4 * k) == climb_anim_words[k];
    if (anim_ok) {
        for (unsigned k = 0; k < 8; k++) gw32(at + 4 * k, k == 3 ? climb_adrp(at + 12, 9) : climb_anim_patched[k]);
        /* The state 2 check, the +D04 load, add and store, and the reload and signed compare before the finish. */
        const int stop_ok = enabled == 2 && gr32(anim + 0x1e4) == 0x7100091f &&
                            gr32(anim + 0x1ec) == 0xb94d0668 && gr32(anim + 0x1f0) == 0x11000508 &&
                            gr32(anim + 0x1f4) == 0xb90d0668 && gr32(anim + 0x308) == 0xb94d0668 &&
                            gr32(anim + 0x30c) == 0x6b14011f;
        if (enabled == 2 && !stop_ok) WARN("arceus-climb: refused stop guard\n");
        if (stop_ok) gw32(anim + 0x38, 0x91000013); /* Equivalent MOV X19,X0 -> ADD X19,X0,#0. */
        jit_code_changed(anim, 0xef4);
        if (stop_ok) {
            climb_anim_original = jit_wait(anim);
            if (climb_anim_original) {
                climb_stop_fix = 1;
                __atomic_store_n(&g_disp[(anim - g_code_lo) / 4], climb_anim_update, __ATOMIC_RELEASE);
            }
        }
    } else {
        WARN("arceus-climb: refused animation guard\n");
    }
    LOG("arceus-climb: EXPERIMENT: Sneasler climb distance uses min(1/30, elapsed), animation ratio %s, stop %s\n",
        anim_ok ? "on" : "off", climb_stop_fix ? "on" : "off");
}

/* Sneasler wall jump: the ride script (Lua 5.3) sets climbingJumpWaitCount, the bound field +F4
   of the object its RideNyuuraStateContext userdata points to, to 15 and counts it down one per
   update, so the wait after a jump up the wall took a quarter of its 0.5 s at 120 FPS. With
   SNX_ARCEUS_WALL_JUMP_FIX=1 a one-step countdown write converts as the old value until 1/30 s. */
#define WALL_JUMP_SLOTS 4
static GuestFn wall_jump_original;
static struct { u64 object; float progress; int value; } wall_jump_slots[WALL_JUMP_SLOTS];
/* 1 when a write of next over old should keep old: a step down that does not complete a whole
   1/30 s. Any other write (the set to 15, 0 to 0) ends the object's fraction. */
static int wall_jump_hold(u64 object, int old, int next, float want) {
    int slot = -1;
    for (int k = 0; k < WALL_JUMP_SLOTS && slot < 0; k++)
        if (wall_jump_slots[k].object == object) slot = k;
    if (old <= 0 || next != old - 1) {
        if (slot >= 0) wall_jump_slots[slot].object = 0;
        return 0;
    }
    for (int k = 0; k < WALL_JUMP_SLOTS && slot < 0; k++)
        if (!wall_jump_slots[k].object) slot = k;
    if (slot < 0 || !(want > 0.0f && want <= 1.0f)) return 0;
    /* A new countdown, or another writer changed the field: start the fraction over. */
    if (wall_jump_slots[slot].object != object || wall_jump_slots[slot].value != old) {
        wall_jump_slots[slot].object = object;
        wall_jump_slots[slot].progress = 0.0f;
    }
    float progress = wall_jump_slots[slot].progress + want;
    const int hold = progress < 1.0f;
    if (!hold) progress -= 1.0f;
    wall_jump_slots[slot].progress = progress;
    wall_jump_slots[slot].value = hold ? old : next;
    return hold;
}
/* A short Lua string (TString tag 4 at +8, length at +B, bytes at +18) equal to text. */
static int lua_short_string_is(u64 ts, const char* text, u32 length) {
    if (!readable(ts, 0x18 + length + 3) || (gr32(ts + 8) & 0xff) != 4 || ((gr32(ts + 8) >> 24) & 0xff) != length)
        return 0;
    for (u32 k = 0; k < length; k++)
        if ((gr32(ts + 0x18 + k) & 0xff) != (u8)text[k]) return 0;
    return 1;
}
/* lua_tointegerx 50BA0 (L, index, isnum). The ride setter (caller 2331B64 in the C closure
   2335E5C) converts the value, stack slot 3, of a write whose key in slot 2 is the counter. */
static void wall_jump_update(CPU* c, u64 entry) {
    const u64 L = c->x[0], caller = c->x[30];
    const int index = (int)(u32)c->x[1];
    wall_jump_original(c, entry);
    if (caller != main_base + 0x2331b64 || !(index == 3 || (index < 0 && index >= -8))) return;
    if (!readable(L + 0x10, 0x18) || !readable(gr64(L + 0x20), 8)) return;
    const u64 func = gr64(gr64(L + 0x20)), top = gr64(L + 0x10);
    const u64 slot = index > 0 ? func + 0x10 * (u64)index : top - 0x10 * (u64)(-index);
    if (slot != func + 0x30 || top < func + 0x40 || !readable(func, 0x40) || (gr32(func + 8) & 0xff) != 0x66) return;
    const u64 closure = gr64(func);
    if (!readable(closure + 0x18, 8) || gr64(closure + 0x18) != main_base + 0x2335e5c) return;
    if ((gr32(func + 0x28) & 0xff) != 0x44 || !lua_short_string_is(gr64(func + 0x20), "climbingJumpWaitCount", 21))
        return;
    /* Slot 1 is the full userdata (tag 0x47) whose block holds the object pointer. */
    if ((gr32(func + 0x18) & 0xff) != 0x47) return;
    const u64 block = gr64(func + 0x10) + 0x28;
    const u64 object = readable(block, 8) ? gr64(block) : 0;
    const long long next = (long long)c->x[0];
    if (!object || !readable(object + 0xf4, 4) || next < -1 || next > 0x7fffffff) return;
    const int old = (int)gr32(object + 0xf4);
    if (wall_jump_hold(object, old, (int)next, guest_float(frame_step_address) / NOMINAL_STEP_SECONDS))
        c->x[0] = (u64)(long long)old;
}
static void wall_jump_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_WALL_JUMP_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-wall-jump: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x50ba0;
    /* STR X19,[SP,#-0x20]!, the ci load and MOV X19,X2 of the verified lapi.c body. */
    if (!covers_exactly(pc, 0x114) || !readable(pc, 0x114) || gr32(pc) != 0xf81e0ff3 || gr32(pc + 0xc) != 0xf9401009 ||
        gr32(pc + 0x10) != 0xaa0203f3) {
        WARN("arceus-wall-jump: refused guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-wall-jump")) return;
    gw32(pc + 0x10, 0x91000053); /* Equivalent MOV X19,X2 -> ADD X19,X2,#0. */
    jit_code_changed(pc, 0x114);
    wall_jump_original = jit_wait(pc);
    if (!wall_jump_original) {
        WARN("arceus-wall-jump: compile failed\n");
        return;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], wall_jump_update, __ATOMIC_RELEASE);
    LOG("arceus-wall-jump: EXPERIMENT: Sneasler wall jump wait counts down 30 times per second\n");
}

/* Sneasler ledge: the ride state script, run by RideNyuuraStatePreUpdate 26163E0 and PostUpdate
   2616AB0 once per update, steps the move over the top after EndClimbing(0) 2553EE0 per run, so
   it took 175 ms at 120 FPS instead of 700. With SNX_ARCEUS_LEDGE_FIX=1 the state whose script
   ends the climb runs its updates only on calls completing a whole 1/30 s, until EndClimbingEnd. */
typedef struct {
    u64 owner, controller, opened, frame;
    float progress, want, previous[4], target[4], direction[4];
    int open, pending, skip, positions, lagged;
} LedgeWindow;
/* A script target still owed to the controller after its window closed. */
typedef struct { u64 controller; float target[4], direction[4]; int valid; } LedgeFlush;
static void ledge_retire_into(LedgeWindow* w, LedgeFlush* flush) {
    if (flush && w->open && w->positions && w->lagged) {
        flush->controller = w->controller;
        for (int k = 0; k < 4; k++) {
            flush->target[k] = w->target[k];
            flush->direction[k] = w->direction[k];
        }
        flush->valid = 1;
    }
    *w = (LedgeWindow){0};
}
static void ledge_open(LedgeWindow* w, u64 owner, u64 controller, u64 now) {
    *w = (LedgeWindow){0};
    if (!owner) return;
    w->owner = owner;
    w->controller = controller;
    w->opened = now;
    w->open = 1;
}
static void ledge_retire(LedgeWindow* w) { ledge_retire_into(w, NULL); }
/* PreUpdate of object in engine frame: 1 to skip. The decision, a pass-through included, is
   kept for one PostUpdate of the same object in the same frame. A window over 2 s is retired. */
static int ledge_pre(LedgeWindow* w, LedgeFlush* flush, u64 object, u64 now, float want, u64 frame) {
    if (!w->open || object != w->owner) return 0;
    if (now < w->opened || now - w->opened >= 2000000000ull) {
        ledge_retire_into(w, flush);
        return 0;
    }
    w->pending = 1;
    w->frame = frame;
    w->skip = 0;
    w->want = want;
    if (!(want > 0.0f && want <= 1.0f)) return 0;
    float progress = w->progress + want;
    w->skip = progress < 1.0f;
    if (!w->skip) progress -= 1.0f;
    w->progress = progress;
    return w->skip;
}
static int ledge_post(LedgeWindow* w, u64 object, u64 frame) {
    if (!w->open || !w->pending || object != w->owner) return 0;
    /* A decision from another frame is stale: drop it and run. */
    w->pending = 0;
    return w->frame == frame && w->skip;
}
/* The script's set position on a run. On a short frame (more updates follow before the next
   run) the ride is placed at the previous script target and the new one is kept to blend
   toward. On a full 1/30 s frame it is placed at the request. Returns 1 when out is set. */
static int ledge_script_position(LedgeWindow* w, const float in[4], const float direction[4], float out[4]) {
    if (!w->open) return 0;
    const int lag = w->positions && w->want > 0.0f && w->want < 1.0f;
    for (int k = 0; k < 4; k++) {
        w->previous[k] = lag ? w->target[k] : in[k];
        w->target[k] = in[k];
        w->direction[k] = direction[k];
        out[k] = w->previous[k];
    }
    w->positions = 1;
    w->lagged = lag;
    return 1;
}
/* Before the owner's script runs: the target it last set, so it reads its own position. The
   ride now stands on it, so a blend before the next request starts there too. */
static int ledge_authoritative(LedgeWindow* w, u64 object, float out[4], float direction[4]) {
    if (!w->open || object != w->owner || !w->positions || !w->lagged) return 0;
    for (int k = 0; k < 4; k++) {
        out[k] = w->previous[k] = w->target[k];
        direction[k] = w->direction[k];
    }
    w->lagged = 0;
    return 1;
}
/* On a skipped update: the point the accumulated fraction has reached between the two. */
static int ledge_blend(const LedgeWindow* w, float out[4], float direction[4]) {
    if (!w->open || !w->positions) return 0;
    const float f = w->progress < 0.0f ? 0.0f : w->progress > 1.0f ? 1.0f : w->progress;
    for (int k = 0; k < 4; k++) {
        out[k] = k < 3 ? w->previous[k] + (w->target[k] - w->previous[k]) * f : w->target[k];
        direction[k] = w->direction[k];
    }
    return 1;
}
/* Around a running owner PostUpdate: the script target to show it, and afterwards the point
   the ride is presented at (previous), so the frame keeps its place on the delayed path. */
static int ledge_post_positions(const LedgeWindow* w, u64 object, float target[4], float presented[4],
                                float direction[4]) {
    if (!w->open || object != w->owner || !w->positions || !w->lagged) return 0;
    for (int k = 0; k < 4; k++) {
        target[k] = w->target[k];
        presented[k] = w->previous[k];
        direction[k] = w->direction[k];
    }
    return 1;
}
/* A set position on controller from anything but the script: the game moved the ride itself,
   so an open window stops steering (no flush) and an owed target is dropped. */
static void ledge_foreign_position(LedgeWindow* w, LedgeFlush* flush, u64 controller) {
    if (w->open && w->controller == controller) ledge_retire(w);
    if (flush->valid && flush->controller == controller) flush->valid = 0;
}
/* EndClimbing(type) or EndClimbingEnd (type -1) of controller, called from the script of
   state object current (0 outside any Pre/PostUpdate). */
static void ledge_end_call(LedgeWindow* w, LedgeFlush* flush, u64 controller, int type, u64 current, u64 now) {
    if (w->open && w->controller == controller) ledge_retire_into(w, flush);
    if (type == 0) ledge_open(w, current, controller, now);
}
static PMutex ledge_lock = PMUTEX_INIT;
static LedgeWindow ledge_window;
static LedgeFlush ledge_flush;
static _Thread_local u64 ledge_current_state;
static GuestFn ledge_originals[2], ledge_end_originals[2], ledge_set_position_original;
static int ledge_fix_enabled;
/* Only the ground move's direct position branch uses speed/30 as a position delta. */
#define DIRECT_MOVE_RETURN 0x1982e70
static int direct_move_fix;
static u64 trajectory_transform(u64 object) {
    if (!readable(object + 0x1a8, 8)) return 0;
    const u64 handle = gr64(object + 0x1a8);
    if (!handle || !readable(handle, 0x20) || !gr32(handle + 0xc)) return 0;
    const u64 target = gr64(handle + 0x18);
    return target > 0x50 && readable(target - 0x50, 0x180) ? target - 0x50 : 0;
}
static int direct_move_scale(const float current[3], const float requested[3], float ratio, float out[3]) {
    const float d[3] = {requested[0] - current[0], requested[1] - current[1], requested[2] - current[2]};
    const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(ratio > 0.0f) || !(ratio < 1.0f) || !isfinite(length) || !(length < 2.0f)) return 0;
    for (int k = 0; k < 3; k++) out[k] = current[k] + d[k] * ratio;
    return 1;
}
/* Calls the original set position 1983770 on controller from inside a wrapper, keeping every
   register and pc as they were, so the wrapper's own call goes on unchanged. The arguments go
   through the frame-step page, +100 and +110. */
static void ledge_place(CPU* c, u64 controller, const float position[4], const float direction[4]) {
    if (!ledge_set_position_original || !controller || !frame_step_address) return;
    u64 x[32];
    V128 v[32];
    const u64 pc = c->pc;
    memcpy(x, c->x, sizeof x);
    memcpy(v, c->v, sizeof v);
    const u64 scratch = frame_step_address + 0x100;
    for (int k = 0; k < 4; k++) {
        guest_float_write(scratch + 4 * k, position[k]);
        guest_float_write(scratch + 0x10 + 4 * k, direction[k]);
    }
    c->x[0] = controller;
    c->x[1] = scratch;
    c->x[2] = scratch + 0x10;
    c->pc = main_base + 0x1983770;
    ledge_set_position_original(c, main_base + 0x1983770);
    memcpy(c->x, x, sizeof x);
    memcpy(c->v, v, sizeof v);
    c->pc = pc;
}
static int ledge_take_flush(LedgeFlush* out) {
    pm_lock(&ledge_lock);
    const int valid = ledge_flush.valid;
    if (valid) *out = ledge_flush;
    ledge_flush.valid = 0;
    pm_unlock(&ledge_lock);
    return valid;
}
static void ledge_set_position(CPU* c, u64 entry);
static int set_position_wrap_init(void) {
    if (ledge_set_position_original) return 1;
    /* SUB SP,SP,#0xC0, then MOV X19,X0 and the +628 load after it. */
    const u64 set = main_base + 0x1983770;
    if (!covers_exactly(set, 0xb88) || !readable(set, 0xb88) || gr32(set) != 0xd10303ff ||
        gr32(set + 0x20) != 0xaa0003f3 || gr32(set + 0x24) != 0xf9431400) return 0;
    gw32(set + 0x20, 0x91000013);
    jit_code_changed(set, 0xb88);
    ledge_set_position_original = jit_wait(set);
    if (!ledge_set_position_original) return 0;
    __atomic_store_n(&g_disp[(set - g_code_lo) / 4], ledge_set_position, __ATOMIC_RELEASE);
    return 1;
}
static void ledge_update(CPU* c, u64 entry, unsigned post) {
    const u64 object = c->x[0];
    const float want = readable(object + 0x68, 8) ? guest_float(frame_step_address) / NOMINAL_STEP_SECONDS : -1.0f;
    float position[4], direction[4], presented[4];
    int blend = 0, authoritative = 0, post_target = 0;
    pm_lock(&ledge_lock);
    const u64 frame = __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE);
    const int skip = post ? ledge_post(&ledge_window, object, frame)
                          : ledge_pre(&ledge_window, &ledge_flush, object, plat_ns(), want, frame);
    const u64 controller = ledge_window.controller;
    if (skip && !post) blend = ledge_blend(&ledge_window, position, direction);
    if (!skip && !post) authoritative = ledge_authoritative(&ledge_window, object, position, direction);
    if (!skip && post) post_target = ledge_post_positions(&ledge_window, object, position, presented, direction);
    pm_unlock(&ledge_lock);
    /* A target owed by a window that just expired goes first. */
    LedgeFlush owed;
    if (ledge_take_flush(&owed)) ledge_place(c, owed.controller, owed.target, owed.direction);
    /* Both functions return +6C at once when no script is bound: a skipped update does the same,
       after placing the ride on the blended point. */
    if (skip) {
        if (blend) ledge_place(c, controller, position, direction);
        c->x[0] = gr32(object + 0x6c);
        c->pc = c->x[30];
        return;
    }
    if (authoritative || post_target) ledge_place(c, controller, position, direction);
    const u64 outer = ledge_current_state;
    ledge_current_state = object;
    ledge_originals[post](c, entry);
    ledge_current_state = outer;
    /* Back to the presented point after a PostUpdate that saw the target, if still behind. */
    if (post_target) {
        pm_lock(&ledge_lock);
        const int again = ledge_post_positions(&ledge_window, object, position, presented, direction);
        pm_unlock(&ledge_lock);
        if (again) ledge_place(c, controller, presented, direction);
    }
    /* The script closed its window during this run: settle the ride on its last target. */
    if (ledge_take_flush(&owed)) ledge_place(c, owed.controller, owed.target, owed.direction);
}
static void ledge_pre_update(CPU* c, u64 entry) { ledge_update(c, entry, 0); }
static void ledge_post_update(CPU* c, u64 entry) { ledge_update(c, entry, 1); }
static void ledge_end(CPU* c, u64 entry, unsigned finish) {
    const u64 controller = c->x[0];
    const int type = finish ? -1 : (int)(u32)c->x[1];
    ledge_end_originals[finish](c, entry);
    pm_lock(&ledge_lock);
    ledge_end_call(&ledge_window, &ledge_flush, controller, type, ledge_current_state, plat_ns());
    pm_unlock(&ledge_lock);
    /* Outside any state update nothing else would settle it: place the owed target now. */
    LedgeFlush owed;
    if (!ledge_current_state && ledge_take_flush(&owed)) ledge_place(c, owed.controller, owed.target, owed.direction);
}
/* The shared set position and rotation 1983770 (object, position, direction; 16 bytes each).
   The ledge script's calls come from the Lua binding at main+1FF6B5C. On a run in the window the
   ride goes to the previous script target, and any other caller on the controller ends steering. */
static void ledge_set_position(CPU* c, u64 entry) {
    const u64 object = c->x[0], caller = c->x[30];
    if (ledge_fix_enabled && caller != main_base + 0x1ff6b5c) {
        pm_lock(&ledge_lock);
        ledge_foreign_position(&ledge_window, &ledge_flush, object);
        pm_unlock(&ledge_lock);
    } else if (ledge_fix_enabled && readable(c->x[1], 16) && readable(c->x[2], 16)) {
        float in[4], direction[4], out[4];
        for (int k = 0; k < 4; k++) {
            in[k] = guest_float(c->x[1] + 4 * k);
            direction[k] = guest_float(c->x[2] + 4 * k);
        }
        pm_lock(&ledge_lock);
        const int replace =
            ledge_window.controller == object && ledge_script_position(&ledge_window, in, direction, out);
        /* A newer script position after a window closed supersedes the owed target. */
        if (!replace && ledge_flush.valid && ledge_flush.controller == object) ledge_flush.valid = 0;
        pm_unlock(&ledge_lock);
        if (replace) {
            const u64 scratch = frame_step_address + 0x120;
            for (int k = 0; k < 4; k++) guest_float_write(scratch + 4 * k, out[k]);
            c->x[1] = scratch;
        }
    }
    const u64 position = c->x[1], caller_pc = c->x[30];
    u32 saved[3];
    int scaled = 0;
    if (direct_move_fix && caller_pc == main_base + DIRECT_MOVE_RETURN && readable(position, 12) && writable(position, 12)) {
        const u64 transform = trajectory_transform(object);
        float current[3], requested[3], out[3];
        for (int k = 0; transform && k < 3; k++) {
            current[k] = guest_float(transform + 0xd0 + 4 * k);
            requested[k] = guest_float(position + 4 * k);
        }
        scaled = transform && direct_move_scale(current, requested, frame_step_ratio(), out);
        for (int k = 0; scaled && k < 3; k++) {
            saved[k] = gr32(position + 4 * k);
            guest_float_write(position + 4 * k, out[k]);
        }
    }
    ledge_set_position_original(c, entry);
    for (int k = 0; scaled && k < 3; k++) gw32(position + 4 * k, saved[k]);
}
static void ledge_end_start(CPU* c, u64 entry) { ledge_end(c, entry, 0); }
static void ledge_end_finish(CPU* c, u64 entry) { ledge_end(c, entry, 1); }
static void ledge_fix_init(void) {
    if (!main_base ||  !arceus_switch_enabled("SNX_ARCEUS_LEDGE_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-ledge: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    /* PreUpdate and PostUpdate: SUB SP,#0x60, the script check load, the no-script +6C load and
       MOV X20,X0. EndClimbing and EndClimbingEnd: the head, a MOV of X0 and the self load after it. */
    static const u32 offsets[4] = {0x26163e0, 0x2616ab0, 0x2553ee0, 0x2554008};
    static const u32 sizes[4] = {0x234, 0x234, 0x128, 0xe0};
    static const u32 heads[4] = {0xd10183ff, 0xd10183ff, 0xd100c3ff, 0xd10083ff};
    static const u32 moves[4] = {0x40, 0x40, 0x10, 0xc};
    static const u32 words[4] = {0xaa0003f4, 0xaa0003f4, 0xaa0003f3, 0xaa0003e9};
    static const u32 adds[4] = {0x91000014, 0x91000014, 0x91000013, 0x91000009};
    static const GuestFn wrappers[4] = {ledge_pre_update, ledge_post_update, ledge_end_start, ledge_end_finish};
    for (unsigned k = 0; k < 4; k++) {
        const u64 pc = main_base + offsets[k];
        int ok = covers_exactly(pc, sizes[k]) && readable(pc, sizes[k]) && gr32(pc) == heads[k] &&
                 gr32(pc + moves[k]) == words[k];
        if (ok && k < 2) ok = gr32(pc + 0x14) == 0xf9402449 && gr32(pc + 0x20) == 0xb9406c13;
        if (ok && k == 2) ok = gr32(pc + 0x14) == 0xf942e400;
        if (ok && k == 3) ok = gr32(pc + 0x10) == 0xf9460800;
        if (!ok) {
            WARN("arceus-ledge: refused guard at main+%x\n", offsets[k]);
            return;
        }
    }
    if (!frame_step_cell_init("arceus-ledge")) return;
    GuestFn originals[4];
    for (unsigned k = 0; k < 4; k++) {
        const u64 pc = main_base + offsets[k];
        gw32(pc + moves[k], adds[k]); /* Equivalent MOV -> ADD #0, for a body of its own. */
        jit_code_changed(pc, sizes[k]);
        originals[k] = jit_wait(pc);
        if (!originals[k]) {
            WARN("arceus-ledge: compile failed at main+%x\n", offsets[k]);
            return;
        }
    }
    ledge_originals[0] = originals[0];
    ledge_originals[1] = originals[1];
    ledge_end_originals[0] = originals[2];
    ledge_end_originals[1] = originals[3];
    ledge_fix_enabled = 1;
    if (!set_position_wrap_init()) WARN("arceus-ledge: smoothing refused set position guard\n");
    for (unsigned k = 0; k < 4; k++)
        __atomic_store_n(&g_disp[(main_base + offsets[k] - g_code_lo) / 4], wrappers[k], __ATOMIC_RELEASE);
    LOG("arceus-ledge: EXPERIMENT: Sneasler ledge move script runs 30 times per second, smoothing %s\n",
        ledge_set_position_original ? "on" : "off");
}

/* Script tick. The player, buddy and ride state scripts get no time: they count their own
   updates and call natives that apply a fixed amount per call, and each state class runs
   them once per rendered frame through its PreUpdate and PostUpdate bridge.
   SNX_ARCEUS_SCRIPT_TICK is a hex mask of state classes whose bridges then run only when a
   whole 1/30 s has built up: bit 0 Sneasler, 1 Ursaluna, 2 Braviary, 3 Basculegion,
   4 and 5 Wyrdeer's second and first class, 6 the buddy, 7 to 9 the three player classes.
   Unset it is 4, Braviary alone, which is the one bit that was played. The Sneasler bit
   refuses while the ledge fix owns its two bridges. */
#define SCRIPT_TICK_NS 33333334ll
/* One bridge: its size and first word, the load of the current state index +6C that it
   returns without a script, an optional third word, and a register move to swap for an
   equal ADD so the wrapper gets its own compiled body. */
typedef struct { u32 address, size, head, load_at, load, extra_at, extra, swap_at, swap_old, swap_new; } ScriptTickBridge;
/* input is the offset of PlayerInputInfo in the context x2, 0 when the context has none. */
typedef struct { ScriptTickBridge bridge[2]; u32 input; const char* name; } ScriptTickClass;
#define SCRIPT_TICK_RIDE(address, size, script) \
    {address, size, 0xd10183ff, 0x20, 0xb9406c13, 0x14, script, 0x40, 0xaa0003f4, 0x91000014}
/* Besides the script call the ride bridges do nothing. The buddy, player and first Wyrdeer
   ones also hold a reference to [x2] and check for a state change request, with no float
   work and no store to an object field, so a skipped call only delays that check. */
static const ScriptTickClass script_tick_classes[] = {
    {{SCRIPT_TICK_RIDE(0x26163e0, 0x234, 0xf9402449), SCRIPT_TICK_RIDE(0x2616ab0, 0x234, 0xf9402449)}, 0xf8, "Sneasler"},
    {{SCRIPT_TICK_RIDE(0x26203f8, 0x234, 0xf9402449), SCRIPT_TICK_RIDE(0x2620ac8, 0x234, 0xf9402449)}, 0xd8, "Ursaluna"},
    {{SCRIPT_TICK_RIDE(0x2624ddc, 0x234, 0xf9402449), SCRIPT_TICK_RIDE(0x26254ac, 0x234, 0xf9402449)}, 0xc8, "Braviary"},
    {{SCRIPT_TICK_RIDE(0x2611b18, 0x234, 0xf9400049), SCRIPT_TICK_RIDE(0x26121e8, 0x234, 0xf9400049)}, 0xa8, "Basculegion"},
    {{SCRIPT_TICK_RIDE(0x261cfb0, 0x250, 0xf9402449), SCRIPT_TICK_RIDE(0x261d200, 0x250, 0xf9402449)}, 0xb0, "Wyrdeer B"},
    {{SCRIPT_TICK_RIDE(0x261898c, 0x250, 0xf9402449),
      {0x2618d50, 0x3fc, 0xd10243ff, 0x3c, 0xb9406d33, 0, 0, 0x24, 0xaa0003e9, 0x91000009}}, 0xb0, "Wyrdeer A"},
    {{{0x17ec138, 0x2f4, 0xd10203ff, 0x70, 0xb9406e73, 0, 0, 0x14, 0xaa0003f3, 0x91000013},
      {0x17ec8ac, 0x1d8, 0xd101c3ff, 0x60, 0xb9406e73, 0, 0, 0x10, 0xaa0003f3, 0x91000013}}, 0, "buddy"},
    {{{0x1d15900, 0xd4, 0xd10143ff, 0x18, 0xb9406c00, 0, 0, 0x34, 0xaa0903e0, 0x91000120},
      {0x1d15b48, 0x2a4, 0xd101c3ff, 0x30, 0xb9406c14, 0, 0, 0x1c, 0xaa0203f4, 0x91000054}}, 0x14c, "player 1"},
    {{{0x1d20e10, 0xd4, 0xd10143ff, 0x18, 0xb9406c00, 0, 0, 0x34, 0xaa0903e0, 0x91000120},
      {0x1d20ee4, 0x39c, 0xd101c3ff, 0x30, 0xb9406c13, 0, 0, 0x1c, 0xaa0203f4, 0x91000054}}, 0x14c, "player 2"},
    {{{0x1d21894, 0x460, 0xd101c3ff, 0xc4, 0xb9406e75, 0, 0, 0x18, 0xaa0003f3, 0x91000013},
      {0x1d21cf4, 0x2a8, 0xd101c3ff, 0x30, 0xb9406c14, 0, 0, 0x1c, 0xaa0203f4, 0x91000054}}, 0x14c, "player 3"},
};
#define SCRIPT_TICK_CLASSES (sizeof script_tick_classes / sizeof script_tick_classes[0])
/* Indexes in script_tick_classes: the rides come first, then the buddy, then the player. */
#define SCRIPT_TICK_BUDDY 6
#define SCRIPT_TICK_PLAYER 7
/* The class (index + 1) and the state object (x0) of the bridge that is running, for the
   window that one of its script's natives opens. */
static _Thread_local unsigned script_class_running;
static _Thread_local u64 script_class_object;
static void script_class_run(CPU* c, u64 entry, unsigned class_index, GuestFn original) {
    const unsigned outer = script_class_running;
    const u64 outer_object = script_class_object;
    script_class_running = class_index + 1;
    script_class_object = c->x[0];
    original(c, entry);
    script_class_running = outer;
    script_class_object = outer_object;
}
/* The byte fields of PlayerInputInfo that are true for one frame (names ending in Trigger
   or Release in its binding 227BC90). */
static const u8 script_tick_edges[] = {1, 2, 4, 6, 8, 9, 10, 11, 15, 16, 17, 19, 23, 24, 26, 27, 28, 29, 30, 31, 33, 34};
#define SCRIPT_TICK_EDGES (sizeof script_tick_edges / sizeof script_tick_edges[0])
typedef struct { u64 frame; int64_t budget; int decided, tick; u64 frames, ticks; } ScriptTick;
typedef struct { u64 context, applied; u8 held[SCRIPT_TICK_EDGES]; } ScriptTickInput;
static unsigned long script_tick_mask, script_tick_wrapped;
static GuestFn script_tick_originals[SCRIPT_TICK_CLASSES][2];
static ScriptTick script_tick;
static ScriptTickInput script_tick_inputs[4];
static PMutex script_tick_lock = PMUTEX_INIT;
static u64 script_tick_ran, script_tick_skipped, script_tick_latched;
static _Thread_local unsigned script_tick_depth;
static unsigned long script_tick_wanted(void) {
    const char* value = getenv("SNX_ARCEUS_SCRIPT_TICK");
    if (!value) return 4;
    return *value ? strtoul(value, NULL, 16) & ((1ul << SCRIPT_TICK_CLASSES) - 1) : 0;
}
/* One decision per engine frame, shared by every bridge: the step, at most 1/30 s, is added
   to a budget and a tick takes 1/30 s from it. A slow frame gives one tick, never two. The
   tick comes on the frame nearest to the moment the budget is full: on the frame that brings
   it within half a step. Waiting for the full budget made a frame of 16.5 ms miss by a hair
   on every second frame, so at 60 FPS the runs came 3 and 1 frames apart instead of 2 and 2. */
static int script_tick_decide(ScriptTick* t, u64 frame, u64 step_ns) {
    if (t->decided && t->frame == frame) return t->tick;
    /* A frame within 3 % of 1/30 s counts as a whole tick, so at 30 FPS with its jitter every
       frame runs the script once, as the game does, and the budget cannot drift into a skip. */
    const int64_t step = (int64_t)step_ns < SCRIPT_TICK_NS - SCRIPT_TICK_NS / 32 ? (int64_t)step_ns : SCRIPT_TICK_NS;
    t->budget += step;
    t->tick = t->budget + step / 2 >= SCRIPT_TICK_NS;
    if (t->tick) {
        t->budget -= SCRIPT_TICK_NS;
        t->ticks++;
    }
    t->frames++;
    t->frame = frame;
    t->decided = 1;
    return t->tick;
}
/* The edge fields of one context. On a frame without a tick they are kept. Before a ticked
   bridge the kept ones are written into the fields (seen, for the caller to undo), and the
   first call of a later frame forgets them. Returns the number of fields changed. */
static unsigned script_tick_input(ScriptTickInput* slots, u64 context, u64 fields, u64 frame, int tick,
                                  u8 seen[SCRIPT_TICK_EDGES], u8 current[SCRIPT_TICK_EDGES]) {
    ScriptTickInput* slot = NULL;
    for (unsigned k = 0; k < 4 && !slot; k++)
        if (slots[k].context == context) slot = &slots[k];
    for (unsigned k = 0; k < 4 && !slot; k++)
        if (!slots[k].context) {
            slot = &slots[k];
            *slot = (ScriptTickInput){.context = context};
        }
    if (!slot) return 0;
    if (slot->applied && slot->applied != frame) {
        memset(slot->held, 0, sizeof slot->held);
        slot->applied = 0;
    }
    unsigned changed = 0;
    for (unsigned k = 0; k < SCRIPT_TICK_EDGES; k++) {
        current[k] = *GP(fields + script_tick_edges[k]);
        if (!tick) {
            if (current[k]) slot->held[k] = current[k];
            continue;
        }
        seen[k] = current[k] ? current[k] : slot->held[k];
        if (seen[k] != current[k]) {
            *GP(fields + script_tick_edges[k]) = seen[k];
            changed++;
        }
    }
    if (tick) slot->applied = frame;
    return changed;
}
/* Rotation blend for a ticked ride. Braviary's flight script computes its turn per update
   and hands the finished rotation to SetSwoopingRotation 25AB490, which copies it to the
   transform +C0 (25AB908). Set 30 times a second it steps. So between ticks the host shows
   slerp(R0, R1, elapsed / time to the next run): R1 is the script's last rotation, R0 what
   was shown when it came. Before a ticked run the object is put on R1, so the script reads its own value. A
   rotation that another writer changed ends the blend. At 30 FPS nothing is written. */
#define ROTATION_BLEND_CLASS 2
typedef struct { u64 object, frame; int64_t elapsed; int active; float from[4], to[4]; u32 shown[4]; } RotationBlend;
static RotationBlend rotation_blends[2];
static GuestFn rotation_blend_original;
static u64 rotation_blend_writes, rotation_blend_foreign, rotation_blend_dropped;
static u64 rotation_blend_frame, rotation_blend_state;
/* out = slerp(a, b, t) on the shorter arc. Close rotations are blended linearly. */
static void rotation_slerp(const float a[4], const float b[4], float t, float out[4]) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], sign = 1.0f;
    if (dot < 0.0f) {
        dot = -dot;
        sign = -1.0f;
    }
    float wa = 1.0f - t, wb = t;
    if (dot < 0.9995f) {
        const float angle = acosf(dot), inverse = 1.0f / sinf(angle);
        wa = sinf((1.0f - t) * angle) * inverse;
        wb = sinf(t * angle) * inverse;
    }
    float length = 0.0f;
    for (unsigned k = 0; k < 4; k++) {
        out[k] = wa * a[k] + wb * sign * b[k];
        length += out[k] * out[k];
    }
    length = length > 0.0f ? 1.0f / sqrtf(length) : 1.0f;
    for (unsigned k = 0; k < 4; k++) out[k] *= length;
}
/* How far the blend is on a frame without a run: the time since the script's run against
   that time plus the time to its next run. The next run comes after as many frames of this
   step as the tick budget still needs. Taking 1/30 s less the budget as the whole interval
   ended the blend a frame early at 60 FPS, where the budget misses a tick by a hair on every
   second frame, and the turn was drawn as a jump and a hold. */
static float rotation_blend_progress(const RotationBlend* b, int64_t budget, u64 step_ns) {
    const int64_t step = step_ns && (int64_t)step_ns < SCRIPT_TICK_NS ? (int64_t)step_ns : SCRIPT_TICK_NS;
    /* As script_tick_decide: the run comes on the frame that brings the budget within half a step. */
    const int64_t need = SCRIPT_TICK_NS - step / 2 - budget;
    const int64_t frames = need > 0 ? (need + step - 1) / step : 1;
    const double whole = (double)b->elapsed + (double)(frames * step);
    const float progress = whole > 0.0 ? (float)((double)b->elapsed / whole) : 1.0f;
    return progress < 1.0f ? progress : 1.0f;
}
/* Writes the script's rotation (target) or the blended one to the transform, with the
   dirty byte as 25AB914 sets it. The transform must still hold what was written last. */
static void rotation_blend_place(RotationBlend* b, int target, float progress) {
    const u64 transform = b->active ? trajectory_transform(b->object) : 0;
    if (!b->active) return;
    if (!transform || !writable(transform + 0xc0, 0x24)) {
        b->active = 0;
        return;
    }
    for (unsigned k = 0; k < 4; k++)
        if (gr32(transform + 0xc0 + 4 * k) != b->shown[k]) {
            __atomic_add_fetch(&rotation_blend_foreign, 1, __ATOMIC_RELAXED);
            b->active = 0;
            return;
        }
    float q[4];
    if (target || progress >= 1.0f) memcpy(q, b->to, sizeof q);
    else rotation_slerp(b->from, b->to, progress, q);
    for (unsigned k = 0; k < 4; k++) {
        b->shown[k] = float_bits(q[k]);
        gw32(transform + 0xc0 + 4 * k, b->shown[k]);
    }
    *GP(transform + 0xe0) = 1;
    __atomic_add_fetch(&rotation_blend_writes, 1, __ATOMIC_RELAXED);
}
/* Called with script_tick_lock held, once per bridge call of the blended class. stage 0:
   a frame without a tick, 1: before a ticked run, 2: after it. */
static void rotation_blend_bridge(u64 frame, u64 step_ns, int stage, u64 state) {
    /* A blend belongs to the state that set it, on consecutive frames. After a frame without
       a bridge call or under another state object the ride may be gone: let go, write nothing. */
    const int stale = frame > rotation_blend_frame + 1 || state != rotation_blend_state;
    rotation_blend_frame = frame;
    rotation_blend_state = state;
    for (unsigned k = 0; k < 2; k++) {
        RotationBlend* b = &rotation_blends[k];
        if (!b->active) continue;
        if (stale) {
            b->active = 0;
            __atomic_add_fetch(&rotation_blend_dropped, 1, __ATOMIC_RELAXED);
            continue;
        }
        if (b->frame != frame) {
            b->frame = frame;
            b->elapsed += (int64_t)step_ns;
        }
        rotation_blend_place(b, stage == 1, rotation_blend_progress(b, script_tick.budget, step_ns));
        /* Before a later run the object is on the target. If that run sets no new rotation it
           stays there. The PostUpdate of the frame that set the rotation is not a later run. */
        if (stage == 1 && b->elapsed > 0) memcpy(b->from, b->to, sizeof b->from);
    }
}
/* 25AB490 (x0 object, x1 quaternion). Inside a ticked run above 30 FPS its rotation becomes
   the new target and what was shown becomes the start. Otherwise the object is let go. */
static void rotation_blend_set(CPU* c, u64 entry) {
    const u64 object = c->x[0], argument = c->x[1];
    const u64 transform = trajectory_transform(object);
    /* No blend when every frame is a run, with the tolerance script_tick_decide has for a
       frame near 1/30 s. A blend there would draw each rotation one run late. */
    const int blend = script_tick_depth && frame_step_ratio() < 1.0f - 1.0f / 32 && readable(argument, 16) && transform;
    float to[4] = {0, 0, 0, 0}, from[4] = {0, 0, 0, 0};
    RotationBlend* slot = NULL;
    pm_lock(&script_tick_lock);
    for (unsigned k = 0; k < 2 && !slot; k++)
        if (rotation_blends[k].object == object) slot = &rotation_blends[k];
    for (unsigned k = 0; blend && k < 2 && !slot; k++)
        if (!rotation_blends[k].active) slot = &rotation_blends[k];
    if (blend && slot) {
        for (unsigned k = 0; k < 4; k++) to[k] = guest_float(argument + 4 * k);
        /* A new rotation only comes in a run, and before the run the object was put on the old
           target. So the new blend starts from there. */
        if (slot->object == object && slot->active) memcpy(from, slot->to, sizeof from);
        else
            for (unsigned k = 0; k < 4; k++) from[k] = guest_float(transform + 0xc0 + 4 * k);
    }
    pm_unlock(&script_tick_lock);
    rotation_blend_original(c, entry);
    pm_lock(&script_tick_lock);
    if (slot && !blend) slot->active = 0;
    if (slot && blend) {
        slot->object = object;
        slot->frame = __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE);
        slot->elapsed = 0;
        memcpy(slot->from, from, sizeof from);
        memcpy(slot->to, to, sizeof to);
        slot->active = 1;
        /* The native has just written the target. If it did not, there is nothing to blend. */
        for (unsigned k = 0; k < 4; k++) {
            slot->shown[k] = float_bits(to[k]);
            if (gr32(transform + 0xc0 + 4 * k) != slot->shown[k]) slot->active = 0;
        }
    }
    pm_unlock(&script_tick_lock);
}
static int rotation_blend_init(void) {
    const u64 pc = main_base + 0x25ab490;
    /* SUB SP,#0x90, MOV X19,X1, MOV X20,X0, and the copy of the argument to transform +C0. */
    if (!covers_exactly(pc, 0x65c) || !readable(pc, 0x65c) || gr32(pc) != 0xd10243ff || gr32(pc + 0x2c) != 0xaa0103f3 ||
        gr32(pc + 0x30) != 0xaa0003f4 || gr32(pc + 0x478) != 0xa9402668 || gr32(pc + 0x47c) != 0xa90c2408) {
        WARN("arceus-script-tick: refused rotation blend guard at main+25ab490\n");
        return 0;
    }
    gw32(pc + 0x30, 0x91000014); /* Equivalent MOV X20,X0 -> ADD X20,X0,#0. */
    jit_code_changed(pc, 0x65c);
    rotation_blend_original = jit_wait(pc);
    if (!rotation_blend_original) {
        WARN("arceus-script-tick: compile failed at main+25ab490\n");
        return 0;
    }
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], rotation_blend_set, __ATOMIC_RELEASE);
    return 1;
}
/* One-frame answers. A ticked script asks its questions 30 times a second, so an answer
   that is only true for some frames between two runs would be missed: the release event of
   a throw (CheckThrowEvent, which the throw script waits for during 19 updates). For the
   native in this table, on frames without a run the host asks again for the object the
   script asked at its latest run, and the next run is told yes if any of those answers was
   yes. It is a read of one byte, without side effects.
   The host only asks while the object can be taken as alive: the same state object is
   bridged on consecutive frames since the script asked, and the object's first word is what
   it was then. An earlier form also asked for a roll and a turn trigger and crashed on an
   object of an earlier throw that had been released. Those two are not asked again. */
typedef struct { u32 address, size, head, second_at, second; int windowed; const char* name; } ScriptQuery;
static const ScriptQuery script_queries[] = {
    {0x1955fe0, 0x8, 0x3966e000, 4, 0xd65f03c0, 1, "CheckThrowEvent"}, /* LDRB W0,[X0,#0x9B8], RET */
};
#define SCRIPT_QUERIES (sizeof script_queries / sizeof script_queries[0])
typedef struct { GuestFn original; u64 object, word, asked, shown, filled; int held; } ScriptQueryState;
static ScriptQueryState script_query_states[SCRIPT_QUERIES];
static u64 script_query_run, script_query_last, script_query_state, script_query_dropped;
/* The script's own call. Inside a ticked run it is told yes when a skipped frame said yes. */
static void script_query_check(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    unsigned k = 0;
    while (k < SCRIPT_QUERIES - 1 && entry != main_base + script_queries[k].address) k++;
    ScriptQueryState* q = &script_query_states[k];
    q->original(c, entry);
    if (!script_tick_depth) return;
    pm_lock(&script_tick_lock);
    q->object = readable(object, 8) ? object : 0;
    q->word = q->object ? gr64(object) : 0;
    q->asked = script_query_run;
    if (q->held && !(c->x[0] & 1)) {
        c->x[0] = 1;
        q->filled++;
    }
    pm_unlock(&script_tick_lock);
}
/* From a player or buddy bridge outside any run. run is 1 before a ticked run. On other
   frames each query the script asked at its latest run is asked again, with every register
   put back afterwards. An answer shown to a run is forgotten on the next frame. windowed:
   the class is only ticked by a window. */
static void script_query_frame(CPU* c, u64 frame, int run, int windowed) {
    const u64 state = c->x[0];
    pm_lock(&script_tick_lock);
    /* Not the frame after the last one, or another state object: nothing asked before counts. */
    if (frame > script_query_last + 1 || (!run && state != script_query_state)) {
        for (unsigned k = 0; k < SCRIPT_QUERIES; k++) {
            ScriptQueryState* q = &script_query_states[k];
            if (q->object) script_query_dropped++;
            q->object = q->word = q->shown = 0;
            q->held = 0;
        }
    }
    script_query_last = frame;
    if (run) script_query_state = state;
    pm_unlock(&script_tick_lock);
    for (unsigned k = 0; k < SCRIPT_QUERIES; k++) {
        ScriptQueryState* q = &script_query_states[k];
        if (!q->original) continue;
        pm_lock(&script_tick_lock);
        if (q->shown && q->shown != frame) {
            q->held = 0;
            q->shown = 0;
        }
        if (run && q->held) q->shown = frame;
        u64 object = !run && q->asked == script_query_run && (!windowed || script_queries[k].windowed) ? q->object : 0;
        /* A released object does not keep its first word for long. */
        if (object && (!readable(object, 0xe00) || gr64(object) != q->word)) {
            q->object = object = 0;
            script_query_dropped++;
        }
        pm_unlock(&script_tick_lock);
        if (!object) continue;
        u64 x[32];
        V128 v[32];
        const u64 pc = c->pc;
        memcpy(x, c->x, sizeof x);
        memcpy(v, c->v, sizeof v);
        c->x[0] = object;
        c->pc = main_base + script_queries[k].address;
        q->original(c, main_base + script_queries[k].address);
        const int yes = (int)(c->x[0] & 1);
        memcpy(c->x, x, sizeof x);
        memcpy(c->v, v, sizeof v);
        c->pc = pc;
        if (!yes) continue;
        pm_lock(&script_tick_lock);
        q->held = 1;
        pm_unlock(&script_tick_lock);
    }
    if (run) {
        pm_lock(&script_tick_lock);
        script_query_run = frame;
        pm_unlock(&script_tick_lock);
    }
}
/* The natives are reached from scripts through the binding's indirect call only, so their
   dispatch entries are wrapped without changing a word. */
static void script_query_init(void) {
    for (unsigned k = 0; k < SCRIPT_QUERIES; k++) {
        const ScriptQuery* row = &script_queries[k];
        const u64 pc = main_base + row->address;
        if (script_query_states[k].original) continue;
        if (!covers_exactly(pc, row->size) || !readable(pc, row->size) || gr32(pc) != row->head ||
            gr32(pc + row->second_at) != row->second || !g_disp[(pc - g_code_lo) / 4]) {
            WARN("arceus-script-tick: refused query guard %s at main+%x\n", row->name, row->address);
            continue;
        }
        script_query_states[k].original = g_disp[(pc - g_code_lo) / 4];
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], script_query_check, __ATOMIC_RELEASE);
        LOG("arceus-script-tick: EXPERIMENT: %s is kept for the script's next run\n", row->name);
    }
}
/* Script windows: a class is ticked only while a named part of its script runs, as the ledge
   fix does for the ledge move. SNX_ARCEUS_SCRIPT_WINDOWS is a hex mask of windows.
   Bit 0, the throw: the player's throw script waits for the release event of the throw
   animation for 19 updates and then gives the throw up, so above 30 FPS a slow throw never
   leaves the hand. The window opens when the script calls OnStartThrow 1A91624 and closes
   at ThrowObject 1A8EBA4 or OnEndThrowStance 1A91370, or after 1 s. Inside it the three
   player classes run 30 times a second. Outside it they run on every frame as before. */
#define SCRIPT_WINDOW_NS 1000000000ull
typedef struct { u32 address, size, head, second; int window, open; const char* name; } ScriptWindowEvent;
static const ScriptWindowEvent script_window_events[] = {
    {0x1a91624, 0x4, 0xd65f03c0, 0, 0, 1, "OnStartThrow"},              /* RET alone */
    {0x1a8eba4, 0x26b0, 0x6db63bef, 0x6d0133ed, 0, 0, "ThrowObject"},   /* STP D15,D14,[SP,#-0xA0]!, STP D13,D12 */
    {0x1a91370, 0x1bc, 0xf81c0ff7, 0xa90157f6, 0, 0, "OnEndThrowStance"}, /* STR X23,[SP,#-0x40]!, STP X22,X21 */
};
#define SCRIPT_WINDOW_EVENTS (sizeof script_window_events / sizeof script_window_events[0])
/* The classes each window ticks, as a mask over script_tick_classes. */
static const unsigned long script_window_classes[] = {0x380};
#define SCRIPT_WINDOWS (sizeof script_window_classes / sizeof script_window_classes[0])
typedef struct { int open; unsigned owner; u64 state, seen_frame, opened_ns, opened_frame, opens, expired, interrupted; } ScriptWindow;
static ScriptWindow script_windows[SCRIPT_WINDOWS];
static GuestFn script_window_originals[SCRIPT_WINDOW_EVENTS];
static unsigned long script_windows_mask;
static u64 script_window_lines;
/* With the research switch, one line per closed window: what closed it and how long it was
   open. Lock held. */
static void script_window_closed(unsigned window, const char* by, u64 frame, u64 now) {
    const ScriptWindow* w = &script_windows[window];
    if (!research_observing || ++script_window_lines > 400) return;
    LOG("arceus-script-window: window=%u closed_by=%s frames=%llu seconds=%.3f owner=%u opens=%llu expired=%llu interrupted=%llu answers_dropped=%llu\n",
        window, by, (unsigned long long)(frame - w->opened_frame), (double)(now - w->opened_ns) / 1e9, w->owner,
        (unsigned long long)w->opens, (unsigned long long)w->expired, (unsigned long long)w->interrupted,
        (unsigned long long)script_query_dropped);
}
static unsigned long script_windows_wanted(void) {
    const char* value = getenv("SNX_ARCEUS_SCRIPT_WINDOWS");
    if (!value) return 1;
    return *value ? strtoul(value, NULL, 16) & ((1ul << SCRIPT_WINDOWS) - 1) : 0;
}
/* Whether a window ticks this class on this frame. A window counts from the frame after it
   opened, so the PreUpdate and PostUpdate of the opening frame both run. A window lives
   with the state whose script opened it: once that state object has not been bridged for a
   frame (a hit or anything else changed the state) the window is closed. Lock held. */
static int script_window_active(unsigned class_index, u64 frame, u64 now, u64 state) {
    int active = 0;
    for (unsigned k = 0; k < SCRIPT_WINDOWS; k++) {
        ScriptWindow* w = &script_windows[k];
        if (!w->open) continue;
        if (now - w->opened_ns > SCRIPT_WINDOW_NS) {
            w->open = 0;
            w->expired++;
            script_window_closed(k, "expired", frame, now);
            continue;
        }
        if (w->owner == class_index + 1 && w->state == state) w->seen_frame = frame;
        else if (w->owner && frame > w->seen_frame + 1) {
            w->open = 0;
            w->interrupted++;
            script_window_closed(k, "interrupted", frame, now);
            continue;
        }
        if ((script_window_classes[k] >> class_index & 1) && frame > w->opened_frame) active = 1;
    }
    return active;
}
/* The script's call of a native that opens or closes a window. */
static void script_window_event(CPU* c, u64 entry) {
    unsigned k = 0;
    while (k < SCRIPT_WINDOW_EVENTS - 1 && entry != main_base + script_window_events[k].address) k++;
    const ScriptWindowEvent* e = &script_window_events[k];
    pm_lock(&script_tick_lock);
    ScriptWindow* w = &script_windows[e->window];
    if (e->open && !w->open) {
        w->open = 1;
        w->opened_ns = plat_ns();
        w->opened_frame = w->seen_frame = __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE);
        w->owner = script_class_running;
        w->state = script_class_object;
        w->opens++;
    } else if (!e->open && w->open) {
        w->open = 0;
        script_window_closed((unsigned)e->window, e->name, __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE), plat_ns());
    }
    pm_unlock(&script_tick_lock);
    script_window_originals[k](c, entry);
}
/* Wraps the dispatch entries of one window's natives. All or none. */
static int script_window_init(unsigned window) {
    for (unsigned k = 0; k < SCRIPT_WINDOW_EVENTS; k++) {
        const ScriptWindowEvent* e = &script_window_events[k];
        const u64 pc = main_base + e->address;
        if (e->window != (int)window) continue;
        if (!covers_exactly(pc, e->size) || !readable(pc, e->size) || gr32(pc) != e->head ||
            (e->second && gr32(pc + 4) != e->second) || !g_disp[(pc - g_code_lo) / 4]) {
            WARN("arceus-script-window: refused guard %s at main+%x\n", e->name, e->address);
            return 0;
        }
    }
    for (unsigned k = 0; k < SCRIPT_WINDOW_EVENTS; k++) {
        const u64 pc = main_base + script_window_events[k].address;
        if (script_window_events[k].window != (int)window) continue;
        script_window_originals[k] = g_disp[(pc - g_code_lo) / 4];
        __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], script_window_event, __ATOMIC_RELEASE);
    }
    return 1;
}
static void script_tick_bridge(CPU* c, u64 entry, unsigned class_index, unsigned post) {
    const ScriptTickClass* cls = &script_tick_classes[class_index];
    /* A class that is only ticked inside a window runs on every other frame as it always did. */
    if (!(script_tick_mask >> class_index & 1)) {
        pm_lock(&script_tick_lock);
        const int active = script_window_active(class_index, __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE), plat_ns(), c->x[0]);
        pm_unlock(&script_tick_lock);
        if (!active) {
            script_class_run(c, entry, class_index, script_tick_originals[class_index][post]);
            return;
        }
    }
    const u64 object = c->x[0], context = c->x[2], fields = context + cls->input;
    const int usable = readable(object + 0x68, 8) && (!cls->input || writable(fields, 0x24));
    u8 seen[SCRIPT_TICK_EDGES], current[SCRIPT_TICK_EDGES];
    unsigned changed = 0;
    pm_lock(&step_lock);
    const u64 ns = step.step ? step.step : step.initial_ns;
    pm_unlock(&step_lock);
    pm_lock(&script_tick_lock);
    const u64 frame = __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE);
    /* A bridge called from inside a ticked one runs with it. */
    const int tick = !usable || script_tick_depth || script_tick_decide(&script_tick, frame, ns);
    if (usable && cls->input)
        changed = script_tick_input(script_tick_inputs, context, fields, frame, tick, seen, current);
    const int blended = usable && class_index == ROTATION_BLEND_CLASS && rotation_blend_original && !script_tick_depth;
    if (blended && (tick || !post)) rotation_blend_bridge(frame, ns, tick, object);
    const u64 frames = script_tick.frames, ticks = script_tick.ticks;
    pm_unlock(&script_tick_lock);
    if (usable && class_index >= SCRIPT_TICK_BUDDY && !script_tick_depth && !post)
        script_query_frame(c, frame, tick, !(script_tick_mask >> class_index & 1));
    if (!tick) {
        /* What the bridge returns without a script: the current state index. */
        const u64 skipped = __atomic_add_fetch(&script_tick_skipped, 1, __ATOMIC_RELAXED);
        if (research_observing && skipped % 7200 == 1)
            LOG("arceus-script-tick: frames=%llu ticks=%llu ran=%llu skipped=%llu latched=%llu blend_writes=%llu blend_foreign=%llu answers_kept=%llu windows=%llu expired=%llu interrupted=%llu answers_dropped=%llu blend_dropped=%llu\n",
                (unsigned long long)frames, (unsigned long long)ticks,
                (unsigned long long)__atomic_load_n(&script_tick_ran, __ATOMIC_RELAXED), (unsigned long long)skipped,
                (unsigned long long)__atomic_load_n(&script_tick_latched, __ATOMIC_RELAXED),
                (unsigned long long)__atomic_load_n(&rotation_blend_writes, __ATOMIC_RELAXED),
                (unsigned long long)__atomic_load_n(&rotation_blend_foreign, __ATOMIC_RELAXED),
                (unsigned long long)script_query_states[0].filled,
                (unsigned long long)script_windows[0].opens, (unsigned long long)script_windows[0].expired,
                (unsigned long long)script_windows[0].interrupted, (unsigned long long)script_query_dropped,
                (unsigned long long)__atomic_load_n(&rotation_blend_dropped, __ATOMIC_RELAXED));
        c->x[0] = gr32(object + 0x6c);
        c->pc = c->x[30];
        return;
    }
    __atomic_add_fetch(&script_tick_ran, 1, __ATOMIC_RELAXED);
    if (changed) __atomic_add_fetch(&script_tick_latched, changed, __ATOMIC_RELAXED);
    /* Everything the script calls sees a whole 1/30 s in the frame step cell. */
    u32 cell[FRAME_STEP_CELL_WORDS];
    const int outer = usable && !script_tick_depth;
    if (outer) {
        for (unsigned k = 0; k < FRAME_STEP_CELL_WORDS; k++) cell[k] = gr32(frame_step_address + 4 * k);
        frame_step_cell_write(NOMINAL_STEP_SECONDS);
    }
    script_tick_depth++;
    script_class_run(c, entry, class_index, script_tick_originals[class_index][post]);
    script_tick_depth--;
    if (outer)
        for (unsigned k = 0; k < FRAME_STEP_CELL_WORDS; k++) gw32(frame_step_address + 4 * k, cell[k]);
    if (blended) {
        pm_lock(&script_tick_lock);
        rotation_blend_bridge(frame, ns, 2, object);
        pm_unlock(&script_tick_lock);
    }
    /* A field the script or its natives changed keeps its new value. */
    for (unsigned k = 0; changed && k < SCRIPT_TICK_EDGES && writable(fields, 0x24); k++)
        if (seen[k] != current[k] && *GP(fields + script_tick_edges[k]) == seen[k])
            *GP(fields + script_tick_edges[k]) = current[k];
}
#define SCRIPT_TICK_BRIDGES(n) \
    static void script_tick_pre_##n(CPU* c, u64 entry) { script_tick_bridge(c, entry, n, 0); } \
    static void script_tick_post_##n(CPU* c, u64 entry) { script_tick_bridge(c, entry, n, 1); }
SCRIPT_TICK_BRIDGES(0) SCRIPT_TICK_BRIDGES(1) SCRIPT_TICK_BRIDGES(2) SCRIPT_TICK_BRIDGES(3) SCRIPT_TICK_BRIDGES(4)
SCRIPT_TICK_BRIDGES(5) SCRIPT_TICK_BRIDGES(6) SCRIPT_TICK_BRIDGES(7) SCRIPT_TICK_BRIDGES(8) SCRIPT_TICK_BRIDGES(9)
/* Sky clouds: 29BFDAC (vtable 41823B0) adds param/-300 to sky +614 and subtracts
   sin/(param x 30) from +64C and cos/(param x 30) from +650 on every call, so the
   cloud layers moved 4x per second at 120 FPS. With SNX_ARCEUS_CLOUD_FIX=1 these steps
   use min(1/30, elapsed): the divisor -10/step at +614, and s11 = param/step for the other two. */
/* CLOUD_FIX=2, the default, also scales the four wrapped phase increments the original
   adds at 30 Hz. Keep full-step output bit exact and leave a replaced sky block alone. */
static GuestFn cloud_phase_original;
static void cloud_phase_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    const u64 sky = readable(object + 0x58, 8) ? gr64(object + 0x58) : 0;
    const int valid = sky && readable(sky, 0x660);
    float before[4] = {0};
    if (valid)
        for (unsigned k = 0; k < 4; k++) before[k] = guest_float(sky + 0x604 + 4 * k);
    const float seconds = guest_float(frame_step_address);
    cloud_phase_original(c, entry);
    /* Rounded 30 Hz nanoseconds can be one float ULP below 1/30. Keep that native result too. */
    const float full_step = (float)(33333333.0 / 1000000000.0);
    if (!valid || !(seconds > 0.0f && seconds < full_step) ||
        !readable(object + 0x58, 8) || gr64(object + 0x58) != sky || !writable(sky, 0x660)) return;
    const float ratio = seconds / NOMINAL_STEP_SECONDS;
    for (unsigned k = 0; k < 4; k++) {
        const u64 at = sky + 0x604 + 4 * k;
        const float after = guest_float(at);
        if (!isfinite(before[k]) || !isfinite(after)) continue;
        float delta = after - before[k];
        if (delta > 0.5f) delta -= 1.0f;
        if (delta < -0.5f) delta += 1.0f;
        float value = before[k] + delta * ratio;
        if (value >= 1.0f) value -= 1.0f;
        if (value <= -1.0f) value += 1.0f;
        guest_float_write(at, value);
    }
}
/* 2 all eight sky fields (also when unset), 1 the first three, 0 off. */
static int cloud_fix_value(void) {
    const char* value = getenv("SNX_ARCEUS_CLOUD_FIX");
    if (!value || !strcmp(value, "2")) return 2;
    return !strcmp(value, "1");
}
static void cloud_fix_init(void) {
    const int value = cloud_fix_value(), expanded = value == 2;
    if (!main_base || !value) return;
    if (!clocks_installed) {
        WARN("arceus-cloud: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 owner = main_base + 0x29bfdac, one = owner + 0x1b7c, two = owner + 0x2bec;
    /* The +614 step (MOV W8 -300, LDR S0 param, FMOV S1, FDIV), the s11 default and found-path
       product, the pi build at the common join, and both FDIV S0,S0,S11 uses. */
    static const u32 one_words[4] = {0x52b872c8, 0xbd403160, 0x1e270101, 0x1e211800};
    static const u32 two_words[4] = {0x5281fb68, 0x72a80928, 0x1e270101, 0x1e210808};
    int ok = covers_exactly(owner, 0x3060) && readable(owner, 0x3060) && gr32(owner) == 0xd103c3ff &&
             gr32(owner + 0x21f8) == 0x1e27d00b && gr32(owner + 0x2be0) == 0xbd403181 &&
             gr32(owner + 0x2be4) == 0x1e27d002 && gr32(owner + 0x2be8) == 0x1e22082b &&
             gr32(owner + 0x2c04) == 0x1e2b1800 && gr32(owner + 0x2c40) == 0x1e2b1800;
    for (unsigned k = 0; ok && k < 4; k++) ok = gr32(one + 4 * k) == one_words[k] && gr32(two + 4 * k) == two_words[k];
    const u64 extra = owner + 0x1a48;
    if (expanded) {
        for (unsigned k = 0; ok && k < 4; k++) ok = gr32(extra + 4 * k) == one_words[k];
        ok = ok && gr32(main_base + 0x29c23cc) == 0x1e2b0842 &&
             gr32(main_base + 0x29c1fa4) == 0x1e27d00b;
    }
    if (!ok) {
        WARN("arceus-cloud: refused guard\n");
        return;
    }
    if (!frame_step_cell_init("arceus-cloud")) return;
    const u64 page_one = ((frame_step_address >> 12) - (one >> 12)) & 0x1fffff;
    const u64 page_two = ((frame_step_address >> 12) - (two >> 12)) & 0x1fffff;
    /* ADRP X8, cell; LDR S1,[X8,#8] (-10/step); LDR S0 param. The game's FDIV S0,S0,S1 stays.
       X8 is reloaded at +1BAC. */
    gw32(one, 0x90000008 | (u32)((page_one & 3) << 29) | (u32)((page_one >> 2) << 5));
    gw32(one + 4, 0xbd400901);
    gw32(one + 8, 0xbd403160);
    /* ADRP X8, cell; LDP S1,S2,[X8,#16] (pi, 1/(30 x step)); FMUL S11,S11,S2. The FMUL S8 stays. */
    gw32(two, 0x90000008 | (u32)((page_two & 3) << 29) | (u32)((page_two >> 2) << 5));
    gw32(two + 4, 0x2d420901);
    gw32(two + 8, 0x1e22096b);
    if (expanded) {
        const u64 pages = ((frame_step_address >> 12) - (extra >> 12)) & 0x1fffff;
        gw32(extra, 0x90000008 | (u32)((pages & 3) << 29) | (u32)((pages >> 2) << 5));
        gw32(extra + 4, 0xbd400901);
        gw32(extra + 8, 0xbd403160);
    }
    jit_code_changed(owner, 0x3060);
    if (expanded) {
        cloud_phase_original = jit_wait(owner);
        if (!cloud_phase_original) {
            WARN("arceus-cloud: phase wrapper compile failed\n");
            return;
        }
        __atomic_store_n(&g_disp[(owner - g_code_lo) / 4], cloud_phase_update, __ATOMIC_RELEASE);
        LOG("arceus-cloud: EXPERIMENT: extra sky phases +5DC/+604/+608/+60C/+610 use elapsed time\n");
    }
    LOG("arceus-cloud: EXPERIMENT: sky cloud layers +614/+64C/+650 use min(1/30, elapsed), cell=%llx\n",
        (unsigned long long)frame_step_address);
}
static void script_tick_init(void) {
    static const GuestFn wrappers[SCRIPT_TICK_CLASSES][2] = {
        {script_tick_pre_0, script_tick_post_0}, {script_tick_pre_1, script_tick_post_1},
        {script_tick_pre_2, script_tick_post_2}, {script_tick_pre_3, script_tick_post_3},
        {script_tick_pre_4, script_tick_post_4}, {script_tick_pre_5, script_tick_post_5},
        {script_tick_pre_6, script_tick_post_6}, {script_tick_pre_7, script_tick_post_7},
        {script_tick_pre_8, script_tick_post_8}, {script_tick_pre_9, script_tick_post_9}};
    const unsigned long ticked = script_tick_wanted(), windows = script_windows_wanted();
    unsigned long wanted = ticked;
    for (unsigned k = 0; k < SCRIPT_WINDOWS; k++)
        if (windows >> k & 1) wanted |= script_window_classes[k];
    if (!main_base || !wanted) return;
    if (!clocks_installed) {
        WARN("arceus-script-tick: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    if (!frame_step_cell_init("arceus-script-tick")) return;
    for (unsigned k = 0; k < SCRIPT_TICK_CLASSES; k++) {
        const ScriptTickClass* cls = &script_tick_classes[k];
        const u32 offsets[2] = {cls->bridge[0].address, cls->bridge[1].address};
        int ok = wanted >> k & 1;
        for (unsigned b = 0; ok && b < 2; b++) {
            const ScriptTickBridge* g = &cls->bridge[b];
            const u64 pc = main_base + g->address;
            ok = covers_exactly(pc, g->size) && readable(pc, g->size) && gr32(pc) == g->head &&
                 gr32(pc + g->load_at) == g->load && (!g->extra_at || gr32(pc + g->extra_at) == g->extra) &&
                 gr32(pc + g->swap_at) == g->swap_old;
            if (!ok) WARN("arceus-script-tick: refused guard %s at main+%x\n", cls->name, g->address);
        }
        for (unsigned b = 0; ok && b < 2; b++) {
            const ScriptTickBridge* g = &cls->bridge[b];
            const u64 pc = main_base + g->address;
            gw32(pc + g->swap_at, g->swap_new); /* An equal ADD Xd,Xm,#0 for the MOV Xd,Xm. */
            jit_code_changed(pc, g->size);
            script_tick_originals[k][b] = jit_wait(pc);
            if (!script_tick_originals[k][b]) {
                WARN("arceus-script-tick: compile failed %s at main+%x\n", cls->name, g->address);
                ok = 0;
            }
        }
        if (!ok) continue;
        for (unsigned b = 0; b < 2; b++)
            __atomic_store_n(&g_disp[(main_base + offsets[b] - g_code_lo) / 4], wrappers[k][b], __ATOMIC_RELEASE);
        if (ticked >> k & 1) script_tick_mask |= 1ul << k;
        script_tick_wrapped |= 1ul << k;
        const int windowed = (windows & 1) && (script_window_classes[0] >> k & 1);
        LOG("arceus-script-tick: %s state script runs 30 times per second%s\n", cls->name,
            ticked >> k & 1 ? "" : " inside its windows only");
        if (k == ROTATION_BLEND_CLASS && (ticked >> k & 1) && rotation_blend_init())
            LOG("arceus-script-tick: the rotation set by main+25ab490 is blended between runs\n");
        if (k >= SCRIPT_TICK_BUDDY && (ticked >> k & 1 || windowed)) script_query_init();
    }
    for (unsigned k = 0; k < SCRIPT_WINDOWS; k++) {
        if (!(windows >> k & 1)) continue;
        if ((script_tick_wrapped & script_window_classes[k]) != script_window_classes[k] || !script_window_init(k)) {
            WARN("arceus-script-window: window %u is off, its classes then run on every frame\n", k);
            continue;
        }
        script_windows_mask |= 1ul << k;
        LOG("arceus-script-window: window %u ticks classes %lx between its natives\n", k, script_window_classes[k]);
    }
}
static void direct_move_fix_init(void) {
    if (!main_base || !arceus_switch_enabled("SNX_ARCEUS_DIRECT_MOVE_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-direct-move: refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 owner = main_base + 0x1982408, site = main_base + 0x1982e6c, step = main_base + 0x1982930;
    if (!covers_exactly(owner, 0xdd4) || !readable(owner, 0xdd4) || gr32(site) != 0x94000241 ||
        gr32(step) != 0x52911128 || gr32(step + 4) != 0x72a7a108 || gr32(owner + 0x54c) != 0x94000323 ||
        gr32(owner + 0xa08) != 0x394183e8) {
        WARN("arceus-direct-move: refused instruction guard\n");
        return;
    }
    if (!set_position_wrap_init()) {
        WARN("arceus-direct-move: refused, set position 1983770 is not wrapped\n");
        return;
    }
    __atomic_store_n(&direct_move_fix, 1, __ATOMIC_RELEASE);
    LOG("arceus-direct-move: EXPERIMENT: the ground move's direct position step follows elapsed time\n");
}

static GuestFn field_waza_original;
static int field_waza_fix, field_waza_holding;
static float field_waza_fraction;
static u64 field_waza_frame = ~0ull;
static int field_waza_hold(u64 frame, float ratio) {
    if (frame == field_waza_frame) return field_waza_holding;
    field_waza_frame = frame;
    field_waza_fraction += ratio;
    field_waza_holding = field_waza_fraction < 0.9999f;
    if (!field_waza_holding) field_waza_fraction = field_waza_fraction > 1.0f ? field_waza_fraction - 1.0f : 0.0f;
    return field_waza_holding;
}
static void field_waza_update(CPU* c, u64 entry) {
    static const u32 countdowns[2] = {0x218, 0x3b0};
    const u64 object = c->x[0];
    const int known = writable(object, 0x3b8);
    const float ratio = frame_step_ratio();
    const int scale = field_waza_fix && known && ratio < 1.0f;
    const int hold = scale && field_waza_hold(__atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE), ratio);
    int before[2] = {0, 0};
    u32 count = 0, turn = 0, shown = 0;
    if (known) {
        count = gr32(object + 0x370);
        turn = gr32(object + 0x3ac);
    }
    for (unsigned k = 0; known && k < 2; k++) {
        before[k] = (int)gr32(object + countdowns[k]);
        if (hold && before[k] > 0) gw32(object + countdowns[k], (u32)(before[k] + 1));
    }
    if (scale) {
        shown = float_bits(guest_float(object + 0x3ac) * ratio);
        gw32(object + 0x3ac, shown);
    }
    field_waza_original(c, entry);
    if (!known || !writable(object, 0x3b8)) return;
    if (scale && gr32(object + 0x3ac) == shown) gw32(object + 0x3ac, turn);
    if (hold && gr32(object + 0x370) == count + 1) gw32(object + 0x370, count);
    for (unsigned k = 0; k < 2; k++)
        if (hold && before[k] > 0 && gr32(object + countdowns[k]) == (u32)(before[k] + 1))
            gw32(object + countdowns[k], (u32)before[k]);
}
static void field_waza_init(void) {
    if (!main_base || !arceus_switch_enabled("SNX_ARCEUS_FIELD_WAZA_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-field-waza: fix refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x1dc32c0;
    if (!covers_exactly(pc, 0x26c0) || !readable(pc, 0x26c0) || gr32(pc) != 0x6db63bef ||
        gr32(pc + 0x34) != 0xaa0003f3 || gr32(pc + 0x30c) != 0xb943b268 || gr32(pc + 0x310) != 0x71000508 ||
        gr32(pc + 0x3b8) != 0xb9437269 || gr32(pc + 0xf4c) != 0xb9421a69 || gr32(pc + 0xf7c) != 0xbd43ae60 ||
        gr32(pc + 0xf8c) != 0x51000528) {
        WARN("arceus-field-waza: refused guard at main+1dc32c0\n");
        return;
    }
    if (!frame_step_cell_init("arceus-field-waza")) return;
    gw32(pc + 0x34, 0x91000013);
    jit_code_changed(pc, 0x26c0);
    field_waza_original = jit_wait(pc);
    if (!field_waza_original) {
        WARN("arceus-field-waza: compile failed at main+1dc32c0\n");
        return;
    }
    field_waza_fix = 1;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], field_waza_update, __ATOMIC_RELEASE);
    LOG("arceus-field-waza: EXPERIMENT: field move timers and turns use 30 Hz time\n");
}

static GuestFn waza_object_original;
static int waza_object_fix;
static void waza_object_held(CPU* c, u64 entry, u64 object) {
    static const u32 counters[2] = {0x5e4, 0x5e8};
    u32 was[2];
    for (unsigned k = 0; k < 2; k++) {
        was[k] = gr32(object + counters[k]);
        if (was[k]) gw32(object + counters[k], was[k] - 1);
    }
    const u32 terrain = gr32(object + 0x5f0);
    if (terrain && terrain != ~0u) gw32(object + 0x5f0, terrain + 1);
    waza_object_original(c, entry);
    if (!writable(object, 0x5f4)) return;
    if (terrain && terrain != ~0u && gr32(object + 0x5f0) == terrain + 1) gw32(object + 0x5f0, terrain);
    for (unsigned k = 0; k < 2; k++) {
        const u32 now = gr32(object + counters[k]);
        if (was[k] && now == was[k] - 1) gw32(object + counters[k], was[k]);
        else if (!was[k] && now == 1) gw32(object + counters[k], 0);
    }
}
static void waza_object_update(CPU* c, u64 entry) {
    const u64 object = c->x[0];
    pm_lock(&step_lock);
    const unsigned ticks = blink_ticks;
    pm_unlock(&step_lock);
    if (!waza_object_fix || ticks || !writable(object, 0x5f4)) waza_object_original(c, entry);
    else waza_object_held(c, entry, object);
}
static void waza_object_init(void) {
    if (!main_base || !arceus_switch_enabled("SNX_ARCEUS_WAZA_OBJECT_FIX", 1)) return;
    if (!clocks_installed) {
        WARN("arceus-waza-object: fix refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x1d91df4;
    if (!covers_exactly(pc, 0x6478) || !readable(pc, 0x6478) || gr32(pc) != 0x6db63bef ||
        gr32(pc + 0x30) != 0xb945ec08 || gr32(pc + 0x34) != 0xaa0003f3) {
        WARN("arceus-waza-object: refused instruction/function guard\n");
        return;
    }
    gw32(pc + 0x34, 0x91000013);
    jit_code_changed(pc, 0x6478);
    waza_object_original = jit_wait(pc);
    if (!waza_object_original) {
        WARN("arceus-waza-object: compile failed\n");
        return;
    }
    waza_object_fix = 1;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], waza_object_update, __ATOMIC_RELEASE);
    LOG("arceus-waza-object: EXPERIMENT: field attack counters and terrain hit hold use 30 Hz time\n");
}

#define SHOT_OBJECTS 8
typedef struct { u64 object, seen_ns; float from[3], to[3]; int runs; } ShotObject;
static ShotObject shot_objects[SHOT_OBJECTS];
static PMutex shot_lock = PMUTEX_INIT;
static GuestFn waza_shot_original;
static int waza_shot_fix;
static int waza_shot_dependency_warned;
static int waza_shot_mode_value(void) {
    const char* value = getenv("SNX_ARCEUS_WAZA_SHOT_FIX");
    if (!value) return 2;
    return !strcmp(value, "1") ? 1 : !strcmp(value, "2") ? 2 : 0;
}
static ShotObject* shot_slot(u64 object, u64 now) {
    ShotObject* oldest = &shot_objects[0];
    for (unsigned k = 0; k < SHOT_OBJECTS; k++) {
        if (shot_objects[k].object == object && now - shot_objects[k].seen_ns < 500000000ull) return &shot_objects[k];
        if (shot_objects[k].seen_ns < oldest->seen_ns) oldest = &shot_objects[k];
    }
    *oldest = (ShotObject){.object = object};
    return oldest;
}
static void shot_between(const ShotObject* o, float share, float out[3]) {
    float d[3], length = 0.0f;
    for (int k = 0; k < 3; k++) d[k] = o->to[k] - o->from[k], length += d[k] * d[k];
    if (!(share >= 0.0f)) share = 0.0f;
    if (share > 1.0f || o->runs < 2 || !(length < 25.0f)) share = 1.0f;
    for (int k = 0; k < 3; k++) out[k] = o->from[k] + d[k] * share;
}
static void shot_transform_write(u64 transform, const float pos[3]) {
    for (int k = 0; k < 3; k++) guest_float_write(transform + 0xd0 + 4 * k, pos[k]);
    *GP(transform + 0xe0) = 1;
}
static void waza_shot_update(CPU* c, u64 entry) {
    const u64 object = c->x[0], return_pc = c->x[30];
    pm_lock(&step_lock);
    const unsigned ticks = blink_ticks;
    const float share = (float)blink_remainder / (float)ARCEUS_WHOLE_STEP_NS;
    pm_unlock(&step_lock);
    const int hold = waza_shot_fix && !ticks;
    const u64 now = plat_ns();
    u64 transform = waza_shot_fix == 2 ? trajectory_transform(object) : 0;
    if (transform && !writable(transform, 0xe4)) transform = 0;
    if (transform && !hold) {
        pm_lock(&shot_lock);
        ShotObject* o = shot_slot(object, now);
        const int have = o->runs > 0;
        float real[3];
        for (int k = 0; !have && k < 3; k++) o->to[k] = guest_float(transform + 0xd0 + 4 * k);
        if (!have) o->runs = 1, o->seen_ns = now;
        memcpy(real, o->to, sizeof real);
        pm_unlock(&shot_lock);
        if (have) shot_transform_write(transform, real);
    }
    if (hold) c->pc = return_pc;
    else waza_shot_original(c, entry);
    if (transform && readable(transform, 0xe4)) {
        float drawn[3];
        pm_lock(&shot_lock);
        ShotObject* o = shot_slot(object, now);
        const int have = o->runs > 0;
        if (!hold) {
            memcpy(o->from, o->to, sizeof o->from);
            for (int k = 0; k < 3; k++) o->to[k] = guest_float(transform + 0xd0 + 4 * k);
            if (o->runs < 2) o->runs++;
        }
        o->seen_ns = now;
        shot_between(o, share, drawn);
        pm_unlock(&shot_lock);
        if (have) shot_transform_write(transform, drawn);
    }
}
static void waza_shot_init(void) {
    const int mode = waza_shot_mode_value();
    if (!main_base || !mode) return;
    if (!arceus_switch_enabled("SNX_ARCEUS_WAZA_OBJECT_FIX", 1) && !waza_shot_dependency_warned) {
        WARN("arceus-waza-shot: enabled without WAZA_OBJECT_FIX; shots may end at the muzzle\n");
        waza_shot_dependency_warned = 1;
    }
    if (!clocks_installed) {
        WARN("arceus-waza-shot: fix refused without elapsed-time clocks\n");
        return;
    }
    if (!g_mod->nfns || !g_mod->fn_lo || !g_mod->fn_len || !g_mod->stale || !g_mod->entry_fn) return;
    const u64 pc = main_base + 0x1dbf4f4;
    if (!covers_exactly(pc, 0x2c90) || !readable(pc, 0x2c90) || gr32(pc) != 0xd10783ff ||
        gr32(pc + 0x24) != 0xb946c008 || gr32(pc + 0x28) != 0xaa0003f3) {
        WARN("arceus-waza-shot: refused instruction/function guard\n");
        return;
    }
    gw32(pc + 0x28, 0x91000013);
    jit_code_changed(pc, 0x2c90);
    waza_shot_original = jit_wait(pc);
    if (!waza_shot_original) {
        WARN("arceus-waza-shot: compile failed\n");
        return;
    }
    waza_shot_fix = mode;
    __atomic_store_n(&g_disp[(pc - g_code_lo) / 4], waza_shot_update, __ATOMIC_RELEASE);
    LOG("arceus-waza-shot: EXPERIMENT: shot update uses 30 Hz time, drawing %s\n",
        waza_shot_fix == 2 ? "between ticks" : "at the tick rate");
}

void arceus_clock_post_jit(void) {
    if (!timing_enabled || !clocks_installed) return;
    const char* probe = getenv("SNX_ARCEUS_CAMERA_TRACE");
    camera_trace_only = main_base && !target_fps_explicit && probe && !strcmp(probe, "1");
    timer_carry_fix = arceus_switch_enabled("SNX_ARCEUS_TIMER_CARRY_FIX", 1);
    if (timer_carry_fix) LOG("arceus-timer-carry: integer timer catch-up enabled, backlog capped at 1 second\n");
    fixed_resolution_init();
    /* Before the research probe, which compiles and keeps this wrapper's body. */
    physics_timestep_init();
    navigation_timestep_init();
    /* Before the census, which then skips the owner this wraps. */
    world_timers_init();
    gravity_fix_init();
    movement_fix_init();
    ride_ramp_fix_init();
    capped_fall_init();
    ai_tick_fix_init();
    turn_fix_init();
    list_scroll_init();
    ride_swap_init();
    ball_flight_init();
    timing_research_init();
    capture_fix_init();
    battle_sequence_fix_init();
    map_cursor_fix_init();
    cloud_fix_init();
    fade_fix_init();
    climb_step_init();
    wall_jump_fix_init();
    ledge_fix_init();
    /* After the ledge fix, which owns the Sneasler bridges. */
    script_tick_init();
    direct_move_fix_init();
    field_waza_init();
    waza_object_init();
    waza_shot_init();
    cadence_census_init();
    water_fix_init();
    effects_fix_init();
    event_camera_init();
    blink_fix_init();
    pad_repeat_init();
    camera_input_init();
    camera_rate_init();
    field_message_probe_init();
}
