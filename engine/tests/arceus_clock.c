/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Exercise the real wrappers with an asset-free guest sequence and host stubs. */
#include "nx/runtime/arceus_clock.c"
/* Discord presence's host half is not part of this test: its state and a start that starts only when a test says */
SnxDrpState g_drp_state;
int g_drp_packs;
SnxDrpFiles g_drp_files;
uint32_t g_drp_battle;
static int discord_starts;
static unsigned discord_start_calls;
int snx_discord_start(const char* app) {
    (void)app;
    discord_start_calls++;
    return discord_starts;
}
#include "arceus_steps_30.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static u8 memory[4096];
u8* g_mem = memory;
u64 g_as_size = sizeof memory;
MemLayout g_layout = {.base = 1};
const SnxModule* g_mod;
GuestFn* g_disp;
u64 g_code_lo, g_code_hi;
int g_opt_fps;
void pm_lock(PMutex* m) { (void)m; }
/* a test's step run once at the next unlock, as another thread that takes the lock then would */
static void (*unlock_hook)(void);
void pm_unlock(PMutex* m) {
    (void)m;
    void (*hook)(void) = unlock_hook;
    unlock_hook = NULL;
    if (hook) hook();
}
static unsigned waza_dependency_warnings;
static unsigned low_fps_skip_logs, log_lines, drp_layer_notes, drp_bare_lines, drp_bare_warnings;
static char drp_bare_line[512]; /* the last bare start line, formatted */
static unsigned throw_guard_logged, throw_guard_warnings;
static char throw_guard_last[512];
static unsigned log_calls;
/* Only the event probes', the event tick's and the demo probe's lines are kept. */
static char logged[8][512];
static unsigned logged_count;
void logf_(int level, const char* fmt, ...) {
    log_lines++;
    if (strstr(fmt, "arceus-discord: presence off: without the timing layer")) drp_layer_notes++;
    if (strstr(fmt, "arceus-discord: EXPERIMENT: without the timing layer")) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(drp_bare_line, sizeof drp_bare_line, fmt, args);
        va_end(args);
        drp_bare_lines++;
    }
    if (strstr(fmt, "arceus-discord: without the battle sequence count") ||
        strstr(fmt, "arceus-discord: without the camera input count"))
        drp_bare_warnings++;
    if (strstr(fmt, "arceus-throw-guard:")) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(throw_guard_last, sizeof throw_guard_last, fmt, args);
        va_end(args);
        if (level == 0) throw_guard_warnings++;
        else throw_guard_logged++;
    }
    log_calls++;
    if ((!strncmp(fmt, "arceus-event-probe: ", 20) || !strncmp(fmt, "arceus-event-tick: calls", 24) ||
         !strncmp(fmt, "arceus-demo-probe: s", 20) || !strncmp(fmt, "arceus-demo-probe: p", 20) ||
         !strncmp(fmt, "arceus-demo-tick: s", 19) || !strncmp(fmt, "arceus-text-probe: ", 19)) &&
        logged_count < 8) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(logged[logged_count++], sizeof logged[0], fmt, args);
        va_end(args);
    }
    if (level == 0 && strstr(fmt, "arceus-waza-shot: enabled without WAZA_OBJECT_FIX"))
        waza_dependency_warnings++;
    if (level == 1 && strstr(fmt, "arceus-clock: timing layer skipped at fps_limit="))
        low_fps_skip_logs++;
}
static u64 host_ns = 1000000000;
u64 plat_ns(void) { return host_ns; }
void mem_query(u64 address, MemBlock* out) {
    (void)address;
    *out = (MemBlock){.addr = 1, .size = g_as_size - 1, .perm = PERM_RW};
}
u64 mem_find_free(u64 lo, u64 hi, u64 size, u64 align) {
    (void)lo; (void)hi; (void)size; (void)align;
    assert(0); return 0;
}
int mem_map(u64 address, u64 size, u32 state, u32 perm) {
    (void)address; (void)size; (void)state; (void)perm;
    assert(0); return -1;
}
/* The JIT is asked only by the throw guard's, the fixed resolution's and the bare presence path's install tests (jit_allowed: jit_wait
   answers jit_answer for the last change) and by the text probe's (jit_stub_on: each jit_wait answers the next of
   jit_bodies). Every change is noted both ways, and each test sets the counts it reads */
static int jit_allowed, jit_stub_on;
static void jit_compiled(CPU* c, u64 entry) { (void)c, (void)entry; } /* a body for jit_answer */
static GuestFn jit_answer, jit_bodies[2];
static unsigned jit_waits, jit_changes;
static u64 jit_changed_at, jit_changed_size, jit_changed[4];
void jit_code_changed(u64 pc, u64 size) {
    assert(jit_allowed || (jit_stub_on && jit_changes < 4));
    if (jit_changes < 4) jit_changed[jit_changes] = pc;
    jit_changes++;
    jit_changed_at = pc;
    jit_changed_size = size;
}
GuestFn jit_wait(u64 pc) {
    if (jit_allowed) {
        assert(jit_changes && pc == jit_changed_at);
        return jit_answer;
    }
    assert(jit_stub_on && jit_waits < 2);
    return jit_bodies[jit_waits++];
}

static unsigned calls, boundaries, calls_this_render, entered_whole;
/* A command's end check as the game makes it: only in an update entered with the frame on
   hold_at exactly. While hold_left is not 0 it holds the frame, as a waiting track does. */
static float hold_at;
static unsigned hold_left, hold_checks;
static int stop_at, change_speed, divert, no_progress, reuse;
static const u64 object = 128, entry_pc = 16, return_pc = 32;
static void sequence_original(CPU* c, u64 entry) {
    assert(entry == entry_pc && c->x[0] == object && c->x[30] == return_pc);
    const float before = guest_float(object + 0xe8);
    const float after = before + guest_float(object + 0xf0);
    /* Every integer command-end comparison must be visited, never crossed. */
    assert(after <= floorf(before) + 1);
    /* Extra calls may catch up complete boundaries, never a fractional tail. */
    if (calls_this_render++) assert(after == floorf(before) + 1);
    /* The game does its whole-frame work in the update that is entered on the whole frame. */
    if (before == floorf(before)) entered_whole++;
    if (hold_at && before == hold_at) {
        hold_checks++;
        if (hold_left) {
            hold_left--;
            c->x[0] = ++calls;
            c->x[30] = 999;
            c->pc = return_pc;
            return;
        }
    }
    if (after == floorf(before) + 1) boundaries++;
    if (!no_progress) guest_float_write(object + 0xe8, after);
    if (stop_at && after == stop_at) *GP(object + 0x301) = 0;
    if (change_speed) guest_float_write(object + 0xf0, 0);
    /* The object is let go and its memory holds something else that looks like a sequence. */
    if (reuse) gw64(object, gr64(object) + 0x118);
    c->x[0] = ++calls;
    c->x[30] = 999; /* The wrapper must restore the caller on re-entry. */
    c->pc = divert ? 64 : return_pc;
}
static void reset(float frame, float speed, u64 ns) {
    memset(memory, 0, sizeof memory);
    memset(sequence_slots, 0, sizeof sequence_slots);
    sequence_next_slot = 0;
    calls = boundaries = 0;
    stop_at = change_speed = divert = no_progress = reuse = 0;
    guest_float_write(object + 0xe8, frame);
    guest_float_write(object + 0xec, 10000);
    guest_float_write(object + 0xf0, speed);
    *GP(object + 0x301) = *GP(object + 0x302) = *GP(object + 0x303) = 1;
    step.step = ns;
    battle_sequence_original = sequence_original;
}
static void update(void) {
    CPU cpu = {.x = {[0] = 128, [30] = 32}, .pc = 16};
    calls_this_render = 0;
    battle_sequence_update(&cpu, entry_pc);
    assert(cpu.pc == (divert ? 64 : return_pc));
    assert(cpu.x[0] == calls);
}
static void test_sequence(void) {
    static const unsigned rates[] = {60, 120, 144, 240, 500, 1000};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        const unsigned fps = rates[r];
        for (float speed = .25f; speed <= 4; speed *= 2) {
            reset(0, speed, 1000000000 / fps);
            for (unsigned i = 0; i < fps; i++) update();
            assert(fabsf(guest_float(object + 0xe8) + sequence_slots[0].pending - 30 * speed) < .001f);
            assert(sequence_slots[0].pending < 1);
        }
    }
    /* A stall of 100 ms at speed 4 is twelve authored frames. The game would step once, by 4,
       so the call makes four updates and the rest is not caught up beyond one more step. */
    reset(.25f, 4, 100000000);
    update();
    assert(calls == 4 && boundaries == 4);
    assert(guest_float(object + 0xe8) == 4);
    assert(guest_float(object + 0xf0) == 4);
    step.step = 16666667;
    update();
    assert(calls == 8 && guest_float(object + 0xe8) == 8 && sequence_slots[0].pending < 2.01f);
    /* At speed 1 a stall gets one update, and one frame of it is kept. */
    reset(0, 1, 100000000);
    update();
    assert(calls == 1 && guest_float(object + 0xe8) == 1);
    step.step = 16666667;
    update();
    assert(calls == 2 && guest_float(object + 0xe8) == 2 && fabsf(sequence_slots[0].pending - .5f) < .001f);
    reset(0, 4, 16666667);
    stop_at = 1;
    battle_sequence_ended = 0;
    update();
    assert(calls == 1 && guest_float(object + 0xe8) == 1);
    /* The sequence stopped playing in that call: it is counted as ended, once, and its slot
       starts a new count. */
    assert(battle_sequence_ended == 1 && !sequence_slots[0].calls);
    /* The dispatch watch: once a second, and it counts the seconds in which the entry of 9DBEAC
       is not the wrapper. */
    {
        static GuestFn table[8];
        GuestFn* const saved_disp = g_disp;
        const u64 saved_lo = g_code_lo, saved_base = main_base;
        const int saved_research = research_observing;
        g_disp = table, main_base = 0x100000, g_code_lo = main_base + 0x9dbeac - 16, research_observing = 1;
        battle_sequence_watch_ns = battle_sequence_watch_lines = battle_sequence_displaced = 0;
        table[4] = battle_sequence_update;
        battle_sequence_dispatch_watch(5000000000ull);
        battle_sequence_dispatch_watch(5500000000ull);
        assert(battle_sequence_watch_lines == 1 && !battle_sequence_displaced);
        table[4] = sequence_original;
        battle_sequence_dispatch_watch(6000000000ull);
        battle_sequence_dispatch_watch(7000000000ull);
        assert(battle_sequence_watch_lines == 3 && battle_sequence_displaced == 2);
        /* Without the research switch it does nothing. */
        research_observing = 0;
        battle_sequence_dispatch_watch(9000000000ull);
        assert(battle_sequence_watch_lines == 3);
        g_disp = saved_disp, g_code_lo = saved_lo, main_base = saved_base, research_observing = saved_research;
    }
    /* A sequence that runs to its end frame is counted with the calls it took. */
    reset(8, 1, 16666667);
    guest_float_write(object + 0xec, 10);
    for (unsigned i = 0; i < 3; i++) update();
    assert(battle_sequence_ended == 1 && sequence_slots[0].calls == 3);
    update();
    assert(battle_sequence_ended == 2 && guest_float(object + 0xe8) == 10 && !sequence_slots[0].calls);
    reset(0, 4, 16666667);
    change_speed = 1;
    update();
    assert(calls == 1 && guest_float(object + 0xf0) == 0);
    reset(0, 4, 16666667);
    divert = 1;
    update();
    assert(calls == 1);
    reset(0, 4, 16666667);
    no_progress = 1;
    update();
    assert(calls == 1);
    /* Each of these would get a second update (speed 4 at 60 FPS is two frames a call) if the
       first had left the object as it found it. With another first word it gets none, with
       either landing. */
    for (int even = 0; even < 2; even++) {
        reset(0, 4, 16666667);
        battle_sequence_even = even;
        update();
        assert(calls == 2);
        reset(0, 4, 16666667);
        battle_sequence_even = even;
        reuse = 1;
        update();
        assert(calls == 1);
    }
    battle_sequence_even = 0;
    reset(0, 4, 16666667);
    sequence_slots[0].object = object;
    sequence_slots[0].pending = 40;
    /* A budget from before is cut to one step of the game. */
    update();
    assert(calls == 4 && fabsf(sequence_slots[0].pending - 2) < .001f);
    update();
    assert(calls == 8 && fabsf(guest_float(object + 0xe8) - 8) < .0001f);
    /* A skipped update keeps a negative carry: the borrowed half frames are repaid first. */
    reset(.5f, 1, 16666667);
    sequence_slots[0].object = object;
    sequence_slots[0].cut = 1;
    sequence_slots[0].calls = 1;
    sequence_slots[0].pending = -2;
    update();
    assert(calls == 0 && fabsf(sequence_slots[0].pending + 1.5f) < .00001f);
    update();
    assert(calls == 0 && fabsf(sequence_slots[0].pending + 1) < .00001f);
    /* A nearly complete frame must not cause a second, tiny callback this render. */
    reset(.99f, 1, 16666667);
    update();
    assert(calls == 1 && guest_float(object + 0xe8) == 1);
    assert(fabsf(sequence_slots[0].pending - .49f) < .00001f);
    update();
    assert(calls == 2 && guest_float(object + 0xe8) > 1.98f);
    /* Changing frame intervals carry elapsed budget rather than a fixed FPS ratio. */
    const u64 intervals[] = {8333333, 16666667, 11111111, 25000000, 10000000, 100000000,
                             1000000, 4166667, 2777778, 6944444, 1000000, 50000000};
    /* Without the two stalls (100 and 50 ms) nothing is lost. With them the sequence falls
       behind, as the game does, and is never ahead. */
    for (unsigned stalls = 0; stalls < 2; stalls++)
        for (float speed = .25f; speed <= 4; speed *= 2) {
            reset(0, speed, 1);
            u64 elapsed = 0;
            for (unsigned i = 0; i < 120; i++) {
                step.step = intervals[i % (sizeof intervals / sizeof intervals[0])];
                if (!stalls && step.step > 25000000) step.step = 5000000;
                elapsed += step.step;
                update();
                const double expected = speed * (double)elapsed / 33333333.0;
                const double have = guest_float(object + 0xe8) + sequence_slots[0].pending;
                /* What is left is at most one step of the game beyond this frame's own advance. */
                if (stalls)
                    assert(have < expected + .002 && sequence_slots[0].pending <
                           (speed > 1 ? speed : 1) + speed * (float)step.step / 33333333.0f + .001f);
                else assert(fabs(have - expected) < .002 && sequence_slots[0].pending < 1);
            }
        }
}
static void flag(const char* name, const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(name, value));
#else
    assert(!setenv(name, value, 1));
#endif
}
static void unset_flag(const char* name) {
#ifdef _WIN32
    assert(!_putenv_s(name, ""));
#else
    assert(!unsetenv(name));
#endif
}
/* The governor patch at CC35A4 (the user, 2026-11-20: fixed resolution by default): unset or 1 turns its range
   check's SUB into MOV W9,#3, the governor's early return, 0 or any other value leaves it. */
static void test_fixed_resolution(void) {
    static u64 lo[1];
    static const u32 len[1] = {0x2c8};
    static u8 stale[1];
    static GuestFn entry[1];
    const u64 pc = 256;
    SnxModInfo info = {.name = "main", .base = pc - 0xcc35a4};
    memcpy(info.build_id, arceus_main_id, sizeof info.build_id < sizeof arceus_main_id ? sizeof info.build_id
                                                                                        : sizeof arceus_main_id);
    const SnxModule module = {.nmods = 1, .mods = &info, .nfns = 1, .fn_lo = lo, .fn_len = len, .stale = stale,
                              .entry_fn = entry};
    const SnxModule* const saved_mod = g_mod;
    const u64 saved_base = main_base, saved_lo = g_code_lo, saved_hi = g_code_hi;
    const int saved_timing = timing_enabled, saved_clocks = clocks_installed, saved_active = fixed_resolution_active;
    const char* const values[] = {NULL, "1", "0", "2"};
    g_mod = &module, main_base = 0, lo[0] = pc, g_code_lo = 0, g_code_hi = sizeof memory; /* main_base unused */
    jit_answer = jit_compiled;
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
        memset(memory + pc, 0, 0x2c8);
        gw32(pc, 0xd10103ff), gw32(pc + 0x20, 0x51000509), gw32(pc + 0x24, 0x7100093f);
        gw32(pc + 0x28, 0x54000308), gw32(pc + 0x94, 0xd65f03c0);
        if (values[i]) flag("SNX_ARCEUS_FIXED_RESOLUTION", values[i]);
        else unset_flag("SNX_ARCEUS_FIXED_RESOLUTION");
        const int on = i < 2;
        jit_allowed = 1, jit_changes = 0, fixed_resolution_active = 0;
        fixed_resolution_init();
        jit_allowed = 0;
        assert(gr32(pc + 0x20) == (on ? 0x52800069u : 0x51000509u) && jit_changes == (unsigned)on);
        assert(gr32(pc) == 0xd10103ff && gr32(pc + 0x24) == 0x7100093f && gr32(pc + 0x94) == 0xd65f03c0);
        /* the scene scale's guard reads this: set exactly when the patch went in */
        assert(fixed_resolution_active == on);
    }
    /* At the game's own 30 FPS the timing layer is off and main_base stays 0, and with SNX_ARCEUS_TIMING_FIXES=0
       post_jit returns before every timing fix: it still patches the governor first, and =0 still keeps it */
    for (int master_off = 0; master_off < 2; master_off++)
        for (int keep = 0; keep < 2; keep++) {
            memset(memory + pc, 0, 0x2c8);
            gw32(pc, 0xd10103ff), gw32(pc + 0x20, 0x51000509), gw32(pc + 0x24, 0x7100093f);
            gw32(pc + 0x28, 0x54000308), gw32(pc + 0x94, 0xd65f03c0);
            if (keep) flag("SNX_ARCEUS_FIXED_RESOLUTION", "0");
            else unset_flag("SNX_ARCEUS_FIXED_RESOLUTION");
            if (master_off) flag("SNX_ARCEUS_TIMING_FIXES", "0");
            else unset_flag("SNX_ARCEUS_TIMING_FIXES");
            timing_enabled = clocks_installed = 0, main_base = 0;
            jit_allowed = 1, jit_changes = 0, fixed_resolution_active = 0;
            arceus_clock_post_jit();
            jit_allowed = 0;
            assert(gr32(pc + 0x20) == (keep ? 0x51000509u : 0x52800069u) && jit_changes == (unsigned)!keep);
            assert(fixed_resolution_active == !keep);
        }
    unset_flag("SNX_ARCEUS_TIMING_FIXES");
    jit_answer = NULL;
    /* Another main build is left alone */
    gw32(pc + 0x20, 0x51000509);
    unset_flag("SNX_ARCEUS_FIXED_RESOLUTION");
    info.build_id[0] ^= 1;
    fixed_resolution_init();
    assert(gr32(pc + 0x20) == 0x51000509u);
    info.build_id[0] ^= 1;
    /* A changed function is refused with the switch on */
    gw32(pc + 0x20, 0x51000509), gw32(pc + 0x24, 0);
    fixed_resolution_init();
    assert(gr32(pc + 0x20) == 0x51000509u);
    memset(memory + pc, 0, 0x2c8);
    g_mod = saved_mod, main_base = saved_base, g_code_lo = saved_lo, g_code_hi = saved_hi;
    timing_enabled = saved_timing, clocks_installed = saved_clocks, fixed_resolution_active = saved_active;
}

/* Discord presence is on by default, needs an application id, and says why it is off only when asked for */
static void test_discord_default(void) {
    static GuestFn table[1];
    const u64 saved_base = main_base;
    GuestFn* const saved_disp = g_disp;
    const struct {
        const char *presence, *app;
        int on;
    } cases[] = {{NULL, NULL, 0}, {NULL, "", 0}, {NULL, "1234567890", 1}, {"1", NULL, 1}, {"0", "1234567890", 0},
                 {"2", "1234567890", 0}};
    main_base = 0, g_disp = NULL;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (cases[i].presence) flag("SNX_ARCEUS_DISCORD", cases[i].presence);
        else unset_flag("SNX_ARCEUS_DISCORD");
        if (cases[i].app) flag("SNX_ARCEUS_DISCORD_APP", cases[i].app);
        else unset_flag("SNX_ARCEUS_DISCORD_APP");
        discord_presence_init(main_base);
        assert(drp_presence == cases[i].on);
    }
    /* past the module check, a start that fails (the test's never starts) turns it off and says so */
    main_base = 4096, g_disp = table;
    flag("SNX_ARCEUS_DISCORD", "1");
    unset_flag("SNX_ARCEUS_DISCORD_APP");
    const unsigned lines = log_lines;
    discord_presence_init(main_base);
    assert(!drp_presence && log_lines == lines + 1);
    unset_flag("SNX_ARCEUS_DISCORD"); /* unset with no id: off, nothing said */
    discord_presence_init(main_base);
    assert(!drp_presence && log_lines == lines + 1);
    /* a start that starts: the readers and the tick at the layer's base, which is drp_base too */
    const u64 saved_drp = drp_base;
    flag("SNX_ARCEUS_DISCORD", "1");
    discord_starts = 1, drp_base = 0;
    discord_presence_init(main_base);
    assert(drp_presence && drp_base == main_base && drp_ticking);
    unset_flag("SNX_ARCEUS_DISCORD");
    discord_starts = 0, drp_presence = 0, drp_ticking = 0, drp_base = saved_drp;
    main_base = saved_base, g_disp = saved_disp;
}
static void test_probe_master_switch(void) {
    static const char* const probes[] = {
        "SNX_ARCEUS_EVENT_PROBE", "SNX_ARCEUS_DEMO_PROBE", "SNX_ARCEUS_TEXT_PROBE"
    };
    static const char* const disabled[] = {
        "0", "invalid",
#ifndef _WIN32
        "", /* Windows _putenv_s removes empty values, restoring the default-on state. */
#endif
    };
    static const int runtime_limits[] = {SNX_FPS_GAME, 30, 60, 120, 0};
    const int saved_fps = g_opt_fps;
    const SnxModule* saved_mod = g_mod;
    const u64 saved_base = main_base;
    const int saved_timing = timing_enabled, saved_clocks = clocks_installed;
    u8 before[sizeof memory];
    memcpy(before, memory, sizeof before);
    main_base = 0;
    g_mod = NULL;
    for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) flag(probes[i], "1");
    for (unsigned r = 0; r < sizeof runtime_limits / sizeof runtime_limits[0]; r++) {
        g_opt_fps = runtime_limits[r];
        for (unsigned d = 0; d < sizeof disabled / sizeof disabled[0]; d++) {
            flag("SNX_ARCEUS_TIMING_FIXES", disabled[d]);
            for (int installed = 0; installed <= 1; installed++) {
                timing_enabled = clocks_installed = installed;
                log_calls = 0;
                arceus_clock_post_jit();
                assert(!log_calls && g_opt_fps == runtime_limits[r] && !main_base);
                assert(timing_enabled == installed && clocks_installed == installed);
                assert(!event_probe && !demo_probe && !text_probe);
                assert(!event_probe_original[0] && !event_probe_original[1] && !demo_probe_original);
                assert(!text_probe_original[0] && !text_probe_original[1] && !text_probe_setup_original);
                assert(!memcmp(before, memory, sizeof before));
            }
        }
        /* Each enabled initializer refuses this missing module and logs once. Both master-on forms
           must still reach all three probes when timing hooks are absent, including at game pace. */
        timing_enabled = clocks_installed = 0;
        for (unsigned explicit_on = 0; explicit_on < 2; explicit_on++) {
            if (explicit_on) flag("SNX_ARCEUS_TIMING_FIXES", "1");
            else unset_flag("SNX_ARCEUS_TIMING_FIXES");
            for (unsigned p = 0; p < sizeof probes / sizeof probes[0]; p++) {
                for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++)
                    flag(probes[i], i == p ? "1" : "0");
                log_calls = 0;
                arceus_clock_post_jit();
                assert(log_calls == 1 && !main_base && g_opt_fps == runtime_limits[r]);
            }
        }
        for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) flag(probes[i], "1");
    }
    for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) unset_flag(probes[i]);
    unset_flag("SNX_ARCEUS_TIMING_FIXES");
    g_opt_fps = saved_fps;
    g_mod = saved_mod;
    main_base = saved_base;
    timing_enabled = saved_timing;
    clocks_installed = saved_clocks;
    logged_count = log_calls = 0;
}
static void test_tick_switch_defaults(void) {
    unset_flag("SNX_ARCEUS_EVENT_TICK");
    assert(event_tick_enabled() == 1);
    flag("SNX_ARCEUS_EVENT_TICK", "0");
    assert(event_tick_enabled() == 0);
    flag("SNX_ARCEUS_EVENT_TICK", "1");
    assert(event_tick_enabled() == 1);
    unset_flag("SNX_ARCEUS_DEMO_TICK");
    assert(demo_tick_enabled() == 1);
    flag("SNX_ARCEUS_DEMO_TICK", "0");
    assert(demo_tick_enabled() == 0);
    flag("SNX_ARCEUS_DEMO_TICK", "1");
    assert(demo_tick_enabled() == 1);
    unset_flag("SNX_ARCEUS_TEXT_TICK");
    assert(text_tick_enabled() == 1);
    flag("SNX_ARCEUS_TEXT_TICK", "0");
    assert(text_tick_enabled() == 0);
    flag("SNX_ARCEUS_TEXT_TICK", "1");
    assert(text_tick_enabled() == 1);
    unset_flag("SNX_ARCEUS_EVENT_TICK");
    unset_flag("SNX_ARCEUS_DEMO_TICK");
    unset_flag("SNX_ARCEUS_TEXT_TICK");
}

/* Without the timing layer and with SNX_ARCEUS_DISCORD_BARE=0 presence is off, and says so only when it was wanted in
   Arceus 1.1.1 (research 20:46, option c, the note rewritten for =0). With the layer on, no such line */
static void test_discord_layer_off(void) {
    SnxModInfo main_mod = {.name = "main", .base = 4096};
    memcpy(main_mod.build_id, arceus_main_id, sizeof main_mod.build_id);
    SnxModule module = {.nmods = 1, .mods = &main_mod};
    const SnxModule* const saved_mod = g_mod;
    const u64 saved_base = main_base;
    g_mod = &module, main_base = 0;
    flag("SNX_ARCEUS_DISCORD_BARE", "0");
    const struct {
        const char *presence, *app;
        unsigned notes;
    } cases[] = {{"1", NULL, 1}, {NULL, "1234567890", 1}, {"1", "1234567890", 1}, {"0", "1234567890", 0},
                 {NULL, NULL, 0}, {NULL, "", 0}};
    for (int on = 0; on < 2; on++)
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            if (cases[i].presence) flag("SNX_ARCEUS_DISCORD", cases[i].presence);
            else unset_flag("SNX_ARCEUS_DISCORD");
            if (cases[i].app) flag("SNX_ARCEUS_DISCORD_APP", cases[i].app);
            else unset_flag("SNX_ARCEUS_DISCORD_APP");
            timing_enabled = clocks_installed = on;
            drp_layer_notes = 0;
            drp_ticking = 0;
            arceus_clock_post_jit();
            assert(drp_layer_notes == (on ? 0 : cases[i].notes));
            assert(!__atomic_load_n(&drp_ticking, __ATOMIC_RELAXED)); /* nothing installed either way */
        }
    flag("SNX_ARCEUS_DISCORD", "1"); /* the layer only half up (its clock entries refused): off too, the line */
    for (int half = 0; half < 2; half++) {
        timing_enabled = half, clocks_installed = !half;
        drp_layer_notes = 0;
        arceus_clock_post_jit();
        assert(drp_layer_notes == 1);
    }
    timing_enabled = clocks_installed = 0;
    flag("SNX_ARCEUS_DISCORD", "1"); /* another main build: no Arceus line */
    main_mod.build_id[0] ^= 1;
    drp_layer_notes = 0;
    arceus_clock_post_jit();
    assert(drp_layer_notes == 0);
    unset_flag("SNX_ARCEUS_DISCORD");
    unset_flag("SNX_ARCEUS_DISCORD_APP");
    unset_flag("SNX_ARCEUS_DISCORD_BARE");
    g_mod = saved_mod, main_base = saved_base;
}
/* DISCORD-30 (research 23:29, the reviewer's D1 to D6): without the timing layer presence runs with
   SNX_ARCEUS_DISCORD_BARE unset or 1. The clock read entry first, then the start, the counters and the tick. */
static unsigned bare_clock_calls;
static void bare_clock_original(CPU* c, u64 entry) { (void)c, (void)entry, bare_clock_calls++; }
static void bare_returns(CPU* c, u64 entry) { (void)entry, c->pc = c->x[30]; }
static void bare_diverts(CPU* c, u64 entry) { (void)entry, c->pc = c->x[30] + 4; }
static void test_discord_bare(void) {
    static GuestFn table[sizeof memory / 4];
    static u64 lo[1];
    static u32 len[1];
    static u8 stale[1];
    static GuestFn entry[1];
    const u64 clock = 0x100, base = clock - 0x278303c;
    SnxModInfo main_mod = {.name = "main", .base = base};
    memcpy(main_mod.build_id, arceus_main_id, sizeof main_mod.build_id);
    SnxModule module = {.nmods = 1, .mods = &main_mod, .nfns = 1, .fn_lo = lo, .fn_len = len, .stale = stale,
                        .entry_fn = entry};
    const SnxModule* const saved_mod = g_mod;
    GuestFn* const saved_disp = g_disp;
    const u64 saved_base = main_base, saved_lo = g_code_lo, saved_hi = g_code_hi, saved_drp = drp_base;
    const u64 saved_camera = camera_input_calls, saved_sequence = battle_sequence_calls;
    g_mod = &module, g_disp = table, main_base = 0, g_code_lo = 0, g_code_hi = sizeof memory;
    timing_enabled = clocks_installed = 0;
    flag("SNX_ARCEUS_DISCORD", "1");
    flag("SNX_ARCEUS_DISCORD_APP", "1234567890");
    /* on by default: unset or "1" turns it on. 0 or "on" give the note and touch nothing */
    const char* const values[] = {NULL, "0", "on", "1"};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
        const int on = i == 0 || i == 3;
        memset(memory, 0, sizeof memory), memset(table, 0, sizeof table);
        gw32(clock, 0x8b21d008), table[clock / 4] = bare_clock_original;
        if (values[i]) flag("SNX_ARCEUS_DISCORD_BARE", values[i]);
        else unset_flag("SNX_ARCEUS_DISCORD_BARE");
        discord_starts = 1, discord_start_calls = 0, drp_ticking = 0, drp_presence = 0, drp_base = 0;
        drp_layer_notes = drp_bare_lines = drp_bare_warnings = 0, drp_bare_line[0] = 0;
        arceus_clock_post_jit();
        assert(drp_layer_notes == (unsigned)!on && drp_bare_lines == (unsigned)on);
        /* neither site is in this memory: both counters refused, said in the line and by a warning each */
        const char* refused = strstr(drp_bare_line, "main+193c04c, refused)");
        assert(on ? refused && strstr(refused, "main+9dbeac, refused)") && drp_bare_warnings == 2 : !drp_bare_warnings);
        assert(discord_start_calls == (unsigned)on && drp_ticking == on && drp_presence == on);
        assert(table[clock / 4] == (on ? drp_bare_read : bare_clock_original));
        assert(main_base == 0 && drp_base == (on ? base : 0));
    }
    /* the play line (the reviewer's D4): with SNX_ARCEUS_TIMING_FIXES=0 post_jit returns early, and the bare path or
       the note still comes ahead of that return */
    flag("SNX_ARCEUS_TIMING_FIXES", "0");
    for (int bare = 0; bare < 2; bare++) {
        memset(memory, 0, sizeof memory), memset(table, 0, sizeof table);
        gw32(clock, 0x8b21d008), table[clock / 4] = bare_clock_original;
        if (bare) unset_flag("SNX_ARCEUS_DISCORD_BARE"); /* the default */
        else flag("SNX_ARCEUS_DISCORD_BARE", "0");
        discord_start_calls = 0, drp_ticking = 0, drp_presence = 0, drp_layer_notes = drp_bare_lines = 0;
        arceus_clock_post_jit();
        assert(drp_bare_lines == (unsigned)bare && drp_layer_notes == (unsigned)!bare && drp_ticking == bare);
        assert(table[clock / 4] == (bare ? drp_bare_read : bare_clock_original));
    }
    unset_flag("SNX_ARCEUS_TIMING_FIXES");
    /* the tick on the clock read, then the game's own read */
    bare_clock_calls = 0, drp_next_tick = 0;
    CPU cpu = {0};
    table[clock / 4](&cpu, clock);
    assert(bare_clock_calls == 1 && drp_next_tick == host_ns + 1000000000ull);
    /* a changed clock entry: nothing starts and nothing is installed */
    gw32(clock, 0x8b21d009), table[clock / 4] = bare_clock_original;
    discord_start_calls = 0, drp_ticking = 0, drp_presence = 0, drp_bare_lines = 0;
    arceus_clock_post_jit();
    assert(!discord_start_calls && !drp_ticking && !drp_presence && !drp_bare_lines);
    assert(table[clock / 4] == bare_clock_original);
    /* the right word but an empty dispatch entry: the same, never a call through it */
    gw32(clock, 0x8b21d008), table[clock / 4] = NULL;
    arceus_clock_post_jit();
    assert(!discord_start_calls && !drp_ticking && !drp_presence && !drp_bare_lines && !table[clock / 4]);
    table[clock / 4] = bare_clock_original;
    /* no id (the start fails): off, the counters and the tick left alone */
    gw32(clock, 0x8b21d008), discord_starts = 0, discord_start_calls = 0, jit_changes = 0;
    arceus_clock_post_jit();
    assert(discord_start_calls == 1 && !drp_ticking && !drp_presence && !drp_bare_lines && !jit_changes);
    assert(table[clock / 4] == bare_clock_original);
    /* another main build: no line of either kind */
    main_mod.build_id[0] ^= 1, discord_starts = 1, discord_start_calls = 0, drp_layer_notes = 0;
    arceus_clock_post_jit();
    assert(!discord_start_calls && !drp_layer_notes && !drp_bare_lines);
    main_mod.build_id[0] ^= 1;
    /* D3: the two sites' words as the fixes have used them, read by both inits from one table */
    assert(body_sites[BODY_CAMERA_INPUT].offset == 0x193c04c && body_sites[BODY_CAMERA_INPUT].size == 0x278);
    assert(body_sites[BODY_CAMERA_INPUT].guards == 3 && body_sites[BODY_CAMERA_INPUT].guard[0] == 0xfc1b0fec);
    assert(body_sites[BODY_CAMERA_INPUT].guard_at[1] == 0x130 && body_sites[BODY_CAMERA_INPUT].guard[1] == 0x1e222862);
    assert(body_sites[BODY_CAMERA_INPUT].guard_at[2] == 0x274 && body_sites[BODY_CAMERA_INPUT].guard[2] == 0x17ffffee);
    assert(body_sites[BODY_CAMERA_INPUT].at == 0x274 && body_sites[BODY_CAMERA_INPUT].word == 0xb4fffddf);
    assert(body_sites[BODY_BATTLE_SEQUENCE].offset == 0x9dbeac && body_sites[BODY_BATTLE_SEQUENCE].size == 0x598);
    assert(body_sites[BODY_BATTLE_SEQUENCE].guards == 4 && body_sites[BODY_BATTLE_SEQUENCE].guard[0] == 0x6db923e9);
    assert(body_sites[BODY_BATTLE_SEQUENCE].guard_at[1] == 0x2c &&
           body_sites[BODY_BATTLE_SEQUENCE].guard[1] == 0xaa0003f3);
    assert(body_sites[BODY_BATTLE_SEQUENCE].guard_at[2] == 0x534 &&
           body_sites[BODY_BATTLE_SEQUENCE].guard[2] == 0xbd40f262);
    assert(body_sites[BODY_BATTLE_SEQUENCE].guard_at[3] == 0x538 &&
           body_sites[BODY_BATTLE_SEQUENCE].guard[3] == 0x1e222821);
    assert(body_sites[BODY_BATTLE_SEQUENCE].at == 0x2c && body_sites[BODY_BATTLE_SEQUENCE].word == 0x91000013);
    /* each counter on its fix's own body, with its guards: installed, and refused when a guard word changed */
    for (unsigned k = 0; k < BODY_SITES; k++) {
        const ArceusBodySite* site = &body_sites[k];
        const u64 pc = 0x100, at = pc - site->offset;
        for (int changed = 0; changed < 2; changed++) {
            memset(memory, 0, sizeof memory), memset(table, 0, sizeof table);
            for (unsigned g = 0; g < site->guards; g++) gw32(pc + site->guard_at[g], site->guard[g]);
            if (changed) gw32(pc + site->guard_at[site->guards - 1], 0);
            lo[0] = pc, len[0] = site->size, drp_bare_body[k] = NULL;
            jit_allowed = 1, jit_changes = 0, jit_answer = jit_compiled;
            const int installed = drp_bare_counter(k, at);
            jit_allowed = 0, jit_answer = NULL;
            assert(installed == !changed && jit_changes == (unsigned)!changed);
            assert(drp_bare_body[k] == (changed ? NULL : jit_compiled));
            if (!changed) assert(gr32(pc + site->at) == site->word);
            else assert(gr32(pc + site->at) != site->word);
            assert(table[pc / 4] == (changed ? NULL : (k == BODY_CAMERA_INPUT ? drp_bare_camera : drp_bare_sequence)));
        }
        len[0] = site->size + 4; /* the function the module lists is another one */
        for (unsigned g = 0; g < site->guards; g++) gw32(pc + site->guard_at[g], site->guard[g]);
        assert(!drp_bare_counter(k, at) && gr32(pc + site->at) != site->word);
        /* no JIT function lists (the reviewer's M13): refused, no word written and no body asked for */
        len[0] = site->size, module.fn_lo = NULL, jit_changes = 0;
        assert(!drp_bare_counter(k, at) && gr32(pc + site->at) != site->word && !jit_changes);
        module.fn_lo = lo;
    }
    /* the camera counts as its fix does: a readable object and a call that returned. The sequence counts each call */
    camera_input_calls = battle_sequence_calls = 0;
    CPU call = {.x = {[0] = 0x200, [30] = 0x40}};
    drp_bare_body[BODY_CAMERA_INPUT] = bare_returns;
    drp_bare_camera(&call, 0);
    assert(camera_input_calls == 1);
    call.x[0] = sizeof memory, call.x[30] = 0x40;
    drp_bare_camera(&call, 0);
    assert(camera_input_calls == 1);
    call.x[0] = 0x200, call.x[30] = 0x40, drp_bare_body[BODY_CAMERA_INPUT] = bare_diverts;
    drp_bare_camera(&call, 0);
    assert(camera_input_calls == 1);
    drp_bare_body[BODY_BATTLE_SEQUENCE] = bare_diverts;
    drp_bare_sequence(&call, 0);
    drp_bare_sequence(&call, 0);
    assert(battle_sequence_calls == 2);
    /* with the layer up the bare path never runs, the switch set or not */
    flag("SNX_ARCEUS_DISCORD_BARE", "1");
    timing_enabled = clocks_installed = 1, drp_bare_lines = drp_layer_notes = 0;
    arceus_clock_post_jit();
    assert(!drp_bare_lines && !drp_layer_notes);
    timing_enabled = clocks_installed = 0;
    memset(memory, 0, sizeof memory);
    drp_bare_body[BODY_CAMERA_INPUT] = drp_bare_body[BODY_BATTLE_SEQUENCE] = NULL, drp_bare_clock = NULL;
    unset_flag("SNX_ARCEUS_DISCORD_BARE");
    unset_flag("SNX_ARCEUS_DISCORD");
    unset_flag("SNX_ARCEUS_DISCORD_APP");
    discord_starts = 0, drp_ticking = 0, drp_presence = 0, drp_next_tick = 0;
    camera_input_calls = saved_camera, battle_sequence_calls = saved_sequence;
    g_mod = saved_mod, g_disp = saved_disp, main_base = saved_base, g_code_lo = saved_lo, g_code_hi = saved_hi;
    drp_base = saved_drp;
}
/* The reviewer's N1 and N3 (23:46): the two fixes and the bare counters check a site by one helper. Its outcomes,
   then each fix's init installing on the site and refusing it with a guard word changed */
static void fill_site(const ArceusBodySite* site, u64 pc, int changed) {
    memset(memory, 0, sizeof memory);
    for (unsigned g = 0; g < site->guards; g++) gw32(pc + site->guard_at[g], site->guard[g] ^ (changed == (int)g + 1));
}
static void test_body_sites(void) {
    static GuestFn table[sizeof memory / 4];
    static u64 lo[1];
    static u32 len[1];
    static u8 stale[1];
    static GuestFn entry[1];
    SnxModule module = {.nfns = 1, .fn_lo = lo, .fn_len = len, .stale = stale, .entry_fn = entry};
    const SnxModule* const saved_mod = g_mod;
    GuestFn* const saved_disp = g_disp;
    const u64 saved_base = main_base, saved_lo = g_code_lo, saved_hi = g_code_hi;
    const int saved_clocks = clocks_installed, saved_even = battle_sequence_even;
    const u64 pc = 0x100;
    g_mod = &module, g_disp = table, g_code_lo = 0, g_code_hi = sizeof memory;
    for (unsigned k = 0; k < BODY_SITES; k++) {
        const ArceusBodySite* site = &body_sites[k];
        const u64 base = pc - site->offset;
        lo[0] = pc, len[0] = site->size;
        fill_site(site, pc, 0);
        assert(body_site_check(site, base) == 1);
        for (unsigned g = 0; g < site->guards; g++) {
            fill_site(site, pc, (int)g + 1);
            assert(body_site_check(site, base) == 0);
        }
        fill_site(site, pc, 0);
        len[0] = site->size + 4; /* another function at the address */
        assert(body_site_check(site, base) == 0);
        len[0] = site->size, lo[0] = pc + 4;
        assert(body_site_check(site, base) == 0);
        lo[0] = pc, g_code_hi = pc + site->size - 4; /* past the code */
        assert(body_site_check(site, base) == 0);
        g_code_hi = sizeof memory, module.nfns = 0;
        assert(body_site_check(site, base) == 0);
        module.nfns = 1;
        for (unsigned m = 0; m < 4; m++) { /* no JIT function lists */
            SnxModule bare = module;
            if (m == 0) bare.fn_lo = NULL;
            if (m == 1) bare.fn_len = NULL;
            if (m == 2) bare.stale = NULL;
            if (m == 3) bare.entry_fn = NULL;
            g_mod = &bare;
            assert(body_site_check(site, base) == -1);
            g_mod = &module;
        }
        assert(body_site_check(site, base) == 1);
    }
    /* the fixes' inits on the helper */
    clocks_installed = 1;
    for (unsigned k = 0; k < BODY_SITES; k++)
        for (int changed = 0; changed < 2; changed++) {
            const ArceusBodySite* site = &body_sites[k];
            fill_site(site, pc, changed ? (int)site->guards : 0);
            memset(table, 0, sizeof table);
            lo[0] = pc, len[0] = site->size, main_base = pc - site->offset;
            camera_input_original = battle_sequence_original = NULL;
            jit_allowed = 1, jit_changes = 0, jit_answer = jit_compiled;
            if (k == BODY_CAMERA_INPUT) camera_input_init();
            else battle_sequence_fix_init();
            jit_allowed = 0, jit_answer = NULL;
            const GuestFn wrapper = k == BODY_CAMERA_INPUT ? camera_input_update : battle_sequence_update;
            assert(table[pc / 4] == (changed ? NULL : wrapper) && jit_changes == (unsigned)!changed);
            assert((gr32(pc + site->at) == site->word) == !changed);
            assert((k == BODY_CAMERA_INPUT ? camera_input_original : battle_sequence_original) ==
                   (changed ? NULL : jit_compiled));
        }
    camera_input_original = battle_sequence_original = NULL;
    memset(memory, 0, sizeof memory);
    g_mod = saved_mod, g_disp = saved_disp, main_base = saved_base, g_code_lo = saved_lo, g_code_hi = saved_hi;
    clocks_installed = saved_clocks, battle_sequence_even = saved_even;
}
/* The bare path with every site at its real offset, in a guest memory that large: the three readers, both counters
   and the tick installed, and the start line with nothing refused (the reviewer's N4) */
static void test_discord_bare_sites(void) {
    static const struct { u32 offset, word; } readers[DRPS_SITES] = {
        {0x1eb2a7c, 0xf9403408}, {0x1dd7c68, 0xd103c3ff}, {0xfc20c8, 0xf81d0ff5}};
    static const GuestFn wrappers[DRPS_SITES] = {drp_area_name, drp_battle_start, drp_text_set};
    static u64 lo[BODY_SITES];
    static u32 len[BODY_SITES];
    static u8 stale[BODY_SITES];
    static GuestFn entry[BODY_SITES];
    const u64 base = 0x1000, clock = base + 0x278303c, code_lo = base + 0x9dbeac, size = clock + 0x100;
    u8* const big = calloc(size, 1);
    GuestFn* const table = calloc((clock + 4 - code_lo) / 4, sizeof *table);
    assert(big && table);
    SnxModInfo main_mod = {.name = "main", .base = base};
    memcpy(main_mod.build_id, arceus_main_id, sizeof main_mod.build_id);
    SnxModule module = {.nmods = 1, .mods = &main_mod, .nfns = BODY_SITES, .fn_lo = lo, .fn_len = len,
                        .stale = stale, .entry_fn = entry};
    u8* const saved_mem = g_mem;
    const SnxModule* const saved_mod = g_mod;
    GuestFn* const saved_disp = g_disp;
    GuestFn saved_readers[DRPS_SITES];
    memcpy(saved_readers, drp_original, sizeof saved_readers);
    const u64 saved_size = g_as_size, saved_base = main_base, saved_lo = g_code_lo, saved_hi = g_code_hi;
    const u64 saved_drp = drp_base;
    g_mem = big, g_as_size = size, g_mod = &module, g_disp = table, g_code_lo = code_lo, g_code_hi = clock + 4;
    main_base = 0, timing_enabled = clocks_installed = 0;
    gw32(clock, 0x8b21d008), table[(clock - code_lo) / 4] = bare_clock_original;
    for (unsigned k = 0; k < DRPS_SITES; k++)
        gw32(base + readers[k].offset, readers[k].word), table[(base + readers[k].offset - code_lo) / 4] = jit_compiled;
    for (unsigned k = 0; k < BODY_SITES; k++) {
        const ArceusBodySite* site = &body_sites[k];
        const u64 pc = base + site->offset;
        for (unsigned g = 0; g < site->guards; g++) gw32(pc + site->guard_at[g], site->guard[g]);
        lo[k] = pc, len[k] = site->size, table[(pc - code_lo) / 4] = jit_compiled;
    }
    flag("SNX_ARCEUS_DISCORD", "1");
    flag("SNX_ARCEUS_DISCORD_APP", "1234567890");
    flag("SNX_ARCEUS_DISCORD_BARE", "1");
    discord_starts = 1, drp_ticking = 0, drp_presence = 0;
    drp_bare_lines = drp_bare_warnings = drp_layer_notes = 0, drp_bare_line[0] = 0;
    jit_allowed = 1, jit_changes = 0, jit_answer = jit_compiled;
    arceus_clock_post_jit();
    jit_allowed = 0, jit_answer = NULL;
    assert(drp_bare_lines == 1 && !strstr(drp_bare_line, "refused") && !drp_bare_warnings && !drp_layer_notes);
    assert(strstr(drp_bare_line, "(main+193c04c) and the battle sequence update (main+9dbeac)"));
    assert(jit_changes == 2 && drp_ticking && drp_presence && drp_base == base && main_base == 0);
    assert(table[(clock - code_lo) / 4] == drp_bare_read && drp_bare_clock == bare_clock_original);
    for (unsigned k = 0; k < DRPS_SITES; k++)
        assert(table[(base + readers[k].offset - code_lo) / 4] == wrappers[k] && drp_original[k] == jit_compiled);
    assert(table[(base + 0x193c04c - code_lo) / 4] == drp_bare_camera && gr32(base + 0x193c04c + 0x274) == 0xb4fffddf);
    assert(table[(base + 0x9dbeac - code_lo) / 4] == drp_bare_sequence && gr32(base + 0x9dbeac + 0x2c) == 0x91000013);
    assert(drp_bare_body[BODY_CAMERA_INPUT] == jit_compiled && drp_bare_body[BODY_BATTLE_SEQUENCE] == jit_compiled);
    unset_flag("SNX_ARCEUS_DISCORD_BARE");
    unset_flag("SNX_ARCEUS_DISCORD");
    unset_flag("SNX_ARCEUS_DISCORD_APP");
    discord_starts = 0, drp_ticking = 0, drp_presence = 0;
    drp_bare_body[BODY_CAMERA_INPUT] = drp_bare_body[BODY_BATTLE_SEQUENCE] = NULL, drp_bare_clock = NULL;
    memcpy(drp_original, saved_readers, sizeof saved_readers);
    g_mem = saved_mem, g_as_size = saved_size, g_mod = saved_mod, g_disp = saved_disp;
    main_base = saved_base, g_code_lo = saved_lo, g_code_hi = saved_hi, drp_base = saved_drp;
    free(table), free(big);
}
static void test_defaults_on(void) {
    static const char* const all[] = {
        "SNX_ARCEUS_TIMING_FIXES", "SNX_ARCEUS_TARGET_FPS", "SNX_ARCEUS_FPS60",
        "SNX_ARCEUS_PHYSICS_TIMESTEP_FIX", "SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX", "SNX_ARCEUS_WORLD_TIMERS_FIX",
        "SNX_ARCEUS_GRAVITY_FIX", "SNX_ARCEUS_CAPTURE_FIX", "SNX_ARCEUS_BATTLE_SEQUENCE_FIX",
        "SNX_ARCEUS_MAP_CURSOR_FIX", "SNX_ARCEUS_FADE_FIX", "SNX_ARCEUS_EVENT_CAMERA_FIX",
        "SNX_ARCEUS_BLINK_FIX", "SNX_ARCEUS_CAMERA_INPUT_FIX", "SNX_ARCEUS_MENU_REPEAT_FIX",
        "SNX_ARCEUS_TIMER_CARRY_FIX", "SNX_ARCEUS_AI_TICK_FIX", "SNX_ARCEUS_TURN_FIX",
        "SNX_ARCEUS_LIST_SCROLL_FIX", "SNX_ARCEUS_WALL_JUMP_FIX", "SNX_ARCEUS_LEDGE_FIX",
        "SNX_ARCEUS_CLIMB_STEP", "SNX_ARCEUS_MOVEMENT_FIX", "SNX_ARCEUS_CAPPED_FALL_FIX",
        "SNX_ARCEUS_AIR_STEERING_FIX", "SNX_ARCEUS_RIDE_SWAP_FIX", "SNX_ARCEUS_MOVEMENT_SITES",
        "SNX_ARCEUS_RIDE_RAMP_FIX", "SNX_ARCEUS_CLOUD_FIX",
        "SNX_ARCEUS_CAMERA_SPEED",
        "SNX_ARCEUS_CLOCK_TRACE", "SNX_ARCEUS_DIALOGUE_TRACE", "SNX_ARCEUS_CAMERA_TRACE",
        "SNX_ARCEUS_TIMING_RESEARCH", "SNX_ARCEUS_CADENCE_CENSUS", "SNX_ARCEUS_GRAVITY_PROBE",
        "SNX_ARCEUS_FIXED_RESOLUTION", "SNX_ARCEUS_DIRECT_MOVE_FIX", "SNX_ARCEUS_FIELD_WAZA_FIX",
        "SNX_ARCEUS_WAZA_OBJECT_FIX", "SNX_ARCEUS_WAZA_SHOT_FIX", "SNX_ARCEUS_SCRIPT_TICK",
        "SNX_ARCEUS_SCRIPT_WINDOWS", "SNX_ARCEUS_DISCORD"
    };
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) unset_flag(all[i]);
    int saved_fps = g_opt_fps;
    static const int runtime_limits[] = {SNX_FPS_GAME, 30, 31, 60, 0};
    for (unsigned i = 0; i < sizeof runtime_limits / sizeof runtime_limits[0]; i++) {
        const int fps = runtime_limits[i];
        g_opt_fps = fps;
        main_base = 0;
        timing_enabled = clocks_installed = experimental = tracing = 0;
        low_fps_skip_logs = 0;
        ArceusClockConfig rate = arceus_clock_config(g_opt_fps);
        const int skip = fps == SNX_FPS_GAME || (fps > 0 && fps <= 30);
        assert(rate.master && !rate.invalid && !rate.write_fps && rate.skip_runtime_limit == skip);
        if (skip) {
            arceus_clock_init();
            assert(g_opt_fps == fps && main_base == 0 && !clocks_installed && !timing_enabled);
            assert(low_fps_skip_logs == 1);
        } else {
            arceus_clock_config_apply(rate);
            assert(g_opt_fps == fps && clocks_installed && timing_enabled);
            assert(low_fps_skip_logs == 0);
        }
    }
    g_opt_fps = SNX_FPS_GAME;
    main_base = 0;
    timing_enabled = clocks_installed = experimental = tracing = 0;
    low_fps_skip_logs = 0;
    ArceusClockConfig config = arceus_clock_config(g_opt_fps);
    assert(config.master && !config.invalid && config.mode == 0 && !config.write_fps && config.skip_runtime_limit);
    assert(config.initial_ns == (UINT64_C(1000000000) + 15) / 30);
    assert(g_opt_fps == SNX_FPS_GAME && !clocks_installed && !timing_enabled);
    flag("SNX_ARCEUS_TARGET_FPS", "game");
    config = arceus_clock_config(g_opt_fps);
    assert(!config.invalid && !config.write_fps && config.runtime_rate_override && !config.skip_runtime_limit &&
           config.initial_ns == (UINT64_C(1000000000) + 15) / 30);
    arceus_clock_config_apply(config);
    assert(g_opt_fps == SNX_FPS_GAME && clocks_installed && timing_enabled);
    timing_enabled = clocks_installed = experimental = tracing = 0;
    /* `game` keeps the runtime's limit even with the legacy 60 FPS switch set. */
    flag("SNX_ARCEUS_FPS60", "1");
    config = arceus_clock_config(g_opt_fps);
    assert(!config.invalid && config.mode == 0 && !config.write_fps && config.runtime_rate_override);
    arceus_clock_config_apply(config);
    assert(g_opt_fps == SNX_FPS_GAME && clocks_installed && timing_enabled);
    timing_enabled = clocks_installed = experimental = tracing = 0;
    unset_flag("SNX_ARCEUS_FPS60");
    g_opt_fps = 30;
    config = arceus_clock_config(g_opt_fps);
    assert(!config.invalid && !config.write_fps && config.runtime_rate_override && !config.skip_runtime_limit &&
           config.initial_ns == (UINT64_C(1000000000) + 15) / 30);
    arceus_clock_config_apply(config);
    assert(g_opt_fps == 30 && clocks_installed && timing_enabled);
    timing_enabled = clocks_installed = experimental = tracing = 0;
    g_opt_fps = SNX_FPS_GAME;
    flag("SNX_ARCEUS_TARGET_FPS", "");
    const char* const enabled[] = {
        "SNX_ARCEUS_PHYSICS_TIMESTEP_FIX", "SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX", "SNX_ARCEUS_WORLD_TIMERS_FIX",
        "SNX_ARCEUS_GRAVITY_FIX", "SNX_ARCEUS_CAPTURE_FIX", "SNX_ARCEUS_BATTLE_SEQUENCE_FIX",
        "SNX_ARCEUS_MAP_CURSOR_FIX", "SNX_ARCEUS_FADE_FIX", "SNX_ARCEUS_EVENT_CAMERA_FIX",
        "SNX_ARCEUS_BLINK_FIX", "SNX_ARCEUS_CAMERA_INPUT_FIX", "SNX_ARCEUS_MENU_REPEAT_FIX",
        "SNX_ARCEUS_TIMER_CARRY_FIX", "SNX_ARCEUS_AI_TICK_FIX", "SNX_ARCEUS_TURN_FIX",
        "SNX_ARCEUS_LIST_SCROLL_FIX", "SNX_ARCEUS_WALL_JUMP_FIX", "SNX_ARCEUS_LEDGE_FIX",
        "SNX_ARCEUS_MOVEMENT_FIX", "SNX_ARCEUS_CAPPED_FALL_FIX", "SNX_ARCEUS_AIR_STEERING_FIX",
        "SNX_ARCEUS_RIDE_SWAP_FIX", "SNX_ARCEUS_RIDE_RAMP_FIX",
        "SNX_ARCEUS_DIRECT_MOVE_FIX", "SNX_ARCEUS_FIELD_WAZA_FIX", "SNX_ARCEUS_WAZA_OBJECT_FIX",
        "SNX_ARCEUS_FIXED_RESOLUTION"
    };
    for (unsigned i = 0; i < sizeof enabled / sizeof enabled[0]; i++)
        assert(arceus_switch_enabled(enabled[i], 1));
    assert(waza_shot_mode_value() == 2);
    /* Bits 2 to 8 and bit 10, the player's horizontal position step. */
    assert(movement_mask_value() == 0x5fcul && climb_step_value() == 2);
    /* The sky cloud fix: unset is all eight fields, 1 the first three, 0 off. */
    assert(cloud_fix_value() == 2);
    flag("SNX_ARCEUS_CLOUD_FIX", "1");
    assert(cloud_fix_value() == 1);
    flag("SNX_ARCEUS_CLOUD_FIX", "0");
    assert(cloud_fix_value() == 0);
    unset_flag("SNX_ARCEUS_CLOUD_FIX");
    assert(ride_ramp_sites[0].owner == 0x257af70 && ride_ramp_sites[0].site == 0x257c9f4 &&
           !ride_ramp_sites[0].negative);
    assert(horizontal_move_site.owner == 0x1a0d9dc && horizontal_move_site.site == 0x1a0da20 &&
           !horizontal_move_site.negative);
    /* The script tick: unset is the Braviary bit and the throw window. 0 turns each off, and a
       mask is used as given. */
    assert(script_tick_wanted() == 4 && script_windows_wanted() == 1);
    flag("SNX_ARCEUS_SCRIPT_TICK", "0");
    flag("SNX_ARCEUS_SCRIPT_WINDOWS", "0");
    assert(script_tick_wanted() == 0 && script_windows_wanted() == 0);
    flag("SNX_ARCEUS_SCRIPT_TICK", "44");
    assert(script_tick_wanted() == 0x44);
    unset_flag("SNX_ARCEUS_SCRIPT_TICK");
    unset_flag("SNX_ARCEUS_SCRIPT_WINDOWS");
    /* The battle sequence fix: unset is the even landing, 1 the plain landing, 0 off, and
       anything else is off too. */
    assert(battle_sequence_value() == 2);
    flag("SNX_ARCEUS_BATTLE_SEQUENCE_FIX", "2");
    assert(battle_sequence_value() == 2);
    flag("SNX_ARCEUS_BATTLE_SEQUENCE_FIX", "1");
    assert(battle_sequence_value() == 1);
    flag("SNX_ARCEUS_BATTLE_SEQUENCE_FIX", "0");
    assert(battle_sequence_value() == 0);
    flag("SNX_ARCEUS_BATTLE_SEQUENCE_FIX", "3");
    assert(battle_sequence_value() == 0);
    unset_flag("SNX_ARCEUS_BATTLE_SEQUENCE_FIX");
    static const char* const probes[] = {
        "SNX_ARCEUS_CLOCK_TRACE", "SNX_ARCEUS_DIALOGUE_TRACE", "SNX_ARCEUS_CAMERA_TRACE",
        "SNX_ARCEUS_TIMING_RESEARCH", "SNX_ARCEUS_CADENCE_CENSUS", "SNX_ARCEUS_GRAVITY_PROBE"
    };
    for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++)
        assert(!arceus_switch_enabled(probes[i], 0));
    int invalid = 0;
    assert(camera_speed_value(&invalid) == 1.0f && !invalid);
    flag("SNX_ARCEUS_TARGET_FPS", "120");
    config = arceus_clock_config(g_opt_fps);
    assert(!config.invalid && !config.skip_runtime_limit && config.write_fps && config.target_fps == 120 &&
           config.initial_ns == 8333333);
    flag("SNX_ARCEUS_TARGET_FPS", "");
    g_opt_fps = 120;
    config = arceus_clock_config(g_opt_fps);
    assert(!config.write_fps && !config.skip_runtime_limit && config.initial_ns == 8333333);
    g_opt_fps = 0;
    config = arceus_clock_config(g_opt_fps);
    assert(!config.write_fps && !config.skip_runtime_limit && config.initial_ns == ARCEUS_UNLOCKED_INITIAL_STEP_NS);
    flag("SNX_ARCEUS_MOVEMENT_SITES", "7fc");
    assert(movement_mask_value() == 0x7fcul && (movement_mask_value() & 0x5fcul) == 0x5fcul &&
           (movement_mask_value() & 0x200ul));
    flag("SNX_ARCEUS_MOVEMENT_SITES", "1fc");
    assert(movement_mask_value() == 0x1fcul);
    for (unsigned i = 0; i < sizeof enabled / sizeof enabled[0]; i++) {
        flag(enabled[i], "0");
        assert(!arceus_switch_enabled(enabled[i], 1));
        unset_flag(enabled[i]);
    }
    flag("SNX_ARCEUS_WAZA_SHOT_FIX", "1");
    assert(waza_shot_mode_value() == 1);
    flag("SNX_ARCEUS_WAZA_SHOT_FIX", "2");
    assert(waza_shot_mode_value() == 2);
    flag("SNX_ARCEUS_WAZA_SHOT_FIX", "0");
    assert(waza_shot_mode_value() == 0);
    unset_flag("SNX_ARCEUS_WAZA_SHOT_FIX");
    flag("SNX_ARCEUS_WAZA_OBJECT_FIX", "0");
    const u64 saved_main_base = main_base;
    const int saved_clocks_installed = clocks_installed;
    main_base = 1;
    clocks_installed = 0;
    waza_dependency_warnings = 0;
    waza_shot_dependency_warned = 0;
    waza_shot_init();
    waza_shot_init();
    assert(waza_dependency_warnings == 1);
    waza_shot_dependency_warned = 0;
    waza_dependency_warnings = 0;
    main_base = saved_main_base;
    clocks_installed = saved_clocks_installed;
    unset_flag("SNX_ARCEUS_WAZA_OBJECT_FIX");
    flag("SNX_ARCEUS_CLIMB_STEP", "0");
    assert(climb_step_value() == 0);
    flag("SNX_ARCEUS_TIMING_FIXES", "0");
    config = arceus_clock_config(g_opt_fps);
    assert(!config.master);
    timing_enabled = clocks_installed = 0;
    arceus_clock_config_apply(config);
    assert(!timing_enabled && !clocks_installed);
    timer_carry_fix = 0;
    direct_move_fix = field_waza_fix = waza_object_fix = waza_shot_fix = 0;
    script_tick_mask = script_tick_wrapped = script_windows_mask = 0;
    arceus_clock_post_jit();
    assert(!timer_carry_fix && !direct_move_fix && !field_waza_fix && !waza_object_fix && !waza_shot_fix);
    /* The master switch leaves the script tick and its windows uninstalled. */
    assert(!script_tick_mask && !script_tick_wrapped && !script_windows_mask);
    g_opt_fps = saved_fps;
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) unset_flag(all[i]);
    timing_enabled = clocks_installed = experimental = tracing = 0;
}
/* A new air steering entry is scaled by step / (1/30). One second of entries sums to 30 authored ones. */
static void test_air_steering(void) {
    const u32 none[3] = {0};
    const float authored[3] = {0.25f, 0.0f, -0.1f};
    const float steps[4][2] = {{1.0f / 60, 1.0f / 60}, {1.0f / 120, 1.0f / 120}, {0.012f, 0.020f}, {0.008f, 0.030f}};
    memset(memory, 0, sizeof memory);
    for (unsigned k = 0; k < 3; k++) guest_float_write(object + 0x6f0 + 4 * k, authored[k]);
    /* No flag, a full 1/30 step and a slower frame leave the entry bit exact. */
    assert(!air_steering_scale(object, 0, none, 1.0f / 60));
    *GP(object + 0x6e0) = 1;
    assert(!air_steering_scale(object, 0, none, NOMINAL_STEP_SECONDS));
    assert(!air_steering_scale(object, 0, none, 0.05f));
    assert(!air_steering_scale(object, 0, none, 0.0f));
    for (unsigned k = 0; k < 3; k++) assert(guest_float(object + 0x6f0 + 4 * k) == authored[k]);
    for (unsigned schedule = 0; schedule < 4; schedule++) {
        double elapsed = 0, sum[3] = {0};
        for (unsigned call = 0; elapsed < 1.0; call++) {
            const float seconds = steps[schedule][call & 1];
            for (unsigned k = 0; k < 3; k++) guest_float_write(object + 0x6f0 + 4 * k, authored[k]);
            assert(air_steering_scale(object, 0, none, seconds));
            for (unsigned k = 0; k < 3; k++) sum[k] += guest_float(object + 0x6f0 + 4 * k);
            elapsed += seconds;
        }
        for (unsigned k = 0; k < 3; k++) assert(fabs(sum[k] - authored[k] * elapsed * 30) < 1e-4);
    }
    /* An entry still queued and not rewritten by this call is not scaled again. */
    u32 previous[3];
    for (unsigned k = 0; k < 3; k++) previous[k] = gr32(object + 0x6f0 + 4 * k);
    assert(!air_steering_scale(object, 1, previous, 1.0f / 60));
    for (unsigned k = 0; k < 3; k++) assert(gr32(object + 0x6f0 + 4 * k) == previous[k]);
    /* A rewritten entry on a queue that was already set is scaled. */
    guest_float_write(object + 0x6f0, authored[0]);
    assert(air_steering_scale(object, 1, previous, 1.0f / 60));
    assert(fabsf(guest_float(object + 0x6f0) - authored[0] / 2) < 1e-7f);
    /* A value that is not finite is left alone. */
    guest_float_write(object + 0x6f8, NAN);
    guest_float_write(object + 0x6f0, authored[0]);
    assert(!air_steering_scale(object, 0, none, 1.0f / 60));
    assert(guest_float(object + 0x6f0) == authored[0]);
}
/* The three requested schedules are replayed through the real cell and wrappers. */
/* The mock stands for the patched 1050E00 body: every context gains V9.S[1] frames. */
static unsigned ai_frames, ai_passes, ai_turns;
static void ai_tick_mock(CPU* c, u64 entry) {
    (void)entry;
    assert(c->v[9].s[0] == 0 && c->v[9].s[1] <= 1);
    ai_frames += c->v[9].s[1];
    gw32(object + 0x88, gr32(object + 0x88) + c->v[9].s[1]);
    ai_passes++;
    c->pc = c->x[30];
}
/* The mock stands for Turn and TurnKey: add the context's frames, turn one step, finish
   by the count. */
static void ai_turn_mock(CPU* c, u64 entry) {
    (void)entry;
    const u64 node = c->x[0], at = node + (gr32(node + 0x130) ? 0x134 : 0x144);
    const u32 duration = gr32(node + 0x130) ? gr32(node + 0x130) : gr32(node + 0x140);
    ai_turns++;
    if (duration) gw32(at, gr32(at) + gr32(object + 0x88));
    gw32(c->x[8], duration && gr32(at) < duration ? 4 : 1);
    c->pc = c->x[30];
}
/* The mock stands for 1727DF4: one slerp step toward the target with the game's fraction. */
static float turn_angle, turn_passed;
static void turn_mock(CPU* c, u64 entry) {
    (void)entry;
    turn_passed = c->v[0].f[0];
    const float t = guest_float(c->x[0] + 0x268) * NOMINAL_STEP_SECONDS / turn_passed;
    turn_angle += (1 - turn_angle) * (t < 1 ? t : 1);
    c->pc = c->x[30];
}
static u64 audit_step(unsigned schedule, unsigned i) {
    if (!schedule) return 16666667;
    if (schedule == 1) return i & 1 ? 20000000 : 12000000;
    return i % 31 == 30 ? 100000000 : 8000000 + (i * 1103515245u + 12345u) % 32000001;
}
static double audit_cursor_distance;
static void audit_map_original(CPU* c, u64 entry) {
    (void)entry;
    const MapCursorSite* site = &map_cursor_sites[0];
    audit_cursor_distance += guest_float(c->x[0] + site->speed);
    for (unsigned k = 0; k < 2; k++) {
        u64 at = c->x[0] + site->held + 4 * k;
        if (gr32(at) < 9) gw32(at, gr32(at) + 1);
    }
    c->pc = c->x[30];
}
/* Recover long-frame tick budget on later calls without replaying game callbacks. */
static const float cloud_increments[4] = {.004f, .006f, -.007f, -.008f};
static unsigned cloud_calls;
static int cloud_replace_sky;
static void cloud_original_stub(CPU* c, u64 entry) {
    assert(entry == entry_pc);
    const u64 sky = gr64(c->x[0] + 0x58);
    for (unsigned k = 0; k < 4; k++) {
        const u64 at = sky + 0x604 + 4 * k;
        float value = guest_float(at) + cloud_increments[k];
        if (value >= 1) value -= 1;
        if (value <= -1) value += 1;
        guest_float_write(at, value);
    }
    if (cloud_replace_sky) gw64(c->x[0] + 0x58, 0);
    cloud_calls++;
    c->x[0] = 77;
    c->pc = c->x[30];
}
static void test_cloud_phases(void) {
    const u64 sky = 1024;
    double reference[4] = {0};
    for (unsigned schedule = 0; schedule < 4; schedule++) {
        memset(memory, 0, sizeof memory);
        frame_step_address = 0xf00;
        gw64(object + 0x58, sky);
        cloud_phase_original = cloud_original_stub;
        cloud_calls = cloud_replace_sky = 0;
        for (unsigned k = 0; k < 4; k++) guest_float_write(sky + 0x604 + 4 * k, k < 2 ? .9995f : -.9995f);
        double sums[4] = {0};
        const unsigned frames = schedule == 0 ? 30 : schedule == 1 ? 60 : schedule == 2 ? 120 : 63;
        for (unsigned i = 0; i < frames; i++) {
            const u64 ns = schedule < 3 ? (1000000000ull + frames / 2) / frames
                                      : i == 62 ? 8000000 : (i & 1 ? 20000000 : 12000000);
            frame_step_write(ns);
            float before[4];
            for (unsigned k = 0; k < 4; k++) before[k] = guest_float(sky + 0x604 + 4 * k);
            CPU c = {.x = {[0] = 128, [30] = 32}};
            cloud_phase_update(&c, entry_pc);
            assert(c.x[0] == 77 && c.pc == return_pc);
            for (unsigned k = 0; k < 4; k++) {
                float after = guest_float(sky + 0x604 + 4 * k);
                if (!schedule) {
                    float expected = before[k] + cloud_increments[k];
                    if (expected >= 1) expected -= 1;
                    if (expected <= -1) expected += 1;
                    assert(float_bits(after) == float_bits(expected));
                }
                float delta = after - before[k];
                if (delta > .5f) delta -= 1;
                if (delta < -.5f) delta += 1;
                sums[k] += delta;
            }
        }
        assert(cloud_calls == frames);
        for (unsigned k = 0; k < 4; k++) {
            if (!schedule) reference[k] = sums[k];
            else assert(fabs(sums[k] - reference[k]) < 1e-5);
        }
    }
    /* If the original replaces its sky block, leave the old block's native output alone. */
    gw64(object + 0x58, sky);
    cloud_replace_sky = 1;
    frame_step_write(16666667);
    float expected = guest_float(sky + 0x604) + cloud_increments[0];
    if (expected >= 1) expected -= 1;
    CPU c = {.x = {[0] = 128, [30] = 32}};
    cloud_phase_update(&c, entry_pc);
    assert(guest_float(sky + 0x604) == expected);
    cloud_replace_sky = 0;
    frame_step_address = 0;
}
static void test_timer_carry(void) {
    for (unsigned enabled = 0; enabled < 2; enabled++) {
        timer_carry_fix = (int)enabled;
        memset(memory, 0, sizeof memory);
        memset(capture_slots, 0, sizeof capture_slots);
        capture_next_slot = 0;
        memset(map_cursor_object, 0, sizeof map_cursor_object);
        memset(map_cursor_ns, 0, sizeof map_cursor_ns);
        map_cursor_original[0] = audit_map_original;
        guest_float_write(object + map_cursor_sites[0].speed, 1);
        u64 elapsed = 0;
        unsigned captures = 0, holds = 0;
        for (unsigned i = 0; i < 420; i++) {
            const u64 ns = i < 300 ? audit_step(2, i) : 12000000;
            step.step = ns;
            elapsed += ns;
            captures += capture_tick(object, 1);
            /* A fresh held counter makes every retained tick observable. */
            gw32(object + map_cursor_sites[0].held, 0);
            gw32(object + map_cursor_sites[0].held + 4, 0);
            CPU c = {.x = {[0] = 128, [30] = 32}};
            field_map_cursor_update(&c, entry_pc);
            holds += gr32(object + map_cursor_sites[0].held);
        }
        if (enabled) {
            assert(captures == elapsed / CAPTURE_TICK_NS);
            assert(holds == elapsed / ARCEUS_WHOLE_STEP_NS);
            assert(capture_slots[0].ns == (int64_t)(elapsed % CAPTURE_TICK_NS));
            assert(map_cursor_ns[0] == elapsed % ARCEUS_WHOLE_STEP_NS);
        } else {
            assert(captures < elapsed / CAPTURE_TICK_NS);
            assert(holds < elapsed / ARCEUS_WHOLE_STEP_NS);
        }
    }
    /* Repeated slow calls cannot grow an unbounded recovery delay. */
    for (unsigned i = 0; i < 100; i++) {
        step.step = 100000000;
        (void)capture_tick(object, 1);
        CPU c = {.x = {[0] = 128, [30] = 32}};
        field_map_cursor_update(&c, entry_pc);
    }
    assert(capture_slots[0].ns <= (int64_t)TIMER_CARRY_LIMIT_NS);
    assert(map_cursor_ns[0] <= TIMER_CARRY_LIMIT_NS);
    /* The cursor mock ran and moved the cursor. */
    assert(audit_cursor_distance > 0);
    timer_carry_fix = 0;
}
static void test_ai_tick(void) {
    const u64 saved_base = main_base;
    const u64 node = 512, owner = 1024, out = 2048;
    ai_tick_original = ai_tick_mock;
    ai_turn_original[0] = ai_turn_original[1] = ai_turn_mock;
    /* Schedules: 30 FPS, 30 FPS with jitter, 60, 120, 12/20 ms, 8..40 ms with stalls. */
    for (unsigned schedule = 0; schedule < 6; schedule++) {
        memset(memory, 0, sizeof memory);
        memset(ai_tick_slots, 0, sizeof ai_tick_slots);
        ai_frames = ai_passes = ai_turns = 0;
        ai_turn_holds[0] = ai_turn_holds[1] = 0;
        main_base = 3072 - 0x40e7c04;
        gw32(3072, 4);
        gw32(node + 0x130, 30);
        gw32(node + 0x140, 30);
        gw64(node + 0x58, owner);
        gw64(owner + 0x90, object);
        double want = 0;
        for (unsigned i = 0; i < 2000; i++) {
            const u64 ns = schedule == 0 ? 33333333 : schedule == 1 ? 31000000 + i * 7919 % 5000000 :
                           schedule == 2 ? 16666667 : schedule == 3 ? 8333333 : audit_step(schedule - 3, i);
            step.step = ns;
            want += ns >= 30000000 ? 1 : ns / 33333333.0;
            CPU c = {.x = {[0] = 128, [30] = 32}};
            c.v[9].s[0] = 0x11111111;
            c.v[9].s[1] = 0x22222222;
            gw32(object + 0x88, 0);
            ai_tick_update(&c, entry_pc);
            assert(c.pc == return_pc && c.v[9].s[0] == 0x11111111 && c.v[9].s[1] == 0x22222222);
            /* At 30 FPS, even or uneven, every pass ticks as in the game. */
            if (schedule < 2) assert(ai_frames == i + 1);
            if (schedule == 2) assert(ai_frames == (i + 1) / 2);
            assert(fabs(ai_frames - want) < 1.04);
        }
        assert(ai_passes == 2000);
    }
    /* A turn of 30 frames on a context the actor reads every pass (forced) and on one it
       reads every 4th pass, which then holds the frames of all 4. Both kinds. */
    unsigned reference[2] = {0, 0};
    for (unsigned every = 1; every <= 4; every += 3)
        for (unsigned schedule = 0; schedule < 6; schedule++)
            for (unsigned kind = 0; kind < 2; kind++) {
                memset(memory, 0, sizeof memory);
                memset(ai_tick_slots, 0, sizeof ai_tick_slots);
                memset(ai_turn_nodes, 0, sizeof ai_turn_nodes);
                ai_pass_total = ai_tick_total = 0;
                ai_turns = 0;
                ai_turn_holds[kind] = ai_turn_runs[kind] = 0;
                gw32(3072, 4);
                gw32(node + (kind ? 0x140 : 0x130), 30);
                gw64(node + 0x58, owner);
                gw64(owner + 0x90, object);
                memory[object + 0x81] = every == 1;
                unsigned calls = 0, ticks = 0, last_run = 0, done = 0;
                for (unsigned i = 0; !done; i++) {
                    assert(i < 4000);
                    step.step = schedule == 0 ? 33333333 : schedule == 1 ? 31000000 + i * 7919 % 5000000 :
                                schedule == 2 ? 16666667 : schedule == 3 ? 8333333 : audit_step(schedule - 3, i);
                    /* The pass after the actor's read starts the context's count again. */
                    if (i % every == 0) gw32(object + 0x88, 0);
                    CPU c = {.x = {[0] = 128, [30] = 32}};
                    const unsigned before = ai_frames;
                    ai_tick_update(&c, entry_pc);
                    ticks += ai_frames - before;
                    if (i % every != every - 1) continue;
                    const unsigned ran = ai_turns;
                    CPU t = {.x = {[0] = node, [8] = out, [30] = 32}};
                    gw32(out, 99);
                    ai_turn_update(&t, entry_pc, kind);
                    assert(t.pc == return_pc && (gr32(out) == 4 || gr32(out) == 1));
                    calls++;
                    done = gr32(out) == 1;
                    /* The first call runs. A held call reports running and does not turn. */
                    if (calls == 1) assert(ai_turns == 1);
                    if (ai_turns == ran) assert(gr32(out) == 4);
                    /* 30 FPS never holds. At 60 and 120 a step comes every 2nd and 4th call. */
                    if (schedule < 2) assert(ai_turns == calls);
                    if (ai_turns != ran && ai_turns > 2 && (schedule == 2 || schedule == 3))
                        assert(calls - last_run == (schedule == 2 ? 2u : 4u));
                    if (ai_turns != ran) last_run = calls;
                }
                /* The same number of steps, so the same angle, and the same length in ticks. */
                if (schedule == 0) reference[every == 4] = ai_turns;
                assert(reference[every == 4] == (every == 1 ? 30u : 8u));
                if (schedule < 4) assert(ai_turns == reference[every == 4]);
                assert(ai_turns + 1 >= reference[every == 4] && ai_turns <= reference[every == 4] + 1);
                assert(ticks >= 30 && ticks <= 30 + every);
                assert(gr32(node + (kind ? 0x144 : 0x134)) >= 30);
                assert(ai_turn_runs[kind] == ai_turns && ai_turn_holds[kind] == calls - ai_turns);
            }
    /* A zero duration and a missing context both reach the original. */
    memset(memory, 0, sizeof memory);
    gw32(3072, 4);
    gw64(node + 0x58, owner);
    gw64(owner + 0x90, object);
    CPU t = {.x = {[0] = node, [8] = out, [30] = 32}};
    ai_turns = 0;
    ai_turn_update(&t, entry_pc, 0);
    gw32(node + 0x140, 30);
    gw64(owner + 0x90, 0);
    ai_turn_update(&t, entry_pc, 1);
    assert(ai_turns == 2);
    /* Two schedulers keep separate tick phases. */
    memset(ai_tick_slots, 0, sizeof ai_tick_slots);
    assert(!ai_tick_count(1, 16666667) && !ai_tick_count(2, 16666667));
    assert(ai_tick_count(1, 16666667) && ai_tick_count(2, 16666667));
    main_base = saved_base;
}
static void test_turn(void) {
    /* Rate and duration pairs: a 1/30 s duration as 1A19104 passes, a looked-up one, a snap. */
    static const float cases[3][2] = {{.2f, 1.0f / 30}, {3, .5f}, {2, 1.0f / 30}};
    memset(memory, 0, sizeof memory);
    frame_step_address = 0xf00;
    turn_original = turn_mock;
    for (unsigned k = 0; k < 3; k++) {
        float reached[4];
        guest_float_write(object + 0x268, cases[k][0]);
        /* 0.8 s at 30, 60 and 120 FPS and at 12/20 ms: 24, 48, 96 and 50 calls. */
        for (unsigned schedule = 0; schedule < 4; schedule++) {
            static const unsigned calls_for[4] = {24, 48, 96, 50};
            turn_angle = 0;
            for (unsigned i = 0; i < calls_for[schedule]; i++) {
                const u64 ns = schedule == 0 ? 33333333 : schedule == 1 ? 16666667 : schedule == 2 ? 8333333 :
                               i & 1 ? 20000000 : 12000000;
                frame_step_write(ns);
                CPU c = {.x = {[0] = 128, [30] = 32}};
                c.v[0].f[0] = cases[k][1];
                turn_update(&c, entry_pc);
                assert(c.pc == return_pc);
                /* 30 FPS and a snap pass the authored duration through, bit for bit. */
                if (!schedule || k == 2) assert(turn_passed == cases[k][1]);
                else assert(turn_passed > cases[k][1]);
            }
            reached[schedule] = turn_angle;
            assert(fabsf(reached[schedule] - reached[0]) < 1e-4f);
        }
        assert(k == 2 ? reached[0] == 1 : reached[0] > .9f && reached[0] < 1);
    }
    /* The helper's early out for a tiny duration and a zero rate stay untouched. */
    assert(turn_duration(.2f, 1e-8f, .5f) == 1e-8f && turn_duration(0, .5f, .5f) == .5f);
    frame_step_address = 0;
}
/* A model of the game's scroller: an ease of 6 updates to the next row, then a free scroll per update. */
static void list_scroll_mock(CPU* c, u64 entry) {
    (void)entry;
    const u64 scroller = c->x[0];
    const u32 state = gr32(scroller + 0x1b8);
    if (state == 1) {
        const u32 count = gr32(scroller + 0x1bc) + 1;
        gw32(scroller + 0x1bc, count);
        guest_float_write(scroller + 0x1d4, (float)count / 6);
        if (count == 6) gw32(scroller + 0x1b8, 0);
    } else if (state == 4)
        guest_float_write(scroller + 0x1d4, guest_float(scroller + 0x1d4) + guest_float(scroller + 0x1d8));
    c->pc = c->x[30];
}
/* A tap of 100 ms scrolls the list one row at any frame rate with the fix, and several rows without it. */
static void test_list_scroll(void) {
    static const unsigned rates[] = {30, 60, 150, 300};
    list_scroll_original = list_scroll_mock;
    for (unsigned fix = 0; fix < 2; fix++)
        for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
            memset(memory, 0, sizeof memory);
            gw32(object + 0x1b8, 1);
            u64 remainder = 0, elapsed = 0;
            unsigned eased = 0;
            while (elapsed < 1000000000ull) {
                const u64 ns = 1000000000ull / rates[r];
                elapsed += ns;
                remainder += ns;
                blink_ticks = (unsigned)(remainder / ARCEUS_WHOLE_STEP_NS);
                remainder %= ARCEUS_WHOLE_STEP_NS;
                const int held = elapsed <= 100000000ull;
                /* The list: a finished ease with the direction still held starts the free scroll, a release ends it. */
                if (!gr32(object + 0x1b8) && !eased) {
                    eased = 1;
                    if (held) gw32(object + 0x1b8, 4), guest_float_write(object + 0x1d8, 0.25f);
                }
                if (!held && gr32(object + 0x1b8) == 4) gw32(object + 0x1b8, 0);
                CPU c = {.x = {[0] = object, [30] = 64}, .pc = entry_pc};
                /* Without the fix the switch installs no wrapper and the game runs its own update. */
                if (fix) list_scroll_update(&c, entry_pc);
                else list_scroll_mock(&c, entry_pc);
                assert(c.pc == 64);
            }
            const float rows = guest_float(object + 0x1d4);
            if (fix || rates[r] <= 60) assert(fabsf(rows - 1.0f) < 1e-4f);
            else assert(rows > 2.0f);
        }
    blink_ticks = 0;
}
static void test_diagnostics(void) {
    SnxModInfo main_mod = {.name = "main", .base = 4096,
        .build_id = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                     0xd9,0xf3,0xc5,0x1e}};
    SnxModule module = {.nmods = 1, .mods = &main_mod};
    g_mod = &module;
    const char* flags[] = {"SNX_ARCEUS_CAMERA_TRACE", "SNX_ARCEUS_TIMING_RESEARCH"};
    for (unsigned i = 0; i < 2; i++) {
        flag(flags[i], "1");
        main_base = 0;
        arceus_clock_init();
        assert(main_base == main_mod.base && !clocks_installed);
        main_base = 0;
        main_mod.build_id[0] ^= 1;
        arceus_clock_init();
        assert(main_base == 0);
        main_mod.build_id[0] ^= 1;
        flag(flags[i], "0");
    }
}
/* SNX_ARCEUS_TARGET_FPS=unlocked or 0 reaches the build check like 60 and 120, and any other
   value is refused before it. */
static void test_target(void) {
    SnxModInfo main_mod = {.name = "main", .base = 4096,
        .build_id = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                     0xd9,0xf3,0xc5,0x1e}};
    SnxModule module = {.nmods = 1, .mods = &main_mod};
    g_mod = &module;
    const char* accepted[] = {"game", "unlocked", "0", "60", "120"};
    for (unsigned i = 0; i < 5; i++) {
        flag("SNX_ARCEUS_TARGET_FPS", accepted[i]);
        main_base = 0;
        arceus_clock_init();
        assert(main_base == main_mod.base);
    }
    const char* refused[] = {"144", "unlimited", "-1"};
    for (unsigned i = 0; i < 3; i++) {
        flag("SNX_ARCEUS_TARGET_FPS", refused[i]);
        main_base = 0;
        arceus_clock_init();
        assert(main_base == 0);
        /* The throw guard's module is taken before the target is checked, and only from the 1.1.1 build. */
        assert(throw_guard_mod == &main_mod);
    }
    flag("SNX_ARCEUS_TIMING_FIXES", "0"); /* and before the master */
    arceus_clock_init();
    assert(throw_guard_mod == &main_mod);
    unset_flag("SNX_ARCEUS_TIMING_FIXES");
    main_mod.build_id[0] ^= 1;
    arceus_clock_init();
    assert(!throw_guard_mod);
    main_mod.build_id[0] ^= 1;
    /* The whole id is compared, not its first bytes. */
    main_mod.build_id[19] ^= 1;
    arceus_clock_init();
    assert(!throw_guard_mod);
    main_mod.build_id[19] ^= 1;
    flag("SNX_ARCEUS_TARGET_FPS", "");
}
/* The climb fix's cell load: ADRP X9 to the frame-step cell's page from the patched word,
   checked against llvm-mc encodings of adrp x9 at +0x3000, -0x5000 and +0x7FF000. */
static void test_climb_adrp(void) {
    frame_step_address = 0x10003000;
    assert(climb_adrp(0x10000ffc, 9) == 0xf0000009);
    frame_step_address = 0x0fffb000;
    assert(climb_adrp(0x10000000, 9) == 0xf0ffffc9);
    frame_step_address = 0x107ff000;
    assert(climb_adrp(0x10000123, 9) == 0xf0003fe9);
    frame_step_address = 0;
}
/* The climb stop: the stub adds one to +D04 in state 2 and finishes at stop_frames, as the
   animation does. Staging holds the count to whole 1/30 s steps. */
static const u64 climber = 128, cell = 3840;
static u32 stop_frames;
static unsigned stop_calls;
static void climb_original(CPU* c, u64 entry) {
    (void)c; (void)entry;
    stop_calls++;
    if (gr32(climber + 0xd00) != 2) return;
    const u32 frame = gr32(climber + 0xd04) + 1;
    gw32(climber + 0xd04, frame);
    if ((int)frame >= (int)stop_frames) gw32(climber + 0xd00, 1);
}
static double climb_stop_seconds(const float* steps, unsigned n) {
    gw32(climber + 0xd00, 2);
    gw32(climber + 0xd04, 0);
    stop_calls = 0;
    double seconds = 0;
    for (unsigned i = 0; gr32(climber + 0xd00) == 2 && i < 10000; i++) {
        guest_float_write(cell, steps[i % n]);
        seconds += steps[i % n];
        CPU cpu = {.x = {[0] = climber}};
        climb_anim_update(&cpu, 0);
    }
    assert(gr32(climber + 0xd00) == 1);
    return seconds;
}
static void test_climb_stop(void) {
    memset(memory, 0, sizeof memory);
    memset(climb_stop_slots, 0, sizeof climb_stop_slots);
    frame_step_address = cell;
    climb_anim_original = climb_original;
    climb_stop_fix = 1;
    stop_frames = 3;
    const float at30 = 1.0f / 30, at120 = 1.0f / 120, at1000 = 1.0f / 1000;
    climb_stop_seconds(&at30, 1);
    assert(stop_calls == 3);
    climb_stop_seconds(&at120, 1);
    assert(stop_calls == 12);
    climb_stop_seconds(&at1000, 1);
    assert(stop_calls >= 99 && stop_calls <= 101);
    /* Mixed steps end once three whole 1/30 s have passed, within one call. */
    const float mixed[] = {1.0f / 144, 1.0f / 60, 1.0f / 240, 1.0f / 90, 1.0f / 30};
    const double seconds = climb_stop_seconds(mixed, 5);
    assert(seconds >= 0.1 - 1e-6 && seconds < 0.1 + 1.0 / 30);
    /* An interrupted stop frees the slot, and the next stop starts from zero. */
    gw32(climber + 0xd00, 2);
    gw32(climber + 0xd04, 0);
    guest_float_write(cell, at120);
    CPU cpu = {.x = {[0] = climber}};
    climb_anim_update(&cpu, 0);
    gw32(climber + 0xd00, 1);
    climb_anim_update(&cpu, 0);
    assert(!climb_stop_slots[0].object);
    climb_stop_seconds(&at120, 1);
    assert(stop_calls == 12);
    climb_stop_fix = 0;
    frame_step_address = 0;
}
/* The wall jump wait: a fake Lua state whose stack holds the setter closure, the context
   userdata, the key and the value, run as the ride setter converts and stores each write. */
static const u64 lua_L = 0x200, lua_ci = 0x300, lua_func = 0x400, lua_closure = 0x600, lua_udata = 0x700,
                 lua_key = 0x800, jumper = 0x900, fake_main = 0x100000;
static void lua_value(u64 at, u64 value, u32 tag) {
    gw64(at, value);
    gw32(at + 8, tag);
}
static void wall_jump_conversion(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = gr64(lua_func + 0x30);
}
static void fake_lua(const char* key) {
    gw64(lua_L + 0x10, lua_func + 0x40);
    gw64(lua_L + 0x20, lua_ci);
    gw64(lua_ci, lua_func);
    lua_value(lua_func, lua_closure, 0x66);
    lua_value(lua_func + 0x10, lua_udata, 0x47);
    lua_value(lua_func + 0x20, lua_key, 0x44);
    gw64(lua_closure + 0x18, fake_main + 0x2335e5c);
    gw64(lua_udata + 0x28, jumper);
    const u32 length = (u32)strlen(key);
    gw32(lua_key + 8, 4 | length << 24);
    memcpy(GP(lua_key + 0x18), key, length);
}
/* One script write of value through the wrapper; returns what the setter stores. */
static int wall_jump_write(long long value, u64 caller) {
    lua_value(lua_func + 0x30, (u64)value, 0x13);
    CPU cpu = {.x = {[0] = lua_L, [1] = 3, [30] = caller}};
    wall_jump_update(&cpu, 0x50ba0);
    gw32(jumper + 0xf4, (u32)cpu.x[0]);
    return (int)cpu.x[0];
}
static unsigned wall_jump_countdown(float step) {
    guest_float_write(cell, step);
    wall_jump_write(15, fake_main + 0x2331b64);
    unsigned updates = 0;
    while ((int)gr32(jumper + 0xf4) > 0 && updates < 1000) {
        wall_jump_write((int)gr32(jumper + 0xf4) - 1, fake_main + 0x2331b64);
        updates++;
    }
    return updates;
}
static void test_wall_jump(void) {
    memset(memory, 0, sizeof memory);
    memset(wall_jump_slots, 0, sizeof wall_jump_slots);
    frame_step_address = cell;
    main_base = fake_main;
    wall_jump_original = wall_jump_conversion;
    fake_lua("climbingJumpWaitCount");
    assert(wall_jump_countdown(1.0f / 30) == 15);
    assert(wall_jump_countdown(1.0f / 120) == 60);
    assert(wall_jump_countdown(1.0f / 1000) >= 495 && wall_jump_countdown(1.0f / 1000) <= 505);
    /* Another caller or another key is never held. */
    guest_float_write(cell, 1.0f / 120);
    gw32(jumper + 0xf4, 10);
    assert(wall_jump_write(9, fake_main + 0x2331b00) == 9);
    fake_lua("climbingJumpWaitCoun_");
    gw32(jumper + 0xf4, 10);
    assert(wall_jump_write(9, fake_main + 0x2331b64) == 9);
    /* A full step keeps its carry: .25 .25 .25 1 .25 from 5 ends at 3 with no fraction left. */
    memset(wall_jump_slots, 0, sizeof wall_jump_slots);
    const float wants[] = {.25f, .25f, .25f, 1, .25f};
    int value = 5;
    for (unsigned i = 0; i < 5; i++)
        if (!wall_jump_hold(jumper, value, value - 1, wants[i])) value--;
    assert(value == 3 && wall_jump_slots[0].progress == 0.0f);
    main_base = 0;
    frame_step_address = 0;
}
/* The ledge throttle through the real wrappers: a stub state script that ends the climb from its
   PreUpdate (so the window's owner comes from the wrapper), then 1/120 s frames. */
static const u64 ledge_state = 0x500, ledge_other = 0x540, ledge_controller = 0x600;
static unsigned ledge_runs[2], ledge_other_runs, ledge_script_step;
static int ledge_end_at, ledge_finish_at;
static void ledge_state_original(CPU* c, u64 entry, unsigned post) {
    (void)entry;
    if (c->x[0] == ledge_other) {
        ledge_other_runs++;
        c->x[0] = 1;
        return;
    }
    ledge_runs[post]++;
    c->x[0] = 1;
    if (post) return;
    ledge_script_step++;
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    if ((int)ledge_script_step == ledge_end_at) ledge_end_start(&end, 0x2553ee0);
    if ((int)ledge_script_step == ledge_finish_at) ledge_end_finish(&end, 0x2554008);
}
static void ledge_pre_original(CPU* c, u64 entry) { ledge_state_original(c, entry, 0); }
static void ledge_post_original(CPU* c, u64 entry) { ledge_state_original(c, entry, 1); }
static void ledge_end_original(CPU* c, u64 entry) { (void)c; (void)entry; }
/* One engine frame: a new frame id, then Pre and Post of object. Returns 1 when Pre ran. */
static int ledge_frame(u64 object, float step) {
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
    guest_float_write(cell, step);
    const unsigned before = ledge_runs[0];
    for (unsigned post = 0; post < 2; post++) {
        CPU cpu = {.x = {[0] = object, [30] = return_pc}, .pc = 16};
        if (post) ledge_post_update(&cpu, 0x2616ab0);
        else ledge_pre_update(&cpu, 0x26163e0);
        assert(cpu.pc == return_pc || cpu.x[0] == 1);
        if (cpu.x[0] != 1) assert(cpu.x[0] == 7);
    }
    return ledge_runs[0] != before;
}
static void test_ledge(void) {
    memset(memory, 0, sizeof memory);
    ledge_retire(&ledge_window);
    frame_step_address = cell;
    gw32(ledge_state + 0x6c, 7);
    gw32(ledge_other + 0x6c, 7);
    ledge_originals[0] = ledge_pre_original;
    ledge_originals[1] = ledge_post_original;
    ledge_end_originals[0] = ledge_end_original;
    ledge_end_originals[1] = ledge_end_original;
    ledge_end_at = 1;
    ledge_finish_at = -1;
    /* The frame that ends the climb runs. Then 84 frames at 120 run the script 21 times, as 0.7 s at 30. */
    assert(ledge_frame(ledge_state, 1.0f / 120) && ledge_window.open && ledge_window.owner == ledge_state);
    unsigned runs = 0;
    for (unsigned i = 0; i < 84; i++) {
        const unsigned post_before = ledge_runs[1];
        const int ran = ledge_frame(ledge_state, 1.0f / 120);
        assert((ledge_runs[1] != post_before) == ran);
        runs += (unsigned)ran;
        /* An unrelated state is never held back. */
        ledge_frame(ledge_other, 1.0f / 120);
    }
    assert(runs == 21 && ledge_other_runs == 168);
    /* EndClimbingEnd from the script closes the window, and every update runs again. */
    ledge_finish_at = (int)ledge_script_step + 1;
    while (ledge_window.open) ledge_frame(ledge_state, 1.0f / 120);
    for (unsigned i = 0; i < 4; i++) assert(ledge_frame(ledge_state, 1.0f / 120));
    /* At 30 FPS nothing is skipped. */
    ledge_retire(&ledge_window);
    ledge_end_at = (int)ledge_script_step + 1;
    ledge_finish_at = -1;
    for (unsigned i = 0; i < 20; i++) assert(ledge_frame(ledge_state, 1.0f / 30));
    /* An end call outside any state update opens nothing. */
    ledge_retire(&ledge_window);
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    ledge_end_start(&end, 0x2553ee0);
    assert(!ledge_window.open);
    /* The pure rules: a skipped Pre whose Post never came is not applied in a later frame, a
       window expires after 2 s, and another end type of the controller retires it. */
    LedgeWindow w;
    ledge_open(&w, ledge_state, ledge_controller, 1000000000);
    assert(ledge_pre(&w, NULL, ledge_state, 1008333333, .25f, 1));
    assert(!ledge_post(&w, ledge_state, 2));
    assert(ledge_pre(&w, NULL, ledge_state, 1016666666, .25f, 3) && ledge_post(&w, ledge_state, 3));
    assert(!ledge_pre(&w, NULL, ledge_state, 3100000000ull, .25f, 4) && !w.open);
    ledge_open(&w, ledge_state, ledge_controller, 1000000000);
    ledge_end_call(&w, NULL, ledge_controller, 3, ledge_state, 1000000000);
    assert(!w.open);
    frame_step_address = 0;
}
/* Ledge smoothing through the real wrappers: the stub script reads the ride's height and asks
   for one more through the set position wrapper, as the Lua binding at 1FF6B5C does, up to 15. */
static const u64 ride_request = 0x700, ride_direction = 0x710;
static float ride_y, post_read, last_request;
static unsigned ride_sets;
static int smooth_end_at, smooth_finish_at;
static unsigned smooth_step;
static void ride_set_original(CPU* c, u64 entry) {
    (void)entry;
    ride_y = guest_float(c->x[1] + 4);
    ride_sets++;
}
static void ride_script_set(float y, u64 caller) {
    guest_float_write(ride_request + 4, y);
    CPU set = {.x = {[0] = ledge_controller, [1] = ride_request, [2] = ride_direction, [30] = caller}};
    ledge_set_position(&set, fake_main + 0x1983770);
}
static void smooth_original(CPU* c, u64 entry, unsigned post) {
    (void)entry;
    c->x[0] = 1;
    if (post) {
        post_read = ride_y;
        return;
    }
    smooth_step++;
    CPU end = {.x = {[0] = ledge_controller, [1] = 0}};
    if ((int)smooth_step == smooth_end_at) ledge_end_start(&end, 0x2553ee0);
    if (ledge_window.open && ride_y < 15) {
        last_request = ride_y + 1;
        ride_script_set(last_request, fake_main + 0x1ff6b5c);
    }
    if ((int)smooth_step == smooth_finish_at) ledge_end_finish(&end, 0x2554008);
}
static void smooth_pre(CPU* c, u64 entry) { smooth_original(c, entry, 0); }
static void smooth_post(CPU* c, u64 entry) { smooth_original(c, entry, 1); }
static int smooth_frame(float step) {
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
    guest_float_write(frame_step_address, step);
    const unsigned before = smooth_step;
    CPU pre = {.x = {[0] = ledge_state, [30] = return_pc}};
    ledge_pre_update(&pre, 0x26163e0);
    CPU post = {.x = {[0] = ledge_state, [30] = return_pc}};
    ledge_post_update(&post, 0x2616ab0);
    return smooth_step != before;
}
static void smooth_start(void) {
    memset(memory, 0, sizeof memory);
    ledge_retire(&ledge_window);
    ledge_flush.valid = 0;
    ledge_fix_enabled = 1;
    frame_step_address = 0xc00;
    main_base = fake_main;
    gw32(ledge_state + 0x6c, 7);
    guest_float_write(ride_direction + 8, 1);
    ledge_originals[0] = smooth_pre;
    ledge_originals[1] = smooth_post;
    ledge_end_originals[0] = ledge_end_original;
    ledge_end_originals[1] = ledge_end_original;
    ledge_set_position_original = ride_set_original;
    ride_y = 0;
    ride_sets = 0;
    smooth_step = 0;
    smooth_end_at = 1;
    smooth_finish_at = -1;
}
static void test_ledge_smoothing(void) {
    smooth_start();
    float last = 0;
    unsigned moved = 0, frames = 0;
    for (unsigned guard = 0; smooth_step < 30 && guard < 400; guard++) {
        const int ran = smooth_frame(1.0f / 120);
        /* A running Post reads the script's own latest request, never the delayed point. */
        if (ran && smooth_step > 1 && ride_y < 15) assert(post_read == last_request);
        assert(ride_y >= last);
        /* While the ride is on its way up it moves on most frames, not only every fourth. */
        if (last_request < 15 && smooth_step > 2) {
            moved += ride_y != last;
            frames++;
        }
        last = ride_y;
    }
    assert(frames > 40 && moved * 4 >= frames * 3);
    /* The skipped frames are placed through the original setter, not only the script's runs. */
    assert(ride_sets > 2 * smooth_step);
    /* The script finishes inside a run while the ride is a step behind: it settles on 15. */
    smooth_finish_at = (int)smooth_step + 1;
    while (ledge_window.open) smooth_frame(1.0f / 120);
    assert(ride_y == 15 && !ledge_flush.valid);
    /* At 30 FPS the ride stands on each request at once. */
    smooth_start();
    for (unsigned i = 0; i < 10; i++) {
        smooth_frame(1.0f / 30);
        if (i) assert(ride_y == last_request && post_read == last_request);
    }
    /* Movement from anything but the script ends the steering and is kept. */
    smooth_start();
    for (unsigned i = 0; i < 12; i++) smooth_frame(1.0f / 120);
    assert(ledge_window.open);
    ride_script_set(50, fake_main + 0x1234);
    assert(!ledge_window.open && ride_y == 50);
    ledge_set_position_original = NULL;
    ledge_fix_enabled = 0;
    main_base = 0;
    frame_step_address = 0;
}
/* A fade-in's last call frees the object's slot, so fades of more than FADE_SLOTS objects
   in a session all keep a slot instead of running unfixed. */
static void test_fade_slots_free(void) {
    memset(memory, 0, sizeof memory);
    memset(fade_slots, 0, sizeof fade_slots);
    fade_full = 0;
    fade_fix_active = 1;
    frame_step_address = 4000;
    guest_float_write(frame_step_address, 1.0f / 120);
    for (unsigned k = 0; k < 3 * FADE_SLOTS; k++) {
        const u64 o = 128 + 8 * k;
        FadeStage st;
        gw32(o + 0x500, 0);
        gw32(o + 0x504, 2);
        gw32(o + 0x538, 3);
        gw32(o + 0x53c, 8);
        fade_before(o, &st);
        assert(st.slot >= 0);
        fade_after(&st);
        /* The fade-in reaches its end: the call is left alone and the slot is released. */
        gw32(o + 0x538, 8);
        fade_before(o, &st);
        assert(st.slot < 0);
    }
    assert(fade_full == 0);
    for (unsigned k = 0; k < FADE_SLOTS; k++) assert(fade_slots[k].object == 0);
    fade_fix_active = 0;
    frame_step_address = 0;
}
/* The capped-fall guard only accepts a bit-1 patch loading this cell's negative step word. */
static void test_capped_fall_body(void) {
    const u64 pc = 0x10001234;
    frame_step_address = 0x10003000;
    assert(capped_fall_body(pc, 0x52911128, 0x72b7a108) == 1);
    assert(capped_fall_body(pc, climb_adrp(pc, 8), 0xb9400508) == 2);
    assert(capped_fall_body(pc, climb_adrp(pc, 8), 0xb9400108) == 0);
    assert(capped_fall_body(pc, climb_adrp(pc, 8), 0xb9400908) == 0);
    assert(capped_fall_body(pc, climb_adrp(pc, 9), 0xb9400508) == 0);
    const u32 wrong_page = climb_adrp(pc, 8);
    frame_step_address = 0x10004000;
    assert(capped_fall_body(pc, wrong_page, 0xb9400508) == 0);
    frame_step_address = 0x10003000;
    assert(capped_fall_body(pc, climb_adrp(pc, 8), 0xb9400108) == 0);
    const u64 negative_pc = 0x10006000;
    assert(capped_fall_body(negative_pc, climb_adrp(negative_pc, 8), 0xb9400508) == 2);
    frame_step_address = 0;
    assert(capped_fall_body(pc, climb_adrp(pc, 8), 0xb9400508) == 0);
}
/* The ride controller's waits under the swap fix: a stub that counts +3C0 down per call
   and skips +2BC. A wait of 20 calls must last 20 thirtieths at every frame rate. */
static void swap_native(CPU* c, u64 entry) {
    (void)entry;
    const int wait = (int)gr32(c->x[0] + 0x3c0);
    if (wait > 0) gw32(c->x[0] + 0x3c0, (u32)(wait - 1));
}
static void test_ride_swap(void) {
    static const u64 steps[5] = {33333334, 16666667, 8333333, 5000000, 11111111};
    const u64 object = 1024;
    ride_swap_original = swap_native;
    for (unsigned k = 0; k < 5; k++) {
        memset(memory, 0, sizeof memory);
        ride_swap_fix = 1;
        ride_swap_fraction = 0;
        ride_swap_frame = ~0ull;
        step.step = steps[k];
        gw32(object + 0x3c0, 20);
        gw32(object + 0x2bc, 7);
        u64 elapsed = 0;
        while (gr32(object + 0x3c0)) {
            frame_step_frames++;
            CPU cpu = {.x = {[0] = object}};
            ride_swap_update(&cpu, 16);
            ride_swap_update(&cpu, 16);
            elapsed += steps[k];
            assert(elapsed < 2000000000);
        }
        /* Two calls in a frame share its decision, so a counting frame counts twice. */
        assert(elapsed >= 333333340 - steps[k] && elapsed <= 333333340 + steps[k]);
        /* The count the call never touched is as the game left it. */
        assert(gr32(object + 0x2bc) == 7);
    }
    ride_swap_fix = 0;
    ride_swap_original = NULL;
}
static void test_direct_move(void) {
    static const unsigned rates[] = {30, 60, 190, 300};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        float pos[3] = {0, 0, 0};
        const float ratio = 30.0f / rates[r];
        for (unsigned i = 0; i < rates[r] / 2; i++) {
            const float req[3] = {pos[0] + 10.0f / 30, 0, 0};
            float out[3];
            if (direct_move_scale(pos, req, ratio, out)) memcpy(pos, out, sizeof pos);
            else { assert(rates[r] == 30); memcpy(pos, req, sizeof pos); }
        }
        assert(fabsf(pos[0] - 5.0f) < 0.05f);
    }
    const float zero[3] = {0, 0, 0}, far_point[3] = {0, 0, 2.5f};
    float out[3];
    assert(!direct_move_scale(zero, far_point, 0.5f, out));
}

static unsigned field_waza_attacks;
static float field_waza_turned;
static void field_waza_mock(CPU* c, u64 entry) {
    (void)entry;
    const u64 o = c->x[0];
    const int wait = (int)gr32(o + 0x3b0) - 1;
    if (wait >= 0) {
        gw32(o + 0x3b0, (u32)wait);
        if (!wait) field_waza_attacks++, gw32(o + 0x218, 20);
    } else if (gr32(o + 0x218)) {
        field_waza_turned += guest_float(o + 0x3ac) / 20.0f;
        gw32(o + 0x218, gr32(o + 0x218) - 1);
        gw32(o + 0x370, gr32(o + 0x370) + 1);
    }
}
static void test_field_waza(void) {
    static const u64 steps[] = {33333334, 16666667, 8333333, 5555556, 11111111};
    const ArceusStep saved_step = step;
    const u64 saved_frames = frame_step_frames;
    field_waza_original = field_waza_mock;
    for (unsigned k = 0; k < sizeof steps / sizeof steps[0]; k++) {
        const u64 o = 1024;
        memset(memory, 0, sizeof memory);
        field_waza_fix = 1, field_waza_fraction = 0, field_waza_frame = ~0ull, frame_step_frames = 0;
        step.step = steps[k];
        gw32(o + 0x3b0, 15);
        guest_float_write(o + 0x3ac, 90.0f);
        field_waza_attacks = 0, field_waza_turned = 0;
        u64 elapsed = 0, attack_at = 0;
        while (gr32(o + 0x3b0) || gr32(o + 0x218)) {
            frame_step_frames++;
            CPU c = {.x = {[0] = o}};
            field_waza_update(&c, 16);
            elapsed += steps[k];
            if (field_waza_attacks && !attack_at) attack_at = elapsed;
            assert(elapsed < 3000000000ull);
        }
        assert(field_waza_attacks == 1 && attack_at >= 500000000 - steps[k] && attack_at <= 500000000 + 2 * steps[k]);
        assert(elapsed >= 1166666700 - 2 * steps[k] && elapsed <= 1166666700 + 2 * steps[k]);
        assert(fabsf(field_waza_turned - 90.0f) < 2.7f);
        assert(gr32(o + 0x370) == 20 && guest_float(o + 0x3ac) == 90.0f);
    }
    field_waza_fix = 0;
    step = saved_step;
    frame_step_frames = saved_frames;
}

static void waza_counter_mock(CPU* c, u64 entry) {
    (void)entry;
    const u64 o = c->x[0];
    if (gr32(o + 0x5e0) < 6) {
        gw32(o + 0x5e4, gr32(o + 0x5e4) + 1);
        if (gr32(o + 0x5e4) >= 9) gw32(o + 0x5e0, 6);
    } else if (gr32(o + 0x5e0) == 6) {
        gw32(o + 0x5e8, gr32(o + 0x5e8) + 1);
        if (gr32(o + 0x5e8) >= 15) gw32(o + 0x5e0, 8);
    }
    c->pc = c->x[30];
}
static void waza_terrain_mock(CPU* c, u64 entry) {
    (void)entry;
    if (gr32(c->x[0] + 0x5f0)) gw32(c->x[0] + 0x5f0, gr32(c->x[0] + 0x5f0) - 1);
    c->pc = c->x[30];
}
static void test_waza_object(void) {
    static const unsigned rates[] = {30, 60, 190, 300};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        memset(memory, 0, sizeof memory);
        waza_object_fix = 1, waza_object_original = waza_counter_mock;
        gw32(object + 0x5e0, 4);
        u64 rem = 0, elapsed = 0, acted = 0, ended = 0;
        while (!ended && elapsed < 3000000000ull) {
            const u64 ns = 1000000000ull / rates[r];
            elapsed += ns, rem += ns * 30;
            blink_ticks = (unsigned)(rem / 1000000000), rem %= 1000000000;
            CPU c = {.x = {[0] = object, [30] = 64}};
            waza_object_update(&c, entry_pc);
            if (!acted && gr32(object + 0x5e0) == 6) acted = elapsed;
            if (gr32(object + 0x5e0) == 8) ended = elapsed;
        }
        assert(fabs((double)acted / 1e9 - .3) < .04 && fabs((double)(ended - acted) / 1e9 - .5) < .04);
        memset(memory, 0, sizeof memory);
        waza_object_original = waza_terrain_mock;
        gw32(object + 0x5f0, 6), rem = elapsed = 0;
        while (gr32(object + 0x5f0)) {
            const u64 ns = 1000000000ull / rates[r];
            elapsed += ns, rem += ns * 30;
            blink_ticks = (unsigned)(rem / 1000000000), rem %= 1000000000;
            CPU c = {.x = {[0] = object, [30] = 64}};
            waza_object_update(&c, entry_pc);
            assert(gr32(object + 0x5f0) <= 6 && elapsed < 1000000000ull);
        }
        assert(fabs((double)elapsed / 1e9 - .2) < .04);
    }
    waza_object_fix = 0, blink_ticks = 0;
}

static void waza_shot_mock(CPU* c, u64 entry) {
    (void)entry;
    guest_float_write(c->x[0] + 0x6b0, guest_float(c->x[0] + 0x6b0) + .5f);
    gw32(c->x[0] + 0x6d4, gr32(c->x[0] + 0x6d4) + 1);
    c->pc = c->x[30];
}
static void waza_shot_transform_mock(CPU* c, u64 entry) {
    (void)entry;
    guest_float_write(3270 + 0xd0, guest_float(3270 + 0xd0) + .5f);
    gw32(c->x[0] + 0x6d4, gr32(c->x[0] + 0x6d4) + 1);
    c->pc = c->x[30];
}
static void test_waza_shot(void) {
    static const unsigned rates[] = {30, 60, 190, 300};
    for (unsigned fix = 1; fix <= 2; fix++)
        for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
            memset(memory, 0, sizeof memory), memset(shot_objects, 0, sizeof shot_objects);
            waza_shot_fix = (int)fix, blink_remainder = 0;
            const u64 transform = 3270;
            if (fix == 2) {
                const u64 handle = 3200, target = 3350;
                memcpy(GP(object + 0x1a8), &handle, 8), memcpy(GP(handle + 0x18), &target, 8);
                *GP(handle + 0xc) = 1, waza_shot_original = waza_shot_transform_mock;
                assert(trajectory_transform(object) == transform);
                guest_float_write(transform + 0xd0, 100.0f);
                guest_float_write(transform + 0xd4, 50.0f);
                guest_float_write(transform + 0xd8, -20.0f);
            } else waza_shot_original = waza_shot_mock;
            for (unsigned frame = 0; frame < rates[r]; frame++) {
                /* As adjust_duration feeds the cadence: a step near 1/30 s is the game's own. */
                blink_cadence_add(arceus_whole_step(1000000000ull / rates[r]));
                CPU c = {.x = {[0] = object, [30] = 64}};
                waza_shot_update(&c, entry_pc);
                assert(c.pc == 64);
                if (fix == 2 && !frame) {
                    assert(guest_float(transform + 0xd0) == 100.0f);
                    assert(guest_float(transform + 0xd4) == 50.0f);
                    assert(guest_float(transform + 0xd8) == -20.0f);
                }
                /* The drawn shot is the share of a whole step since the last run, between its
                   last two runs (the blink cadence keeps nanoseconds below a whole step). */
                const ShotObject* o = 0;
                for (unsigned k = 0; k < SHOT_OBJECTS; k++)
                    if (shot_objects[k].object == object) o = &shot_objects[k];
                if (fix == 2 && o && o->runs >= 2) {
                    const float share = (float)((double)blink_remainder / (double)ARCEUS_WHOLE_STEP_NS);
                    const float want = o->from[0] + (o->to[0] - o->from[0]) * share;
                    assert(fabsf(guest_float(transform + 0xd0) - want) < .001f);
                }
            }
            if (fix == 1) {
                assert(fabsf(guest_float(object + 0x6b0) - 15.0f) <= .5f);
                assert(gr32(object + 0x6d4) >= 29 && gr32(object + 0x6d4) <= 30);
            } else {
                assert(guest_float(transform + 0xd0) > 114.0f && guest_float(transform + 0xd0) < 115.1f);
                assert(guest_float(transform + 0xd4) == 50.0f && guest_float(transform + 0xd8) == -20.0f);
                assert(gr32(object + 0x6d4) >= 29 && gr32(object + 0x6d4) <= 30);
            }
        }
    waza_shot_fix = 0, blink_ticks = 0, blink_remainder = 0;
}

/* The even landing (value 2) on frame times taken from two played sessions: 60 FPS (report
   20261113-045030-report-acf2fb) and unlocked (20261113-045201-report-e5d35e). Every rendered
   frame must move the sequence by about its share, every whole frame must be landed on
   exactly once, and the frame must not drift from elapsed time. */
static void test_sequence_even(void) {
    static const u64 sixty[48] = {16364100, 16967500, 16664300, 16735200, 16707400, 16562200, 16710600, 16710400,
        16867000, 16200700, 16871100, 16605700, 16330200, 17052800, 16489500, 16939200, 16112400, 16784000, 17036000,
        16528400, 16508200, 16670500, 16892700, 16645000, 16224900, 17021700, 16519600, 17740700, 15698100, 16703500,
        16892800, 16197600, 17490400, 16167900, 16344900, 16726800, 17002600, 16793000, 16395400, 16658500, 16603700,
        17210200, 16226200, 17037700, 16313000, 16456900, 17122100, 16258700};
    static const u64 unlocked[48] = {4960100, 5789000, 5375700, 6016100, 5508600, 5525300, 6322800, 5306600, 5121400,
        5043200, 5678600, 5166500, 5051000, 5798600, 5410700, 5351200, 4817000, 5503700, 5101500, 5666500, 5717000,
        5406700, 5092000, 5788800, 5129000, 5324100, 5818500, 4590600, 5769400, 5514300, 5407300, 5278300, 5032800,
        6133300, 6145900, 4964900, 4672100, 6117600, 5187000, 5329200, 5365900, 5247100, 5641700, 5877800, 5750900,
        5160300, 5093300, 5754100};
    /* Frame steps of the session in which the first even landing lost its send-outs. */
    static const u64 spu[48] = {3345200, 3203100, 9137600, 5501200, 5695200, 6108100, 6939500, 5918200, 5781600,
        6107900, 6859100, 6766400, 5807100, 4755800, 4936500, 5262900, 4755600, 5163200, 5906700, 5942400, 6067000,
        6440100, 8208800, 6142000, 6122600, 6295300, 6152500, 6620000, 6051100, 6590000, 6093800, 5837600, 6110000,
        5912200, 5845500, 5525700, 5938500, 6426300, 5606400, 5944700, 6263300, 8191400, 6161800, 5608700, 5519500,
        6544500, 7586900, 5517500};
    /* Frame steps of a session at the game's own 30 FPS. A step is a hair under or over a frame. */
    static const u64 thirty[48] = {33288000, 33208000, 33210700, 33187600, 33262200, 33479000, 33373300, 33396900,
        33703200, 33519700, 32979300, 33523500, 33086100, 33300700, 33718300, 32919100, 33582000, 33252300, 33631600,
        33158800, 33184800, 33292200, 33374500, 33210600, 33419700, 33337900, 33506800, 33415800, 33479300, 32795400,
        33475100, 33413900, 33238700, 33499200, 32911800, 33776300, 33438700, 33505900, 33407300, 33310200, 33084900,
        33273900, 33588000, 33260900, 33394800, 33114400, 33209700, 33257500};
    static const float speeds[] = {1, .2f, 2};
    for (unsigned even = 0; even < 2; even++)
        for (unsigned set = 0; set < 5; set++)
            for (unsigned s = 0; s < sizeof speeds / sizeof speeds[0]; s++) {
                const float speed = speeds[s];
                reset(8, speed, 1);
                battle_sequence_even = (int)even;
                boundaries = entered_whole = 0;
                u64 elapsed = 0;
                unsigned renders = 0, held = 0, jumped = 0, low = 0, high = 0, ended_whole = 0;
                float last_whole = 8;
                for (unsigned i = 0; i < 960; i++) {
                    /* Set 2 is the constant 60 FPS step. */
                    step.step = set == 0 ? sixty[i % 48] : set == 1 ? unlocked[i % 48] : set == 2 ? 16666667 :
                                set == 3 ? spu[i % 48] : thirty[i % 48];
                    elapsed += step.step;
                    renders++;
                    const float before = guest_float(object + 0xe8);
                    update();
                    const float frame = guest_float(object + 0xe8);
                    const float share = (frame - before) / (speed * (float)step.step / 33333333.0f);
                    /* The first frames start from a budget of nothing. */
                    if (i > 2 && share < 0.7f) held++;
                    if (i > 2 && share > 1.3f) jumped++;
                    if (i > 2 && share < 0.45f) low++;
                    if (i > 2 && share > 1.55f) high++;
                    /* The mock asserts that no whole frame is crossed, so this counts skips and repeats. */
                    assert(boundaries == (unsigned)floorf(frame) - 8);
                    /* Both sides of the game's rule. Every whole frame the sequence has left was
                       entered by an update, once (the one it stands on is entered by the next). And
                       every whole frame was the value a rendered frame ended on, once, which is what
                       the commands' step slot sees. A step of more than a frame may pass one. */
                    assert(entered_whole == (unsigned)floorf(frame) - 8 + (frame != floorf(frame)));
                    if (frame == floorf(frame) && frame != last_whole) ended_whole++, last_whole = frame;
                    if (speed <= 1) assert(ended_whole == (unsigned)floorf(frame) - 8);
                    const double expected = 8 + speed * (double)elapsed / 33333333.0;
                    /* Nothing is gained. The old landing at 30 FPS loses a little where its budget
                       is cut to one frame. The budget stays inside one step of the game beyond the
                       frame's own advance, and the even landing owes at most half an advance. */
                    const double have = frame + sequence_slots[0].pending;
                    const float advance = speed * (float)step.step / 33333333.0f;
                    assert(have < expected + .004 && have > expected - (set == 4 && !even ? .02 : .004));
                    assert(sequence_slots[0].pending < (speed > 1 ? speed : 1) + advance + .001f &&
                           sequence_slots[0].pending > -SEQUENCE_SNAP_SHARE * advance - .001f);
                }
                /* One update per rendered frame at these speeds: no rest gets a second one. */
                if (speed <= 1) assert(calls <= renders);
                if (even) {
                    /* At 60 FPS, where the landing keeps the frame on wholes and halves, rendered
                       frames move by 0.7 to 1.3 of their share (one of 960 real ones is at 1.30, after
                       a 17.7 ms frame). With uneven unlocked frame times a few frames fall outside half
                       to one and a half, when a long frame follows a short one at a landing. */
                    if (set == 0 || set == 2) assert(held + jumped <= 1 && !low && !high);
                    else assert((low + high) * 40 < renders && (held + jumped) * 3 < renders);
                } else if (set == 0 && speed == 1)
                    /* The old rule on the same 60 FPS times: about a quarter held and a quarter jumped. */
                    assert(held > renders / 5 && jumped > renders / 5);
                else if (set == 1 && speed == 1)
                    /* And on unlocked times it leaves more frames far off their share than the even one. */
                    assert((low + high) * 40 > renders);
            }
    /* An end check on frame 12 that holds the sequence for five updates. It has to be made, on
       every one of those updates, with the old landing and with the even one, at 60 FPS and
       unlocked frame times. A landing that never enters frame 12 would make none. */
    for (unsigned even = 0; even < 2; even++)
        for (unsigned set = 0; set < 3; set++) {
            reset(8, 1, 1);
            battle_sequence_even = (int)even;
            hold_at = 12;
            hold_left = 5;
            hold_checks = 0;
            for (unsigned i = 0; i < 300; i++) {
                step.step = set == 0 ? sixty[i % 48] : set == 1 ? unlocked[i % 48] : spu[i % 48];
                update();
            }
            assert(hold_checks == 6 && !hold_left && guest_float(object + 0xe8) > 13);
        }
    hold_at = 0;
    battle_sequence_even = 0;
}
static void put64(u64 address, u64 value) { memcpy(GP(address), &value, 8); }
static void put32(u64 address, u32 value) { memcpy(GP(address), &value, 4); }
/* The script tick through its real bridge wrapper, with a stub script that records what it
   sees: the frame step cell and the jump trigger (byte 2 of the input fields). */
static unsigned tick_runs[2], tick_triggers, tick_class, tick_throw_events;
static float tick_cell;
static void throw_event_stub(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = *GP(c->x[0] + 0x9b8);
}
static void tick_script(CPU* c, u64 entry) {
    /* A player script polls the throw release event of its animation controller at 256. */
    if (tick_class >= SCRIPT_TICK_PLAYER && entry == 16 && script_query_states[0].original) {
        CPU poll = {.x = {[0] = 256}};
        script_query_check(&poll, main_base + 0x1955fe0);
        tick_throw_events += poll.x[0] & 1;
    }
    const u64 fields = c->x[2] + script_tick_classes[tick_class].input;
    tick_runs[entry == 32]++;
    tick_cell = guest_float(frame_step_address);
    if (entry == 16 && *GP(fields + 2)) tick_triggers++;
    c->x[0] = 7;
    c->pc = c->x[30];
}
static void tick_reset(void) {
    memset(memory, 0, sizeof memory);
    memset(&script_tick, 0, sizeof script_tick);
    memset(script_tick_inputs, 0, sizeof script_tick_inputs);
    memset(script_windows, 0, sizeof script_windows);
    script_tick_mask = ~0ul; /* Every class ticked, unless a test says otherwise. */
    script_tick_originals[tick_class][0] = script_tick_originals[tick_class][1] = tick_script;
    tick_runs[0] = tick_runs[1] = tick_triggers = tick_throw_events = 0;
    for (unsigned k = 0; k < SCRIPT_QUERIES; k++) {
        const GuestFn original = script_query_states[k].original;
        script_query_states[k] = (ScriptQueryState){.original = original};
    }
    script_query_run = script_query_last = script_query_state = script_query_dropped = 0;
    frame_step_address = 3900;
    frame_step_frames = 0;
    put32(128 + 0x6c, 3);
}
/* One engine frame: the clock write, then PreUpdate and PostUpdate of the state at 128. */
static unsigned tick_frame(u64 ns, int trigger) {
    const u64 context = 512, fields = context + script_tick_classes[tick_class].input;
    unsigned ran = 0;
    step.step = ns;
    frame_step_write(ns);
    *GP(fields + 2) = (u8)trigger;
    const u32 before = gr32(frame_step_address), derived = gr32(frame_step_address + 8);
    /* The derived words follow the step: -10/step and 1/(30 x step). */
    const float seconds = guest_float(frame_step_address);
    assert(guest_float(frame_step_address + 8) == (seconds == NOMINAL_STEP_SECONDS ? -300.0f : -10.0f / seconds) &&
           guest_float(frame_step_address + 20) == 1.0f / (30.0f * seconds));
    /* At the game's own step they are the game's constants, so its division and product give
       the game's values: -300 where the game divides by -300, and 1. */
    frame_step_cell_write(NOMINAL_STEP_SECONDS);
    assert(guest_float(frame_step_address + 8) == -300.0f && guest_float(frame_step_address + 20) == 1.0f &&
           gr32(frame_step_address + 16) == 0x40490fdb);
    frame_step_cell_write(seconds);
    for (unsigned post = 0; post < 2; post++) {
        CPU cpu = {.x = {[0] = 128, [2] = context, [30] = 64}, .pc = 16};
        const unsigned runs = tick_runs[post];
        script_tick_bridge(&cpu, post ? 32 : 16, tick_class, post);
        assert(cpu.pc == 64 && cpu.x[0] == (tick_runs[post] > runs ? 7u : 3u));
        ran += tick_runs[post] - runs;
        /* A ticked script sees 1/30 s, and the cell and the field are back afterwards. */
        if (tick_runs[post] > runs && (script_tick_mask >> tick_class & 1)) assert(tick_cell == NOMINAL_STEP_SECONDS);
        assert(gr32(frame_step_address) == before && *GP(fields + 2) == (u8)trigger);
        assert(gr32(frame_step_address + 8) == derived);
    }
    /* PreUpdate and PostUpdate of a frame share the decision. */
    assert(ran == 0 || ran == 2);
    return ran / 2;
}
static void test_script_tick(void) {
    static const unsigned rates[] = {30, 45, 60, 72, 120, 144, 240};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        tick_reset();
        unsigned ticks = 0;
        /* 30 FPS is the game's own 33333334 ns frame. */
        const u64 ns = rates[r] == 30 ? 33333334 : 1000000000 / rates[r];
        for (unsigned k = 0; k < 10 * rates[r]; k++) ticks += tick_frame(ns, 0);
        assert(ticks >= 299 && ticks <= 300);
        /* At 30 FPS every frame runs. */
        if (rates[r] == 30) assert(ticks == 300 && script_tick.budget == 0);
    }
    /* Uneven steps: 12 and 20 ms, then random 4 to 40 ms. Steps above 1/30 s count as 1/30. */
    tick_reset();
    unsigned ticks = 0;
    for (unsigned k = 0; k < 625; k++) ticks += tick_frame(k % 2 ? 20000000 : 12000000, 0);
    assert(ticks >= 299 && ticks <= 300);
    tick_reset();
    ticks = 0;
    u64 seed = 12345, due = 0;
    for (unsigned k = 0; k < 2000; k++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        const u64 ns = 4000000 + (seed >> 33) % 36000001;
        due += ns < 33333334 ? ns : 33333334;
        ticks += tick_frame(ns, 0);
    }
    assert(ticks == due / 33333334 || ticks == due / 33333334 + 1);
    /* A slow frame gives one tick and carries nothing over. */
    tick_reset();
    assert(tick_frame(100000000, 0) == 1 && tick_frame(100000000, 0) == 1 && script_tick.budget == 0);
    /* A trigger on any one frame of a 120 FPS run reaches exactly one script run. */
    for (unsigned at = 0; at < 8; at++) {
        tick_reset();
        for (unsigned k = 0; k < 40; k++) tick_frame(8333333, k == at);
        assert(tick_triggers == 1);
    }
    /* A trigger held over three frames is still one press for the script. */
    tick_reset();
    for (unsigned k = 0; k < 40; k++) tick_frame(8333333, k >= 1 && k <= 3);
    assert(tick_triggers == 1);
    /* A player class has its input fields at context +14C. A skipped frame returns the
       current state index 3 (checked in tick_frame), so the state machine sees no change. */
    tick_class = 7;
    assert(script_tick_classes[tick_class].input == 0x14c);
    for (unsigned at = 0; at < 4; at++) {
        tick_reset();
        unsigned ticks = 0;
        for (unsigned k = 0; k < 40; k++) ticks += tick_frame(8333333, k == at);
        assert(ticks == 10 && tick_triggers == 1);
    }
    /* The throw release event set on any single frame reaches exactly one run of the player
       script, also on a frame without a run. Never set, it is never seen. */
    script_query_states[0].original = throw_event_stub;
    for (unsigned at = 8; at < 13; at++) {
        tick_reset();
        for (unsigned k = 0; k < 40; k++) {
            *GP(256 + 0x9b8) = k == at;
            tick_frame(8333333, 0);
        }
        assert(tick_throw_events == 1);
    }
    tick_reset();
    for (unsigned k = 0; k < 40; k++) tick_frame(8333333, 0);
    assert(!tick_throw_events);
    /* A query the script stopped asking is not asked for it on later frames. */
    tick_reset();
    for (unsigned k = 0; k < 8; k++) tick_frame(8333333, 0);
    script_query_states[0].asked = 1;
    *GP(256 + 0x9b8) = 1;
    tick_frame(8333333, 0);
    assert(!script_query_states[0].held);
    script_query_states[0].original = NULL;
    /* The buddy context has no input struct: its bytes are never written. */
    tick_class = 6;
    assert(!script_tick_classes[tick_class].input);
    tick_reset();
    ticks = 0;
    for (unsigned k = 0; k < 40; k++) ticks += tick_frame(8333333, k == 1);
    assert(ticks == 10 && tick_triggers == 0);
    tick_class = 0;
    script_tick_mask = 0;
    frame_step_address = 0;
    frame_step_frames = 0;
}
/* The rotation blend on the ticked class 2, with a stub script that turns by 0.01 rad per
   run through the real setter wrapper and checks that it reads its own last rotation. */
static const u64 blend_ride = 1024, blend_transform = 1600, blend_argument = 2048;
static float blend_target;
static unsigned blend_runs;
static float blend_yaw(u64 address) { return 2.0f * atan2f(guest_float(address + 4), guest_float(address + 12)); }
static void blend_native(CPU* c, u64 entry) {
    (void)entry;
    memcpy(GP(blend_transform + 0xc0), GP(c->x[1]), 16);
    *GP(blend_transform + 0xe0) = 1;
}
static void blend_script(CPU* c, u64 entry) {
    /* Both bridges see the script's own rotation, never a blended one. */
    if (blend_runs) assert(fabsf(blend_yaw(blend_transform + 0xc0) - blend_target) < 1e-6f);
    if (entry == 16) {
        blend_target += 0.01f;
        guest_float_write(blend_argument + 4, sinf(blend_target / 2));
        guest_float_write(blend_argument + 12, cosf(blend_target / 2));
        CPU set = {.x = {[0] = blend_ride, [1] = blend_argument, [30] = 64}, .pc = 48};
        rotation_blend_set(&set, 48);
        blend_runs++;
    }
    c->x[0] = 7;
    c->pc = c->x[30];
}
static void blend_reset(void) {
    memset(memory, 0, sizeof memory);
    memset(&script_tick, 0, sizeof script_tick);
    memset(script_tick_inputs, 0, sizeof script_tick_inputs);
    memset(script_windows, 0, sizeof script_windows);
    script_tick_mask = ~0ul; /* Every class ticked, unless a test says otherwise. */
    memset(rotation_blends, 0, sizeof rotation_blends);
    script_tick_originals[2][0] = script_tick_originals[2][1] = blend_script;
    rotation_blend_original = blend_native;
    rotation_blend_writes = rotation_blend_foreign = rotation_blend_dropped = 0;
    rotation_blend_frame = rotation_blend_state = 0;
    blend_target = 0;
    blend_runs = 0;
    frame_step_address = 3900;
    frame_step_frames = 0;
    put64(blend_ride + 0x1a8, 1500);
    put32(1500 + 0xc, 1);
    put64(1500 + 0x18, blend_transform + 0x50);
    guest_float_write(blend_transform + 0xcc, 1);
}
/* One engine frame. Returns the yaw shown at its end. */
static float blend_frame(u64 ns) {
    step.step = ns;
    frame_step_write(ns);
    for (unsigned post = 0; post < 2; post++) {
        CPU cpu = {.x = {[0] = 128, [2] = 512, [30] = 64}, .pc = 16};
        script_tick_bridge(&cpu, post ? 32 : 16, 2, post);
    }
    return blend_yaw(blend_transform + 0xc0);
}
/* At 30 FPS with real jitter every frame runs the script exactly once: no skip, no double. */
static void test_script_tick_30(void) {
    static const u64 jitter[8] = {33000000, 33600000, 33200000, 33500000, 33100000, 33600000, 33050000, 33400000};
    ScriptTick tick;
    for (unsigned set = 0; set < 3; set++) {
        memset(&tick, 0, sizeof tick);
        for (unsigned k = 0; k < 9000; k++) {
            /* Uneven around 1/30 s, always a little short, and always a little long. */
            const u64 ns = set == 0 ? jitter[k % 8] : set == 1 ? 33000000 : 33600000;
            assert(script_tick_decide(&tick, k + 1, ns) == 1);
            /* The same frame asked again gives the same answer and no second tick. */
            assert(script_tick_decide(&tick, k + 1, ns) == 1);
        }
        assert(tick.ticks == 9000 && tick.frames == 9000);
    }
}
static void test_rotation_blend(void) {
    /* At 30 FPS nothing is written: the transform is the script's rotation on every frame. */
    blend_reset();
    for (unsigned k = 0; k < 30; k++) {
        const float shown = blend_frame(33333334);
        assert(shown == blend_yaw(blend_argument) && !rotation_blend_writes);
    }
    static const u64 steps[][2] = {{16666667, 16666667}, {8333333, 8333333}, {6060606, 6060606}, {5000000, 12000000}};
    for (unsigned r = 0; r < 4; r++) {
        blend_reset();
        float before = 0, reached = 0;
        unsigned runs = 0, moved = 0, frames = 0;
        for (unsigned k = 0; k < 600; k++) {
            const float target_before = blend_target;
            const float shown = blend_frame(steps[r][k % 2]);
            if (blend_runs < 3) {
                before = shown;
                runs = blend_runs;
                continue;
            }
            /* It never goes back, never passes the script's rotation, and moves on every frame. */
            assert(shown >= before - 1e-6f && shown <= blend_target + 1e-6f);
            moved += shown > before + 1e-6f;
            frames++;
            /* When a new rotation comes, the old one has just been reached. */
            if (blend_runs != runs) {
                reached = target_before - shown;
                assert(reached < 1e-4f && reached > -1e-6f);
                runs = blend_runs;
            }
            before = shown;
        }
        assert(moved >= frames - 2 && rotation_blend_writes && !rotation_blend_foreign);
        /* The turn rate is the script's: 0.01 rad per 1/30 s whatever the frame rate. */
        const float seconds = (float)(300 * (steps[r][0] + steps[r][1])) / 1e9f;
        assert(fabsf(blend_target - 0.3f * seconds) < 0.011f);
    }
    /* Real frame times of a 60 FPS session, a step just under 1/60 s, 72 and 90 FPS, and real
       unlocked frame times (about 185 FPS, uneven). The tick budget used to miss a tick by a
       hair on some frames at 60 FPS. The drawn turn must move on every frame by 0.6 to 1.5 of
       the frame's share, never back and never twice the same. With the uneven unlocked times
       the next run cannot be told to the frame, so a few frames move less or more. */
    static const u64 uneven[24] = {4960100, 5789000, 5375700, 6016100, 5508600, 5525300, 6322800, 5306600, 5121400,
        5043200, 5678600, 5166500, 5051000, 5798600, 5410700, 5351200, 4817000, 5503700, 5101500, 5666500, 5717000,
        5406700, 5092000, 5788800};
    static const u64 sixty[24] = {16364100, 16967500, 16664300, 16735200, 16707400, 16562200, 16710600, 16710400,
        16867000, 16200700, 16871100, 16605700, 16330200, 17052800, 16489500, 16939200, 16112400, 16784000, 17036000,
        16528400, 16508200, 16670500, 16892700, 16645000};
    for (unsigned mode = 0; mode < 5; mode++) {
        blend_reset();
        float before = 0;
        unsigned outside = 0, frames = 0;
        for (unsigned k = 0; k < 960; k++) {
            const u64 ns = mode == 0 ? sixty[k % 24] : mode == 1 ? 16500000 : mode == 2 ? 13888889 :
                           mode == 3 ? 11111111 : uneven[k % 24];
            const float shown = blend_frame(ns);
            if (blend_runs >= 3) {
                const float share = (shown - before) / (0.01f * (float)ns / 33333333.0f);
                assert(share > 0.1f && share < 1.9f);
                outside += !(share > 0.6f && share < 1.5f);
                frames++;
            }
            before = shown;
        }
        assert(!rotation_blend_foreign && (mode == 4 ? outside * 12 < frames : !outside));
    }
    /* 30 FPS with jitter: every frame is a run and draws the rotation that run set, not the
       one before it. */
    {
        static const u64 jitter[8] = {33000000, 33600000, 33200000, 33500000, 33100000, 33600000, 33050000, 33400000};
        blend_reset();
        float before = 0;
        for (unsigned k = 0; k < 240; k++) {
            const float shown = blend_frame(jitter[k % 8]);
            if (k) assert(fabsf(shown - before - 0.01f) < 1e-5f);
            before = shown;
        }
    }
    /* Another writer ends the blend: the host does not write that transform again. */
    blend_reset();
    for (unsigned k = 0; k < 20; k++) blend_frame(8333333);
    guest_float_write(blend_transform + 0xc4, 0.5f);
    script_tick_originals[2][0] = script_tick_originals[2][1] = tick_script;
    tick_class = 2;
    const u64 writes = rotation_blend_writes;
    for (unsigned k = 0; k < 8; k++) blend_frame(8333333);
    assert(rotation_blend_foreign == 1 && rotation_blend_writes == writes && guest_float(blend_transform + 0xc4) == 0.5f);
    tick_class = 0;
    rotation_blend_original = NULL;
    frame_step_address = 0;
    frame_step_frames = 0;
}
/* The throw window: the player classes run on every frame, and 30 times a second only
   between the script's OnStartThrow and its ThrowObject or OnEndThrowStance. */
static void window_native(CPU* c, u64 entry) { (void)c; (void)entry; }
static unsigned window_run(unsigned frames) {
    unsigned ran = 0;
    for (unsigned k = 0; k < frames; k++) ran += tick_frame(8333333, 0);
    return ran;
}
static void test_script_window(void) {
    tick_class = 7;
    tick_reset();
    script_tick_mask = 0;
    for (unsigned k = 0; k < SCRIPT_WINDOW_EVENTS; k++) script_window_originals[k] = window_native;
    CPU cpu = {.x = {[30] = 64}};
    /* Outside a window every frame runs, at any rate, and the frame step cell is not touched. */
    assert(window_run(40) == 40 && tick_cell != NOMINAL_STEP_SECONDS);
    /* OnStartThrow opens it. The frame of the call still runs whole. Then 1 run in 4 frames at 120 FPS. */
    host_ns += 1;
    script_window_event(&cpu, main_base + 0x1a91624);
    assert(script_windows[0].open && script_windows[0].opens == 1);
    assert(window_run(40) == 10 && tick_cell == NOMINAL_STEP_SECONDS);
    /* 19 script updates now take 76 frames, 0.63 s, as at 30 FPS. */
    assert(window_run(36) == 9);
    /* ThrowObject closes it: every frame runs again. */
    script_window_event(&cpu, main_base + 0x1a8eba4);
    assert(!script_windows[0].open && window_run(20) == 20);
    /* OnEndThrowStance closes it too. */
    script_window_event(&cpu, main_base + 0x1a91624);
    assert(window_run(8) == 2);
    script_window_event(&cpu, main_base + 0x1a91370);
    assert(window_run(8) == 8);
    /* A window nobody closes ends after 1 s. */
    script_window_event(&cpu, main_base + 0x1a91624);
    for (unsigned k = 0; k < 130; k++) {
        host_ns += 8333333;
        tick_frame(8333333, 0);
    }
    assert(!script_windows[0].open && script_windows[0].expired == 1 && window_run(8) == 8);
    /* A class the window does not name is never ticked by it. */
    tick_class = 0;
    tick_reset();
    script_tick_mask = 0;
    script_window_event(&cpu, main_base + 0x1a91624);
    assert(window_run(16) == 16);
    script_tick_mask = 0;
    memset(script_windows, 0, sizeof script_windows);
    frame_step_address = 0;
    frame_step_frames = 0;
}
/* Fail safe after the crash of run timing-20261112-191158-069: the host never asks a kept
   query for an object that may be gone, a window ends when its state stops being bridged,
   and a blend is let go after a frame without its bridge. */
static unsigned safe_asks;
static void safe_query_stub(CPU* c, u64 entry) {
    (void)entry;
    safe_asks++;
    c->x[0] = *GP(c->x[0] + 0x9b8);
}
static void test_fail_safe(void) {
    tick_class = 9;
    tick_reset();
    put32(160 + 0x6c, 3);
    for (unsigned k = 0; k < SCRIPT_QUERIES; k++) script_query_states[k].original = safe_query_stub;
    put64(256, 0xc1551a0);
    /* The script asks at each run, and the host asks again on the three frames after the first. */
    safe_asks = 0;
    assert(window_run(8) == 2 && safe_asks == 2 + 3);
    /* The object is released and its memory reused: the first word changes. No more host asks. */
    put64(256, 0xbc59dcc7b3f81af0ull);
    safe_asks = 0;
    assert(tick_frame(8333333, 0) == 0 && safe_asks == 0 && script_query_dropped == 1);
    assert(window_run(3) == 1 && safe_asks == 1);
    /* Alive again and asked by a run. Then two frames pass without a bridge call: forgotten. */
    assert(window_run(4) == 1);
    frame_step_frames += 2;
    safe_asks = 0;
    CPU skipped = {.x = {[0] = 128, [2] = 512, [30] = 64}};
    script_query_frame(&skipped, frame_step_frames, 0, 0);
    assert(safe_asks == 0 && !script_query_states[0].object);
    /* Another state object on the next frame is not asked for either. */
    assert(window_run(4) == 1 && script_query_states[0].object == 256);
    CPU other = {.x = {[0] = 160, [2] = 512, [30] = 64}};
    safe_asks = 0;
    script_query_frame(&other, frame_step_frames + 1, 0, 0);
    assert(safe_asks == 0 && !script_query_states[0].object);
    for (unsigned k = 0; k < SCRIPT_QUERIES; k++) script_query_states[k].original = NULL;
    /* A window opened by the script of state 128. It ticks while that state is bridged. The
       line per closed window needs the research switch. */
    research_observing = 1;
    tick_class = 7;
    tick_reset();
    script_tick_mask = 0;
    put32(160 + 0x6c, 3);
    for (unsigned k = 0; k < SCRIPT_WINDOW_EVENTS; k++) script_window_originals[k] = window_native;
    CPU cpu = {.x = {[30] = 64}};
    assert(window_run(4) == 4);
    script_class_running = 8;
    script_class_object = 128;
    script_window_event(&cpu, main_base + 0x1a91624);
    script_class_running = 0;
    script_class_object = 0;
    assert(window_run(16) == 4 && script_windows[0].open);
    /* A hit changes the state: another state object is bridged. Its second frame closes the
       window, and from then on the script runs on every frame again. */
    unsigned ran = 0;
    for (unsigned k = 0; k < 6; k++) {
        host_ns += 8333333;
        step.step = 8333333;
        frame_step_write(8333333);
        for (unsigned post = 0; post < 2; post++) {
            CPU hit = {.x = {[0] = 160, [2] = 512, [30] = 64}, .pc = 16};
            const unsigned runs = tick_runs[post];
            script_tick_bridge(&hit, post ? 32 : 16, tick_class, post);
            if (!post) ran += tick_runs[0] - runs;
        }
    }
    assert(!script_windows[0].open && script_windows[0].interrupted == 1 && ran >= 5);
    /* Each closed window wrote its line. */
    assert(script_window_lines >= 1);
    research_observing = 0;
    memset(script_windows, 0, sizeof script_windows);
    script_tick_mask = 0;
    /* A blend whose bridge missed a frame is let go without a write. */
    blend_reset();
    for (unsigned k = 0; k < 8; k++) blend_frame(8333333);
    assert(rotation_blends[0].active);
    const u64 writes = rotation_blend_writes;
    frame_step_frames += 3;
    blend_frame(8333333);
    assert(rotation_blend_dropped == 1 && rotation_blend_writes == writes);
    tick_class = 0;
    rotation_blend_original = NULL;
    frame_step_address = 0;
    frame_step_frames = 0;
}
/* A fade-in's last call frees the object's slot, so fades of more than FADE_SLOTS objects
   in a session all keep a slot instead of running unfixed. */
/* The game's own pace. A frame goes through the real clock entry with a measured step near
   1/30 s, then one wrapper runs. Its guest memory and return registers must equal, call for
   call, those of the game's function called with no wrapper. */
static u64 whole_hash;
static void whole_mix(u64 value) { whole_hash = (whole_hash ^ value) * 0x100000001b3ull; }
static void whole_cpu(const CPU* c) { whole_mix(c->x[0]), whole_mix(c->pc), whole_mix(c->v[0].s[0]); }
static void whole_frame(u64 elapsed) {
    static const u64 callers[4] = {0x278150c, 0x2781540, 0x2781574, 0x27815a8};
    host_ns += elapsed;
    for (unsigned channel = 0; channel < 4; channel++) {
        CPU c = {.x = {[0] = 4000, [1] = channel, [2] = 33333334, [30] = main_base + callers[channel]}};
        adjust_duration(&c);
        assert(c.x[2] == ARCEUS_WHOLE_STEP_NS);
    }
    assert(step.step == ARCEUS_WHOLE_STEP_NS && blink_ticks == 1 && blink_remainder == 0);
    assert(guest_float(frame_step_address) == NOMINAL_STEP_SECONDS && frame_step_ratio() == 1.0f);
}
static float whole_speed;
static unsigned whole_count;
static void whole_sequence_stock(CPU* c, u64 entry) {
    (void)entry;
    guest_float_write(c->x[0] + 0xe8, guest_float(c->x[0] + 0xe8) + guest_float(c->x[0] + 0xf0));
    c->x[0] = ++whole_count;
    c->pc = c->x[30];
}
static void whole_sequence_setup(void) {
    reset(0, whole_speed, 0);
    guest_float_write(object + 0xec, 100000);
    battle_sequence_original = whole_sequence_stock;
    whole_count = 0;
}
static void whole_sequence(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[0] = object, [30] = 32}, .pc = 16};
    if (stock) whole_sequence_stock(&c, 16);
    else battle_sequence_update(&c, 16);
    whole_cpu(&c);
}
static void whole_countdown_stock(CPU* c, u64 entry) {
    (void)entry;
    const u64 director = gr64(c->x[1]);
    if (gr32(director + 0x1dc)) gw32(director + 0x1dc, gr32(director + 0x1dc) - 1);
    else gw32(director + 0xf0, gr32(director + 0xf0) == 3 ? 5 : 3), gw32(director + 0x1dc, 12);
    c->pc = c->x[30];
}
static void whole_capture_setup(void) {
    memset(memory, 0, sizeof memory);
    memset(capture_slots, 0, sizeof capture_slots);
    capture_next_slot = 0;
    capture_fix_active = 1;
    sequence_clock_original[1] = whole_countdown_stock;
    gw64(3000, 1024);
    gw32(1024 + 0xf0, 3);
    gw32(1024 + 0x1dc, 12);
}
static void whole_capture(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[1] = 3000, [30] = 32}, .pc = 16};
    if (stock) whole_countdown_stock(&c, 16);
    else capture_sequence_probe(&c, 16);
    whole_cpu(&c);
}
static void whole_cursor_stock(CPU* c, u64 entry) {
    (void)entry;
    const MapCursorSite* site = &map_cursor_sites[0];
    guest_float_write(c->x[0] + 0x40, guest_float(c->x[0] + 0x40) + guest_float(c->x[0] + site->speed));
    for (unsigned k = 0; k < 2; k++)
        if (gr32(c->x[0] + site->held + 4 * k) < 9)
            gw32(c->x[0] + site->held + 4 * k, gr32(c->x[0] + site->held + 4 * k) + 1);
    c->pc = c->x[30];
}
static void whole_cursor_setup(void) {
    memset(memory, 0, sizeof memory);
    memset(map_cursor_object, 0, sizeof map_cursor_object);
    memset(map_cursor_ns, 0, sizeof map_cursor_ns);
    map_cursor_original[0] = whole_cursor_stock;
    guest_float_write(object + map_cursor_sites[0].speed, 0.37f);
}
static void whole_cursor(unsigned i, int stock) {
    /* The stick is let go now and then, which starts the held counts again. */
    if (i % 23 == 0) gw32(object + map_cursor_sites[0].held, 0), gw32(object + map_cursor_sites[0].held + 4, 0);
    CPU c = {.x = {[0] = object, [30] = 32}, .pc = 16};
    if (stock) whole_cursor_stock(&c, 16);
    else field_map_cursor_update(&c, 16);
    whole_cpu(&c);
}
static unsigned whole_every;
static void whole_ai_setup(void) {
    memset(memory, 0, sizeof memory);
    memset(ai_tick_slots, 0, sizeof ai_tick_slots);
    memset(ai_turn_nodes, 0, sizeof ai_turn_nodes);
    ai_pass_total = ai_tick_total = 0;
    ai_tick_original = ai_tick_mock;
    ai_turn_original[0] = ai_turn_original[1] = ai_turn_mock;
    main_base = 3072 - 0x40e7c04;
    gw32(3072, 4);
    gw32(512 + 0x130, 30);
    gw64(512 + 0x58, 1024);
    gw64(1024 + 0x90, object);
    memory[object + 0x81] = whole_every == 1;
}
/* One scheduler pass and the actor's read of the context, every pass or every 4th. */
static void whole_ai(unsigned i, int stock) {
    if (i % whole_every == 0) gw32(object + 0x88, 0);
    CPU c = {.x = {[0] = object, [30] = 32}};
    if (stock) c.v[9].s[1] = 1, ai_tick_mock(&c, 16), c.v[9].s[1] = 0;
    else ai_tick_update(&c, 16);
    whole_cpu(&c);
    if (i % whole_every != whole_every - 1) return;
    CPU t = {.x = {[0] = 512, [8] = 2048, [30] = 32}};
    gw32(2048, 99);
    if (stock) ai_turn_mock(&t, 16);
    else ai_turn_update(&t, 16, 0);
    whole_cpu(&t);
    /* A finished turn starts again. */
    if (gr32(2048) == 1) gw32(512 + 0x134, 0);
}
static void whole_turn_setup(void) {
    memset(memory, 0, sizeof memory);
    turn_original = turn_mock;
    turn_angle = 0;
    guest_float_write(object + 0x268, .2f);
}
static void whole_turn(unsigned i, int stock) {
    CPU c = {.x = {[0] = object, [30] = 32}};
    c.v[0].f[0] = i % 3 ? 1.0f / 30 : .5f;
    if (stock) turn_mock(&c, 16);
    else turn_update(&c, 16);
    whole_cpu(&c);
    u32 passed, angle;
    memcpy(&passed, &turn_passed, 4), memcpy(&angle, &turn_angle, 4);
    whole_mix(passed), whole_mix(angle);
}
static void whole_list_setup(void) {
    memset(memory, 0, sizeof memory);
    list_scroll_original = list_scroll_mock;
    gw32(object + 0x1b8, 1);
    guest_float_write(object + 0x1d8, 0.25f);
}
static void whole_list(unsigned i, int stock) {
    /* An ease, then the free scroll, then a new ease. */
    if (!gr32(object + 0x1b8)) gw32(object + 0x1b8, 4);
    if (i % 40 == 39) gw32(object + 0x1b8, 1), gw32(object + 0x1bc, 0);
    CPU c = {.x = {[0] = object, [30] = 64}, .pc = 16};
    if (stock) list_scroll_mock(&c, 16);
    else list_scroll_update(&c, 16);
    whole_cpu(&c);
}
static void whole_blink_stock(CPU* c, u64 entry) {
    (void)entry;
    if (gr32(c->x[0] + 0x10)) gw32(c->x[0] + 0x10, gr32(c->x[0] + 0x10) - 1);
    else gw32(c->x[0] + 0x10, 90), gw32(c->x[0] + 0x14, gr32(c->x[0] + 0x14) + 1);
    c->pc = c->x[30];
}
static void whole_blink_setup(void) {
    memset(memory, 0, sizeof memory);
    blink_original[0] = whole_blink_stock;
    gw32(object + 0x10, 90);
}
static void whole_blink(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[0] = object, [30] = 32}, .pc = 16};
    if (stock) whole_blink_stock(&c, 16);
    else blink_primary(&c, 16);
    whole_cpu(&c);
}
/* The collector counts +18 down and sets the repeat mask +B8 when it reaches 0. */
static void whole_pad_stock(CPU* c, u64 entry) {
    (void)entry;
    const u64 pad = c->x[0];
    gw32(pad + 0x18, gr32(pad + 0x18) - 1);
    gw64(pad + 0xb8, 0);
    if (!gr32(pad + 0x18)) gw32(pad + 0x18, 3), gw64(pad + 0xb8, 0x10);
    c->pc = c->x[30];
}
static void whole_pad_setup(void) {
    memset(memory, 0, sizeof memory);
    pad_repeat_original = whole_pad_stock;
    gw32(object + 0x18, 10);
}
static void whole_pad(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[0] = object, [30] = 32}, .pc = 16};
    if (stock) whole_pad_stock(&c, 16);
    else pad_repeat_update(&c, 16);
    whole_cpu(&c);
}
static void whole_event_camera_stock(CPU* c, u64 entry) {
    (void)entry;
    guest_float_write(c->x[0] + 0x4b0, guest_float(c->x[0] + 0x4b0) + guest_float(c->x[0] + 0x4b4));
    c->pc = c->x[30];
}
static void whole_event_camera_setup(void) {
    memset(memory, 0, sizeof memory);
    event_camera_original = whole_event_camera_stock;
    gw32(object + 0x3e4, 1);
    guest_float_write(object + 0x4b4, 0.0137f);
}
static void whole_event_camera(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[0] = object, [30] = 32}, .pc = 16};
    if (stock) whole_event_camera_stock(&c, 16);
    else event_camera_update(&c, 16);
    whole_cpu(&c);
}
static void whole_camera_rate_stock(CPU* c, u64 entry) {
    (void)entry;
    c->v[0].f[0] = 0.0663225f;
    c->pc = c->x[30];
}
static void whole_camera_rate_setup(void) {
    memset(memory, 0, sizeof memory);
    camera_rate_original = whole_camera_rate_stock;
    camera_speed = 1.0f, camera_trace_only = 0;
}
static void whole_camera_rate(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[19] = object, [30] = 32}, .pc = 16};
    if (stock) whole_camera_rate_stock(&c, 16);
    else camera_rate_update(&c, 16);
    whole_cpu(&c);
}
static void whole_cave_stock(CPU* c, u64 entry) {
    (void)entry;
    const u64 record = c->x[0] + CAVE_LIGHT_RECORD;
    guest_float_write(record + 8, guest_float(record + 8) + guest_float(record + 0xc));
    c->pc = c->x[30];
}
static void whole_cave_setup(void) {
    memset(memory, 0, sizeof memory);
    cave_light_original = whole_cave_stock;
    gw32(2048 + 4, 1);
    guest_float_write(2048 + 0xc, 1.0f / 120);
}
static void whole_cave(unsigned i, int stock) {
    (void)i;
    CPU c = {.x = {[0] = 2048 - CAVE_LIGHT_RECORD, [30] = 32}, .pc = 16};
    if (stock) whole_cave_stock(&c, 16);
    else cave_light_update(&c, 16);
    whole_cpu(&c);
}
static void test_whole_step(void) {
    static const struct {
        const char* name;
        void (*setup)(void);
        void (*frame)(unsigned, int);
        float speed;
        unsigned n;
    } cases[] = {
        {"sequence", whole_sequence_setup, whole_sequence, 1.0f, 0},
        {"sequence at speed 0.2", whole_sequence_setup, whole_sequence, 0.2f, 0},
        {"sequence at speed 0.5", whole_sequence_setup, whole_sequence, 0.5f, 0},
        {"sequence at speed 2", whole_sequence_setup, whole_sequence, 2.0f, 0},
        {"sequence at speed 6", whole_sequence_setup, whole_sequence, 6.0f, 0},
        {"capture", whole_capture_setup, whole_capture, 0, 0},
        {"map cursor", whole_cursor_setup, whole_cursor, 0, 0},
        {"AI tick, turn every pass", whole_ai_setup, whole_ai, 0, 1},
        {"AI tick, turn every 4th pass", whole_ai_setup, whole_ai, 0, 4},
        {"turn", whole_turn_setup, whole_turn, 0, 0},
        {"list scroll", whole_list_setup, whole_list, 0, 0},
        {"blink", whole_blink_setup, whole_blink, 0, 0},
        {"pad repeat", whole_pad_setup, whole_pad, 0, 0},
        {"event camera", whole_event_camera_setup, whole_event_camera, 0, 0},
        {"camera rate", whole_camera_rate_setup, whole_camera_rate, 0, 0},
        {"cave light", whole_cave_setup, whole_cave, 0, 0},
    };
    enum { FRAMES = 3000 };
    unsigned failed = 0;
    static u64 seen[2][FRAMES];
    const u64 saved_base = main_base, saved_cell = frame_step_address, saved_frames = frame_step_frames;
    const ArceusStep saved_step = step;
    const int saved_experimental = experimental;
    /* Two series: the played one without its five steps outside the window, and 33.0 to 33.6 ms. */
    for (unsigned series = 0; series < 2; series++)
        for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++)
            for (int stock = 0; stock < 2; stock++) {
                whole_speed = cases[k].speed, whole_every = cases[k].n;
                main_base = 0;
                cases[k].setup();
                experimental = 1;
                frame_step_address = 3900;
                frame_step_frames = 0;
                blink_remainder = 0;
                host_ns = 1000000000;
                step = (ArceusStep){.initial_ns = arceus_whole_step(33333333)};
                whole_hash = 0xcbf29ce484222325ull;
                for (unsigned i = 0, at = 0; i < FRAMES; i++, at++) {
                    while (arceus_whole_step(arceus_steps_30[at % ARCEUS_STEPS_30]) != ARCEUS_WHOLE_STEP_NS) at++;
                    whole_frame(series ? 33000000 + (i * 2654435761u >> 8) % 600001 : arceus_steps_30[at % ARCEUS_STEPS_30]);
                    cases[k].frame(i, stock);
                    for (unsigned b = 0; b < sizeof memory; b += 8) whole_mix(gr64(b ? b : 8));
                    seen[stock][i] = whole_hash;
                    if (stock && seen[0][i] != seen[1][i]) {
                        fprintf(stderr, "arceus_clock: %s differs from the game at frame %u of series %u\n",
                                cases[k].name, i, series);
                        failed++;
                        break;
                    }
                }
            }
    assert(!failed);
    /* A short frame between whole ones holds the 30 Hz gates once and leaves nothing behind. */
    static const u64 callers[4] = {0x278150c, 0x2781540, 0x2781574, 0x27815a8};
    main_base = 0, experimental = 1;
    for (unsigned i = 0; i < 40; i++) {
        const u64 elapsed = i % 4 == 1 ? 30912500 : 33200000;
        host_ns += elapsed;
        for (unsigned channel = 0; channel < 4; channel++) {
            CPU c = {.x = {[0] = 4000, [1] = channel, [2] = 33333334, [30] = callers[channel]}};
            adjust_duration(&c);
            assert(c.x[2] == (i % 4 == 1 ? elapsed : ARCEUS_WHOLE_STEP_NS));
        }
        assert(blink_ticks == (i % 4 != 1) && blink_remainder == (i % 4 == 1 ? elapsed : 0));
    }
    /* The played steps with the five outside the window: the sequence still runs at most one
       update per rendered frame, ends on every whole frame and loses under one frame in 96. */
    for (unsigned speed = 0; speed < 2; speed++) {
        reset(0, speed ? 0.5f : 1.0f, 0);
        guest_float_write(object + 0xec, 100000);
        step = (ArceusStep){.initial_ns = arceus_whole_step(33333333)};
        host_ns = 1000000000;
        double frames = 0;
        float last = 0;
        for (unsigned i = 0; i < 20 * ARCEUS_STEPS_30; i++) {
            u64 ns = 0;
            host_ns += arceus_steps_30[i % ARCEUS_STEPS_30];
            assert(arceus_step(&step, host_ns, 1, 0, &ns));
            frames += (speed ? 0.5 : 1.0) * (double)ns / (double)ARCEUS_WHOLE_STEP_NS;
            update();
            const float frame = guest_float(object + 0xe8);
            assert(calls_this_render <= 1 && frame >= last && frame <= floorf(last) + 1);
            last = frame;
        }
        assert(boundaries == (unsigned)last && frames - last < 20 && frames - last > -1);
    }
    /* Off, the step is the measured one. */
    step = (ArceusStep){.measured_only = 1};
    u64 out = 0;
    assert(arceus_step(&step, 1000000000, 1, 0, &out) && arceus_step(&step, 1033000000, 1, 0, &out) && out == 33000000);
    main_base = saved_base, frame_step_address = saved_cell, frame_step_frames = saved_frames;
    step = saved_step, experimental = saved_experimental;
    blink_ticks = 0, blink_remainder = 0;
    capture_fix_active = 0;
}
/* The reads of the player's battle Pokémon (P8, research 21:35): stand-in memory in the layout 882D58, 823C00, 834074
   and 2847B84 read, one placement of main for the slot and others for the mode tables, which lie 9 MB apart */
static void test_battle_mon(void) {
    const u64 saved_base = drp_base, saved_main = main_base;
    memset(memory, 0, sizeof memory);
    main_base = 0;
    drp_base = 0x100 - 0x4279580; /* the slot at 0x100, holding the singleton's address */
    put64(0x100, 0x108);
    put64(0x108, 0x120);         /* S */
    put64(0x120 + 0xb0, 0x200);  /* [S+B0] */
    put64(0x200 + 0xe0, 0x300);  /* X = [[S+B0]+E0] */
    put64(0x300 + 0xd0, 0x400);  /* c = [X+D0] */
    put64(0x400 + 0x68, 0x480);
    put64(0x480 + 0x98, 0x540);
    put64(0x540 + 0x370, 0x900);
    put64(0x900 + 8, 0xa00); /* T */
    memory[0xa00 + 0x38] = 1; /* side 0: one Pokémon, at 0xc00 */
    put64(0xa00 + 8, 0xc00);
    memory[0xa00 + 2 * 0x38 + 0x38] = 2; /* side 2: its first at 0xd00 */
    put64(0xa00 + 2 * 0x38 + 8, 0xd00);
    put64(0xa00 + 0x38 + 8, 0xe00); /* side 1 has an entry but a count of 0: none */
    const u64 t = drp_battle_table();
    assert(t == 0xa00 && drp_battle_first(t, 0) == 0xc00 && drp_battle_first(t, 2) == 0xd00);
    assert(drp_battle_first(t, 1) == 0 && drp_battle_first(t, 4) == 0);
    /* one read more or less than 882D58 finds nothing: X's first word and the slot's own value lead nowhere */
    assert(drp_rd(drp_rd(drp_rd(drp_rd(drp_rd(drp_base + 0x4279580)) + 0xb0) + 0xe0)) == 0);
    assert(drp_rd(drp_rd(drp_rd(drp_base + 0x4279580) + 0xb0) + 0xe0) == 0);
    put64(0x540 + 0x370, 0); /* a missing link */
    assert(drp_battle_table() == 0);
    put64(0x540 + 0x370, 0x900);
    put64(0x900 + 8, sizeof memory - 0x100); /* a table not readable for its five sides */
    assert(drp_battle_table() == 0);
    /* index 0's side by the mode at M+110: 0 and 2 their tables, 1 by [[M+58]+55], others none */
    memset(memory, 0, sizeof memory);
    drp_base = 0x100 - 0x397eb50;
    put32(0x100, 1);           /* mode 0's table, main+397EB50 */
    put32(0x100 + 0x78, 0);    /* mode 2's, main+397EBC8 */
    put32(0x100 + 0x18, 2);    /* mode 1's when the kind is not 1 to 5, main+397EB68 */
    put64(0x800 + 0x58, 0x600);
    assert(drp_index0_side(0x800) == 1);
    put32(0x800 + 0x110, 2);
    assert(drp_index0_side(0x800) == 0);
    put32(0x800 + 0x110, 3);
    assert(drp_index0_side(0x800) == -1);
    put32(0x800 + 0x110, 1);
    assert(drp_index0_side(0x800) == 2); /* kind 0 */
    memory[0x600 + 0x55] = 9;
    assert(drp_index0_side(0x800) == 2);
    assert(drp_index0_side(sizeof memory) == -1);
    memset(memory, 0, sizeof memory);
    drp_base = 0x100 - 0x41767f8; /* mode 1 with kind 3: the pointer table's third entry */
    put64(0x100 + 2 * 8, 0x300);
    put32(0x300, 4);
    put32(0x800 + 0x110, 1);
    put64(0x800 + 0x58, 0x600);
    memory[0x600 + 0x55] = 3;
    assert(drp_index0_side(0x800) == 4);
    memset(memory, 0, sizeof memory);
    drp_base = saved_base, main_base = saved_main;
}

/* D2's reader on a stand-in Pokémon (D3d reads the battle's copy at P+60 with it): the object's vtable, its data
   object (+8 the extension, +10 the record, +18 enciphered, +30 the lock) and a record we encipher ourselves */
static void test_mon_read(void) {
    const u64 saved_base = drp_base, saved_main = main_base;
    const int saved_d2 = drp_d2;
    memset(memory, 0, sizeof memory);
    drp_base = 0x10000000;
    main_base = 0; /* without the timing layer: every read goes to drp_base (the reviewer's D1) */
    drp_d2 = 1;
    const u64 object = 0x100, data = 0x200, record = 0x300, extension = 0x500;
    put64(object, drp_base + DRP_MON_VTABLE);
    put64(object + 0x98, data);
    put64(data + 0x8, extension);
    put64(data + 0x10, record);
    memory[data + 0x18] = 1;
    u8* r = memory + record;
    const uint32_t key = (5u << 13) | 0x77; /* order ADCB: block A first */
    memcpy(r, &key, 4);
    u8* a = r + 8 + SNX_PK_BLOCK * snx_pk_position(key, 0);
    a[0] = 157 & 0xff; /* Typhlosion */
    a[1] = 157 >> 8;
    unsigned sum = 0;
    for (size_t i = 8; i < SNX_PK_RECORD; i += 2) sum += snx_pk_u16(r + i);
    r[6] = (u8)sum;
    r[7] = (u8)(sum >> 8);
    memory[extension] = 58;
    snx_pk_crypt(r + 8, SNX_PK_RECORD - 8, key);
    snx_pk_crypt(memory + extension, SNX_PK_EXTENSION, key);
    u8 before[SNX_PK_RECORD + SNX_PK_EXTENSION];
    memcpy(before, memory + record, SNX_PK_RECORD);
    memcpy(before + SNX_PK_RECORD, memory + extension, SNX_PK_EXTENSION);
    SnxDrpMon m = {0};
    assert(drp_mon_read(object, &m) == DRP_MON_OK && m.species == 157 && m.level == 58 && !m.alpha);
    /* the game's record and extension are left as they were: the copy is deciphered, never the game's memory */
    assert(!memcmp(before, memory + record, SNX_PK_RECORD));
    assert(!memcmp(before + SNX_PK_RECORD, memory + extension, SNX_PK_EXTENSION));
    put64(data + 0x30, 0x1234); /* held by a game thread: busy, try next second */
    assert(drp_mon_read(object, &m) == DRP_MON_BUSY);
    put64(data + 0x30, 0);
    memory[data + 0x18] = 0; /* said plain while enciphered: the sum fails, dropped */
    assert(drp_mon_read(object, &m) == DRP_MON_DROPPED);
    memory[data + 0x18] = 1;
    put64(object, drp_base + 0x418ae48); /* not a Pokémon object */
    assert(drp_mon_read(object, &m) == DRP_MON_NONE);
    put64(object, drp_base + DRP_MON_VTABLE);
    assert(drp_mon_read(sizeof memory, &m) == DRP_MON_NONE);
    drp_d2 = 0; /* the game's block order differed from ours */
    assert(drp_mon_read(object, &m) == DRP_MON_NONE);
    drp_d2 = saved_d2;
    memset(memory, 0, sizeof memory);
    drp_base = saved_base, main_base = saved_main;
}

/* P6's per-battle line budget (research 22:25): spent lines come back at the next battle start */
static void put_utf16(u64 at, const char* text) {
    for (; *text; text++, at += 2) memory[at] = (u8)*text, memory[at + 1] = 0;
}
/* D5 (research 01:10): the trainer's name from the battle's lines, read by the FC20C8 wrapper. Invented names and
   lines of the game's shape. */
static unsigned text_set_calls;
static void text_set_original(CPU* c, u64 entry) {
    (void)c, (void)entry;
    text_set_calls++;
}
static void battle_start_original(CPU* c, u64 entry) { (void)c, (void)entry; }
static void put16(u64 address, u16 value) { memcpy(GP(address), &value, 2); }
/* "Ada", a tag of length units, then " sent out Bidoof!": 3 + 2 + length + 17 units */
static void put_tagged(u64 at, unsigned length) {
    put_utf16(at, "Ada");
    put16(at + 6, 0x10);
    put16(at + 8, (u16)length);
    for (unsigned i = 0; i < length; i++) put16(at + 10 + i * 2ull, 1);
    put_utf16(at + 10 + length * 2ull, " sent out Bidoof!");
    put16(at + 10 + length * 2ull + 34, 0);
}
static void test_trainer_line(void) {
    const u64 saved_ns = host_ns, saved_base = drp_base;
    const int saved_presence = drp_presence;
    memset(memory, 0, sizeof memory);
    drp_base = 0x10000000;
    drp_presence = 1;
    drp_original[DRPS_TEXT_SET] = text_set_original;
    drp_original[DRPS_BATTLE] = battle_start_original;
    put_utf16(0x400, "Captain Ada wants a battle!");
    put_utf16(0x600, "Ada sent out Bidoof!");
    put_utf16(0x700, "Rook sent out Togepi!");
    g_drp_battle = 31;
    drp_kind_start = host_ns = 90000000000ull;
    host_ns += 3000000000ull;
    drp_values.activity = DRP_BATTLE;
    drp_values.kind = DRP_KIND_TRAINER;
    memset(drp_values.trainer, 0x55, sizeof drp_values.trainer); /* an earlier name's bytes */
    drp_values.trainer[0] = 0;
    const unsigned calls = text_set_calls, logs = log_lines;
    CPU c = {.x = {[0] = 0x200, [1] = 0x600, [30] = 0x10000100}};
    g_drp_files.kinds = DRP_FILE_WILD; /* not a trainer battle */
    drp_text_set(&c, 0);
    assert(!drp_values.trainer[0]);
    g_drp_files.kinds = DRP_FILE_TRAINER;
    c.x[1] = 0x400; /* another line is not used */
    drp_text_set(&c, 0);
    assert(!drp_values.trainer[0]);
    c.x[1] = 0x600;
    drp_text_set(&c, 0);
    assert(!strcmp(drp_values.trainer, "Ada") && drp_name_battle == 31);
    c.x[1] = 0x700; /* the first match in the battle wins */
    drp_text_set(&c, 0);
    assert(!strcmp(drp_values.trainer, "Ada"));
    SnxDrpValues v = {0};
    assert(snx_drp_read(&g_drp_state, &v, 4) && !strcmp(v.trainer, "Ada"));
    for (size_t i = 3; i < sizeof v.trainer; i++) assert(!drp_values.trainer[i] && !v.trainer[i]);
    memset(drp_values.trainer + 5, 0x55, sizeof drp_values.trainer - 5);
    drp_set(DRP_FIELD); /* the battle's end clears it, all of it */
    for (size_t i = 0; i < sizeof v.trainer; i++) assert(!drp_values.trainer[i]);
    memset(drp_values.trainer, 0x55, sizeof drp_values.trainer);
    CPU start = {.x = {[0] = 0x800}}; /* a new battle's start clears the whole name too */
    drp_battle_start(&start, 0);
    for (size_t i = 0; i < sizeof v.trainer; i++) assert(!drp_values.trainer[i]);
    assert(drp_values.activity == DRP_BATTLE && drp_values.kind == DRP_KIND_PENDING && g_drp_battle == 32);
    g_drp_files.kinds = DRP_FILE_TRAINER; /* 20 s after its start: no more looking */
    host_ns = drp_kind_start + 20000000000ull;
    c.x[1] = 0x600;
    drp_text_set(&c, 0);
    assert(!drp_values.trainer[0]);
    host_ns = drp_kind_start + 19000000000ull;
    put_tagged(0xc00, 75); /* 97 units: the 96 read end before its "!" */
    c.x[1] = 0xc00;
    drp_text_set(&c, 0);
    assert(!drp_values.trainer[0]);
    put_tagged(0xb00, 74); /* 96 units, all read */
    c.x[1] = 0xb00;
    drp_text_set(&c, 0);
    assert(!strcmp(drp_values.trainer, "Ada") && drp_name_battle == 32);
    c.x[1] = 0x600;
    drp_text_set(&c, 0);
    assert(!strcmp(drp_values.trainer, "Ada") && drp_name_battle == 32);
    assert(text_set_calls == calls + 8 && log_lines == logs); /* always the game's function, nothing logged */
    drp_set(DRP_FIELD);
    drp_values.activity = DRP_TITLE;
    drp_presence = saved_presence;
    g_drp_files.kinds = 0;
    g_drp_battle = 0;
    drp_last_battle = 0;
    host_ns = saved_ns;
    drp_base = saved_base;
    memset(memory, 0, sizeof memory);
}

/* Research 01:25 item 2: the tick decides a quiet battle's end and makes it in one hold of drp_lock, so a battle that
   starts as the lock is let go keeps its status. The title ends at the camera's first input. */
static u64 race_object;
static void race_start(void) {
    CPU start = {.x = {[0] = race_object}};
    drp_battle_start(&start, 0);
}
/* bare: the battle sequence count is the bare path's (DISCORD-30), not the fix's */
static void tick_end(int bare) {
    const u64 saved_ns = host_ns, saved_base = drp_base, saved_camera = camera_input_calls;
    const GuestFn saved_sequence = battle_sequence_original, saved_bare = drp_bare_body[BODY_BATTLE_SEQUENCE];
    const int saved_presence = drp_presence;
    memset(memory, 0, sizeof memory);
    drp_base = 0x10000000;
    drp_presence = 1;
    drp_original[DRPS_BATTLE] = battle_start_original;
    battle_sequence_original = bare ? NULL : sequence_original;
    drp_bare_body[BODY_BATTLE_SEQUENCE] = bare ? sequence_original : NULL;
    camera_input_calls = 0;
    drp_values.activity = DRP_TITLE;
    drp_next_tick = 0;
    host_ns = 200000000000ull;
    drp_tick();
    assert(drp_values.activity == DRP_TITLE); /* no camera input yet */
    camera_input_calls = 1;
    host_ns += 1000000000ull;
    drp_tick();
    assert(drp_values.activity == DRP_FIELD);
    race_object = 0x900;
    race_start();
    assert(drp_values.activity == DRP_BATTLE);
    for (unsigned i = 0; i < 5; i++) { /* the effects run: a moving counter keeps the battle */
        __atomic_add_fetch(&battle_sequence_calls, 1, __ATOMIC_RELAXED);
        host_ns += 1000000000ull;
        drp_tick();
        assert(drp_values.activity == DRP_BATTLE);
    }
    drp_battle_seen = battle_sequence_calls; /* the effects stopped */
    host_ns += 2000000000ull;
    drp_tick();
    assert(drp_values.activity == DRP_BATTLE); /* 2 s quiet: not over */
    host_ns += 1000000000ull;
    drp_tick();
    assert(drp_values.activity == DRP_FIELD && drp_values.kind == DRP_KIND_RECORD); /* 3 s: over */
    race_object = 0x980;
    race_start();
    host_ns += 3000000000ull;
    race_object = 0xa00; /* quiet for 3 s, and a new battle starts as the tick lets the lock go */
    unlock_hook = race_start;
    const u32 battle = g_drp_battle;
    drp_tick();
    assert(!unlock_hook && g_drp_battle == battle + 1);
    assert(drp_values.activity == DRP_BATTLE && drp_values.kind == DRP_KIND_PENDING);
    drp_set(DRP_FIELD);
    /* with no sequence counter at all, quiet never ends a battle */
    battle_sequence_original = drp_bare_body[BODY_BATTLE_SEQUENCE] = NULL;
    race_object = 0xa80;
    race_start();
    host_ns += 5000000000ull;
    drp_tick();
    assert(drp_values.activity == DRP_BATTLE);
    drp_set(DRP_FIELD);
    drp_values.activity = DRP_TITLE;
    battle_sequence_original = saved_sequence, drp_bare_body[BODY_BATTLE_SEQUENCE] = saved_bare;
    camera_input_calls = saved_camera;
    drp_presence = saved_presence;
    drp_next_tick = 0;
    drp_last_battle = 0;
    g_drp_battle = 0;
    g_drp_files.kinds = 0;
    host_ns = saved_ns;
    drp_base = saved_base;
}
static void test_tick_end(void) {
    tick_end(0);
    tick_end(1);
}

/* SNX_ARCEUS_SCENE_SCALE: what the switch's text means, when the game's table may be written, and the real install
   on a table laid out in the test's memory. */
static void scene_table(u64 table) {
    const float pairs[ARCEUS_SCENE_SCALE_WORDS] = {0.9f, 0.85f, 0.85f, 0.8f, 0.8f, 0.7f, 1.0f, 1.0f, 0.5f, 0.5f};
    memset(memory, 0, sizeof memory);
    for (int i = 0; i < ARCEUS_SCENE_SCALE_WORDS; i++) guest_float_write(table + 4 * (u64)i, pairs[i]);
}
static void test_scene_scale(void) {
    float scale = 0.0f;
    /* unset, 1.0 and above: off, and the scale stays 1 */
    assert(arceus_scene_scale_parse(NULL, &scale) == ARCEUS_SCENE_SCALE_OFF && scale == 1.0f);
    assert(arceus_scene_scale_parse("1", &scale) == ARCEUS_SCENE_SCALE_OFF && scale == 1.0f);
    assert(arceus_scene_scale_parse("1.0", &scale) == ARCEUS_SCENE_SCALE_OFF && scale == 1.0f);
    assert(arceus_scene_scale_parse("2.5", &scale) == ARCEUS_SCENE_SCALE_OFF && scale == 1.0f);
    /* from 0.5 to below 1.0: used as written */
    assert(arceus_scene_scale_parse("0.5", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.5f);
    assert(arceus_scene_scale_parse("0.75", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.75f);
    assert(arceus_scene_scale_parse(".9", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.9f);
    assert(arceus_scene_scale_parse("0.999", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.999f);
    assert(arceus_scene_scale_parse("7.5e-1", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.75f);
    /* Everything else is refused and leaves the scale at 1: below 0.5 (never raised to it), nothing, text, text
       after a number, and what is not finite. */
    static const char* const bad[] = {"0.49", "0", "-0.75", "",     "abc",  "0.75x", "0.75 ", "0,75",
                                      "nan",  "NAN", "inf",  "-inf", "1e99", "-1e99", "0x",    " "};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        scale = 0.25f;
        assert(arceus_scene_scale_parse(bad[i], &scale) == ARCEUS_SCENE_SCALE_BAD);
        assert(scale == 1.0f);
    }
    /* Written as p/q, whole numbers with 1 <= p < q <= 64 and nothing else: divided in double, narrowed once.
       Two thirds times 3840 and 2160 then lands on 2560 and 1440 in the float the game is given. */
    assert(arceus_scene_scale_parse("2/3", &scale) == ARCEUS_SCENE_SCALE_USED && scale == (float)(2.0 / 3.0));
    assert(fabsf(3840.0f * scale - 2560.0f) < 0.001f && fabsf(2160.0f * scale - 1440.0f) < 0.001f);
    assert(arceus_scene_scale_parse("3/4", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.75f);
    assert(arceus_scene_scale_parse("1/2", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.5f);
    assert(arceus_scene_scale_parse("63/64", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.984375f);
    assert(arceus_scene_scale_parse("32/64", &scale) == ARCEUS_SCENE_SCALE_USED && scale == 0.5f);
    assert(arceus_scene_scale_parse("02/03", &scale) == ARCEUS_SCENE_SCALE_USED && scale == (float)(2.0 / 3.0));
    /* Written as pofq, for a launcher that cannot pass a slash: the same rules, the same arithmetic and the
       very same float. */
    {
        static const char* const pairs[][2] = {{"2of3", "2/3"}, {"3of4", "3/4"}, {"1of2", "1/2"}, {"63of64", "63/64"},
                                               {"02of03", "02/03"}, {"5of8", "5/8"}};
        for (unsigned i = 0; i < sizeof pairs / sizeof *pairs; i++) {
            float slash = 0.0f;
            assert(arceus_scene_scale_parse(pairs[i][0], &scale) == ARCEUS_SCENE_SCALE_USED);
            assert(arceus_scene_scale_parse(pairs[i][1], &slash) == ARCEUS_SCENE_SCALE_USED);
            assert(!memcmp(&scale, &slash, sizeof scale));
        }
        static const char* const words[] = {"2of",   "of3",   "2 of 3", "2OF3",  "2of3x", "2Of3",  "2oF3",  "2o3",
                                            "2f3",   "2ofof3", "2of/3", "2/of3", "1of3",  "3of2",  "0of5",  "2of0",
                                            "65of66", "2of3 ", " 2of3", "of",    "o",     "2of3of4", "002of3", "-2of3"};
        for (unsigned i = 0; i < sizeof words / sizeof *words; i++) {
            scale = 0.25f;
            assert(arceus_scene_scale_parse(words[i], &scale) == ARCEUS_SCENE_SCALE_BAD);
            assert(scale == 1.0f);
        }
    }
    /* refused: under 0.5, p not under q, a part of 0 or over 64, a missing part, and anything else in the value */
    static const char* const parts[] = {"1/3",  "3/2",   "3/3",  "0/5",  "2/0",   "2/3x", " 2/3", "2/3 ",  "65/66",
                                        "1/65", "31/64", "/3",   "2/",   "/",     "2//3", "2/3/4", "-2/3", "2/-3",
                                        "+2/3", "2.0/3", "2/3.0", "2 /3", "002/3", "2/003", "a/b",  "1e0/2"};
    for (unsigned i = 0; i < sizeof parts / sizeof *parts; i++) {
        scale = 0.25f;
        assert(arceus_scene_scale_parse(parts[i], &scale) == ARCEUS_SCENE_SCALE_BAD);
        assert(scale == 1.0f);
    }
    /* the guard on the table's words: the governor first, then both words of the pair, then the pair after it */
    const float table[ARCEUS_SCENE_SCALE_WORDS] = {0.9f, 0.85f, 0.85f, 0.8f, 0.8f, 0.7f, 1.0f, 1.0f, 0.5f, 0.5f};
    uint32_t words[ARCEUS_SCENE_SCALE_WORDS], changed[ARCEUS_SCENE_SCALE_WORDS];
    memcpy(words, table, sizeof words);
    assert(ARCEUS_SCENE_SCALE_TABLE + 4 * ARCEUS_SCENE_SCALE_PAIR == 0x3978758u);
    assert(arceus_scene_scale_guard(1, words) == ARCEUS_SCENE_GUARD_OK);
    assert(arceus_scene_scale_guard(0, words) == ARCEUS_SCENE_GUARD_GOVERNOR);
    for (int word = 0; word < 2; word++)
        for (int kind = 0; kind < 3; kind++) {
            memcpy(changed, words, sizeof changed);
            /* one bit above 1.0, one bit below, and 0.75 as if already scaled */
            changed[ARCEUS_SCENE_SCALE_PAIR + word] = kind == 0 ? 0x3f800001u : kind == 1 ? 0x3f7fffffu : 0x3f400000u;
            assert(arceus_scene_scale_guard(1, changed) == ARCEUS_SCENE_GUARD_PAIR);
        }
    for (int word = 2; word < 4; word++) {
        memcpy(changed, words, sizeof changed);
        changed[ARCEUS_SCENE_SCALE_PAIR + word] = 0x3f800000u;
        assert(arceus_scene_scale_guard(1, changed) == ARCEUS_SCENE_GUARD_TABLE);
    }
    /* the three pairs before are not tested: other values there do not refuse */
    memcpy(changed, words, sizeof changed);
    for (int word = 0; word < ARCEUS_SCENE_SCALE_PAIR; word++) changed[word] = 0;
    assert(arceus_scene_scale_guard(1, changed) == ARCEUS_SCENE_GUARD_OK);
    /* the write took only when both words read back as written */
    assert(arceus_scene_scale_took(0x3f400000u, 0x3f400000u, 0x3f400000u));
    assert(!arceus_scene_scale_took(0x3f400000u, 0x3f400000u, 0x3f800000u));
    assert(!arceus_scene_scale_took(0x3f400000u, 0x3f800000u, 0x3f400000u));

    /* The install itself. The module's base is chosen so that the table lies at 256 in the test's memory. */
    const u64 saved_base = main_base, at = 256, pair = at + 4 * ARCEUS_SCENE_SCALE_PAIR;
    const int saved_active = fixed_resolution_active;
    u8 before[sizeof memory];
    main_base = at - ARCEUS_SCENE_SCALE_TABLE;
    /* 0: all in order. Then what must leave every byte as it was: 1 the switch unset, 2 at 1.0, 3 not a number,
       4 below 0.5, 5 the governor not off, 6 and 7 one word of the pair not 1.0, 8 the pair after it not 0.5,
       9 the table outside mapped memory. */
    for (int kind = 0; kind < 10; kind++) {
        scene_table(at);
        fixed_resolution_active = kind != 5;
        if (kind == 1) unset_flag("SNX_ARCEUS_SCENE_SCALE");
        else flag("SNX_ARCEUS_SCENE_SCALE", kind == 2 ? "1.0" : kind == 3 ? "0.75x" : kind == 4 ? "0.4" : "0.75");
        if (kind == 6) guest_float_write(pair, 0.9f);
        if (kind == 7) guest_float_write(pair + 4, 0.9f);
        if (kind == 8) guest_float_write(pair + 8, 1.0f);
        if (kind == 9) main_base = sizeof memory - 8 - ARCEUS_SCENE_SCALE_TABLE;
        memcpy(before, memory, sizeof memory);
        scene_scale_init();
        if (kind == 9) main_base = at - ARCEUS_SCENE_SCALE_TABLE;
        if (kind) {
            assert(!memcmp(before, memory, sizeof memory));
            continue;
        }
        /* both words of the pair are the number, and nothing else moved */
        assert(guest_float(pair) == 0.75f && guest_float(pair + 4) == 0.75f);
        guest_float_write(pair, 1.0f);
        guest_float_write(pair + 4, 1.0f);
        assert(!memcmp(before, memory, sizeof memory));
    }
    /* a second install finds the pair already changed and leaves it */
    scene_table(at);
    fixed_resolution_active = 1;
    flag("SNX_ARCEUS_SCENE_SCALE", "0.75");
    scene_scale_init();
    flag("SNX_ARCEUS_SCENE_SCALE", "0.5");
    scene_scale_init();
    assert(guest_float(pair) == 0.75f && guest_float(pair + 4) == 0.75f);
    unset_flag("SNX_ARCEUS_SCENE_SCALE");
    main_base = saved_base;
    fixed_resolution_active = saved_active;
    memset(memory, 0, sizeof memory);
}
static unsigned throw_original_calls;
static void throw_original(CPU* c, u64 entry) {
    assert(entry == 0x16e33ec);
    throw_original_calls++;
    c->pc = c->x[30];
}
/* One call of the wrapper on Q=0x100, W=0x400 and the handle at out, prefilled so a write shows. */
static void throw_call(u64 q, u64 out) {
    CPU c = {0};
    c.x[0] = q;
    c.x[8] = out;
    c.x[30] = 0x777;
    if (out + 16 <= sizeof memory) gw64(out, 0x1111), gw64(out + 8, 0x2222);
    throw_guard_update(&c, 0x16e33ec);
    assert(c.pc == 0x777 && c.x[0] == q && c.x[8] == out);
}
static void throw_owner(u64 w, u32 use, u64 object) {
    gw64(0x100 + 0x88, w);
    if (w && w < sizeof memory) gw32(w + 0xc, use), gw64(w + 0x18, object);
}
static void test_throw_guard(void) {
    memset(memory, 0, sizeof memory);
    throw_guard_original = throw_original;
    throw_original_calls = 0;
    throw_guard_lines = throw_guard_reached = 0;
    throw_guard_count = 0;
    throw_guard_logged = 0;
    /* Live: the original once, the handle untouched, one line that the wrapper is reached. */
    throw_owner(0x400, 1, 0x450);
    throw_call(0x100, 0x300);
    assert(throw_original_calls == 1 && gr64(0x300) == 0x1111 && gr64(0x308) == 0x2222);
    assert(throw_guard_logged == 1 && strstr(throw_guard_last, "owner live"));
    throw_call(0x100, 0x300);
    assert(throw_original_calls == 2 && throw_guard_logged == 1 && !throw_guard_count);
    /* Each dead condition: {0,0} and no original. */
    static const struct { u64 w; u32 use; u64 object; const char* why; } dead[] = {
        {0, 0, 0, "(no reference)"}, {0x400, 0, 0x450, "(use count 0)"}, {0x400, 2, 0, "(object cleared)"},
    };
    for (unsigned k = 0; k < 3; k++) {
        throw_owner(dead[k].w, dead[k].use, dead[k].object);
        throw_call(0x100, 0x300);
        assert(throw_original_calls == 2 && !gr64(0x300) && !gr64(0x308));
        assert(throw_guard_count == k + 1 && throw_guard_logged == k + 2);
        assert(strstr(throw_guard_last, dead[k].why) && strstr(throw_guard_last, "Q=100 "));
    }
    assert(strstr(throw_guard_last, "W=400 ") && strstr(throw_guard_last, "guarded=3 "));
    /* Unreadable Q, unreadable W, and a handle that cannot be written: the game's own call. */
    throw_call(sizeof memory + 0x100, 0x300);
    assert(throw_original_calls == 3 && gr64(0x300) == 0x1111);
    throw_owner(sizeof memory + 0x80, 0, 0);
    throw_call(0x100, 0x300);
    assert(throw_original_calls == 4 && gr64(0x300) == 0x1111);
    throw_owner(0, 0, 0);
    throw_call(0x100, sizeof memory + 0x40);
    throw_call(0x100, sizeof memory - 8);
    assert(throw_original_calls == 6 && throw_guard_count == 3 && throw_guard_logged == 4);
    /* The lines stop at the limit, the count goes on. */
    for (unsigned k = 0; k < THROW_GUARD_LINES + 4; k++) throw_call(0x100, 0x300);
    assert(throw_guard_count == THROW_GUARD_LINES + 7 && throw_guard_logged == THROW_GUARD_LINES + 1);
    assert(throw_original_calls == 6);
    throw_guard_original = NULL;
    memset(memory, 0, sizeof memory);
}
/* 16E33EC's guard words at a stand-in function of 0xA70 bytes at 0x200, installed from post_jit with the
   timing layer off, as at a target of 30 or with SNX_ARCEUS_TIMING_FIXES=0. */
static void throw_guard_words(u64 pc) {
    gw32(pc + 0x1c, 0xaa0003f5);
    gw32(pc + 0x20, 0xf9404400);
    gw32(pc + 0x2c, 0xb5002040);
}
static void test_throw_guard_init(void) {
    const u64 pc = 0x200;
    SnxModInfo main_mod = {.name = "main", .base = pc - 0x16e33ec};
    u64 fn_lo[2] = {pc, 0x100};
    u32 fn_len[2] = {0xa70, 0x40};
    u8 stale[2] = {0};
    SnxModule module = {.nmods = 1, .mods = &main_mod, .nfns = 2, .fn_lo = fn_lo, .fn_len = fn_len, .stale = stale};
    static GuestFn table[(0xc70 - 0x100) / 4];
    GuestFn* const disp = g_disp;
    const SnxModule* const mod = g_mod;
    const u64 lo = g_code_lo, hi = g_code_hi;
    g_mod = &module;
    g_disp = table;
    g_code_lo = 0x100;
    g_code_hi = 0xc70;
    const int timing = timing_enabled;
    timing_enabled = clocks_installed = 0;
    jit_answer = throw_original;
    /* 0, or any value but 1: nothing read, nothing patched, nothing logged. Not the 1.1.1 build: the same. */
    memset(memory, 0, sizeof memory);
    throw_guard_words(pc);
    throw_guard_mod = &main_mod;
    throw_guard_warnings = throw_guard_logged = jit_changes = 0;
    /* Not "": on Windows an empty value removes the variable, which is unset there. */
    static const char* const off[] = {"0", "on"};
    for (unsigned k = 0; k < 2; k++) {
        flag("SNX_ARCEUS_THROW_OWNER_GUARD", off[k]);
        arceus_clock_post_jit();
    }
    unset_flag("SNX_ARCEUS_THROW_OWNER_GUARD");
    throw_guard_mod = NULL;
    arceus_clock_post_jit();
    assert(!throw_guard_warnings && !throw_guard_logged && gr32(pc + 0x1c) == 0xaa0003f5);
    assert(!table[(pc - g_code_lo) / 4]);
    /* Each refusal, with the switch unset: a WARN line and nothing patched. */
    for (unsigned k = 0; k < 8; k++) {
        throw_guard_words(pc);
        throw_guard_mod = &main_mod;
        fn_len[0] = 0xa70;
        fn_lo[1] = 0x100;
        g_code_hi = 0xc70;
        module.stale = stale;
        if (k == 0) module.stale = NULL; /* no function table */
        if (k == 1) gw32(pc + 0x1c, 0x91000015);
        if (k == 2) gw32(pc + 0x20, 0xf9404000);
        if (k == 3) gw32(pc + 0x2c, 0xb5002060);
        if (k == 4) fn_len[0] = 0xa6c;
        if (k == 5) fn_lo[1] = pc; /* a second function from the same start */
        if (k == 6) g_code_hi = 0xc6c;
        if (k == 7) fn_len[0] = 0xa74;
        const u32 word = gr32(pc + 0x1c);
        throw_guard_warnings = 0;
        arceus_clock_post_jit();
        assert(throw_guard_warnings == 1 && !jit_changes && gr32(pc + 0x1c) == word && !throw_guard_original);
        assert(!table[(pc - g_code_lo) / 4]);
    }
    /* Accepted unset with the timing layer off: the ADD in place of the MOV, the function marked changed, the wrapper
       in dispatch, and the line naming the switch that turns it off. */
    throw_guard_words(pc);
    fn_len[0] = 0xa70;
    throw_guard_logged = throw_guard_warnings = 0;
    jit_allowed = 1;
    arceus_clock_post_jit();
    assert(!throw_guard_warnings && throw_guard_logged == 1 && strstr(throw_guard_last, "SNX_ARCEUS_THROW_OWNER_GUARD=0 turns it off") &&
           !strstr(throw_guard_last, "EXPERIMENT"));
    assert(gr32(pc + 0x1c) == 0x91000015 && gr32(pc + 0x20) == 0xf9404400 && gr32(pc + 0x2c) == 0xb5002040);
    assert(jit_changes == 1 && jit_changed_at == pc && jit_changed_size == 0xa70);
    assert(throw_guard_original == throw_original && table[(pc - g_code_lo) / 4] == throw_guard_update);
    /* A second post_jit leaves it as it is. */
    arceus_clock_post_jit();
    assert(jit_changes == 1 && !throw_guard_warnings && throw_guard_logged == 1);
    /* A failed compile: a WARN line and dispatch left to the game. */
    table[(pc - g_code_lo) / 4] = NULL;
    throw_guard_words(pc);
    throw_guard_original = NULL;
    jit_answer = NULL;
    arceus_clock_post_jit();
    assert(throw_guard_warnings == 1 && strstr(throw_guard_last, "compile failed") && !table[(pc - g_code_lo) / 4]);
    /* Set to 1, as unset. */
    throw_guard_words(pc);
    jit_answer = throw_original;
    flag("SNX_ARCEUS_THROW_OWNER_GUARD", "1");
    arceus_clock_post_jit();
    assert(gr32(pc + 0x1c) == 0x91000015 && table[(pc - g_code_lo) / 4] == throw_guard_update);
    jit_allowed = 0;
    jit_changes = 0;
    jit_answer = NULL;
    unset_flag("SNX_ARCEUS_THROW_OWNER_GUARD");
    throw_guard_mod = NULL;
    timing_enabled = timing;
    g_disp = disp;
    g_mod = mod;
    g_code_lo = lo;
    g_code_hi = hi;
    memset(memory, 0, sizeof memory);
}

/* The event probe: each wrapper calls its original once with the registers as given, counts it, and a line every
   5 s gives the calls and frames a second since the last line. */
static unsigned event_stub_runs;
static void event_stub(CPU* c, u64 entry) {
    assert(entry == entry_pc && c->x[0] == object && c->x[1] == 7);
    event_stub_runs++;
    c->x[0] = 3;
    c->pc = c->x[30];
}
static void test_event_probe(void) {
    event_probe_original[0] = event_probe_original[1] = event_stub;
    event_probe_calls[0] = event_probe_calls[1] = event_probe_ns = 0;
    memset(event_probe_logged, 0, sizeof event_probe_logged);
    const int clocks = clocks_installed;
    const u64 frames = event_probe_frames, ns = host_ns;
    clocks_installed = 1;
    event_probe_logged[2] = event_probe_frames;
    logged_count = 0;
    event_stub_runs = 0;
    /* 6 s at 60 frames a second: the job once a frame, the resume every other frame */
    for (unsigned k = 0; k < 360; k++) {
        host_ns += 16666667;
        event_probe_frames++;
        CPU c = {.x = {[0] = object, [1] = 7, [30] = return_pc}, .pc = entry_pc};
        event_job_probe(&c, entry_pc);
        assert(c.x[0] == 3 && c.pc == return_pc);
        if (k % 2) {
            CPU d = {.x = {[0] = object, [1] = 7, [30] = return_pc}, .pc = entry_pc};
            event_resume_probe(&d, entry_pc);
            assert(d.x[0] == 3 && d.pc == return_pc);
        }
    }
    assert(event_stub_runs == 540 && event_probe_calls[0] == 360 && event_probe_calls[1] == 180);
    /* the first call starts the window, one line 5 s later */
    assert(logged_count == 1);
    assert(strstr(logged[0], "arceus-event-probe: seconds=5.00 job_15ba460_per_s=60.00 resume_f7ec80_per_s=30.00 "
                             "frames_per_s=60.00 job_total=301 resume_total=150"));
    /* without the clock wrappers the frames are not counted, and the line says so */
    clocks_installed = 0;
    for (unsigned k = 0; k < 300; k++) {
        host_ns += 16666667;
        CPU c = {.x = {[0] = object, [1] = 7, [30] = return_pc}, .pc = entry_pc};
        event_job_probe(&c, entry_pc);
    }
    assert(logged_count == 2 && strstr(logged[1], "frames_per_s=- (no clock wrappers)"));
    event_probe_original[0] = event_probe_original[1] = NULL;
    /* At 30 FPS the timing layer leaves main_base unset: the probe looks for the module itself, and with none
       supported it refuses, installs nothing and leaves main_base alone */
    const SnxModule* mod = g_mod;
    const u64 base = main_base;
    static const SnxModule none = {0};
    g_mod = &none;
    main_base = 0;
    event_probe = 0;
    flag("SNX_ARCEUS_EVENT_PROBE", "1");
    event_probe_init();
    unset_flag("SNX_ARCEUS_EVENT_PROBE");
    assert(!event_probe && !main_base && !event_probe_original[0] && !event_probe_original[1]);
    g_mod = mod;
    main_base = base;
    clocks_installed = clocks;
    event_probe_frames = frames;
    host_ns = ns;
    logged_count = 0;
}
/* The demo probe on its real wrapper: a stand-in step that adds 2 to the clock while playing and ends the demo on a
   given call. The step is called once with the registers as given; the probe writes nothing. */
#define DEMO_OBJECT 64
#define DEMO_CONTEXT 1100
static unsigned demo_stub_calls, demo_stub_end;
static void demo_stub(CPU* c, u64 entry) {
    assert(entry == entry_pc && c->x[0] == DEMO_OBJECT && c->x[1] == 9);
    if (gr32(DEMO_OBJECT + 0x3b8) == 3) {
        gw32(DEMO_CONTEXT + 0x7ec, gr32(DEMO_CONTEXT + 0x7ec) + 2);
        if (++demo_stub_calls == demo_stub_end) gw32(DEMO_OBJECT + 0x3b8, 5);
    }
    c->x[0] = 1;
    c->pc = c->x[30];
}
static void test_demo_probe(void) {
    memset(memory, 0, sizeof memory);
    const u64 ns = host_ns, frames = event_probe_frames;
    const int clocks = clocks_installed;
    clocks_installed = 1;
    gw64(DEMO_OBJECT + 0x3b0, DEMO_CONTEXT);
    gw32(DEMO_OBJECT + 0x3b8, 3);
    for (unsigned k = 0; k < 5; k++) gw64(DEMO_CONTEXT + 0x7f0 + 8 * k, DEMO_EMPTY_NAME);
    demo_probe_original = demo_stub;
    demo_probe_ns = demo_probe_object = 0;
    demo_stub_calls = 0;
    demo_stub_end = 400;
    logged_count = 0;
    /* 60 frames a second: the clock gains 120 a second, and playing ends after 400 calls at 800 */
    for (unsigned k = 0; k < 420; k++) {
        host_ns += 16666667;
        event_probe_frames++;
        u8 before[4096];
        memcpy(before, memory, sizeof memory);
        CPU c = {.x = {[0] = DEMO_OBJECT, [1] = 9, [30] = return_pc}, .pc = entry_pc};
        demo_probe_step(&c, entry_pc);
        assert(c.x[0] == 1 && c.pc == return_pc);
        /* only the stand-in's own writes: the clock and, at the end, the state */
        memcpy(before + DEMO_CONTEXT + 0x7ec, memory + DEMO_CONTEXT + 0x7ec, 4);
        memcpy(before + DEMO_OBJECT + 0x3b8, memory + DEMO_OBJECT + 0x3b8, 4);
        assert(!memcmp(before, memory, sizeof memory));
    }
    assert(logged_count == 2);
    assert(strstr(logged[0], "arceus-demo-probe: seconds=5.00 step_1458300_per_s=60.00 frames_per_s=60.00 state=3 ") &&
           strstr(logged[0], " context=44c clock=602 clock_per_s=120.0 slots_empty=1 calls_total=301"));
    assert(strstr(logged[1], "arceus-demo-probe: playing ended: state=5 clock=800 (from 2, 6.65 s at 120.0 a second)"));
    /* a name slot that holds the demo shows as not empty */
    gw64(DEMO_CONTEXT + 0x800, 7);
    u32 state;
    u64 context;
    int32_t clock;
    int empty;
    assert(demo_probe_read(DEMO_OBJECT, &state, &context, &clock, &empty) && !empty && context == DEMO_CONTEXT);
    /* an unreadable context: nothing read, the step still called once */
    gw64(DEMO_OBJECT + 0x3b0, 0);
    assert(!demo_probe_read(DEMO_OBJECT, &state, &context, &clock, &empty));
    demo_probe_original = NULL;
    clocks_installed = clocks;
    host_ns = ns;
    event_probe_frames = frames;
    logged_count = 0;
    memset(memory, 0, sizeof memory);
}
/* DEMO_TICK on its real wrapper: a stand-in step that, in state 3 or 4, adds 2 to the clock from max(clock, 0), or
   holds it, or sets another value */
static int demo_game_hold, demo_game_other;
static void demo_game_step(CPU* c, u64 entry) {
    (void)entry;
    const u64 object = c->x[0], context = gr64(object + 0x3b0);
    const u32 state = gr32(object + 0x3b8);
    const int32_t clock = (int32_t)gr32(context + 0x7ec);
    if ((state == 3 || state == 4) && !demo_game_hold)
        gw32(context + 0x7ec, (u32)(demo_game_other ? 77 : (clock < 0 ? 0 : clock) + 2));
    c->x[0] = 1;
    c->pc = c->x[30];
}
static int32_t demo_tick_call(u64 object, u64 ns) {
    step.step = ns;
    CPU c = {.x = {[0] = object, [30] = return_pc}, .pc = entry_pc};
    demo_tick_step(&c, entry_pc);
    assert(c.x[0] == 1 && c.pc == return_pc);
    return (int32_t)gr32(gr64(object + 0x3b0) + 0x7ec);
}
static void demo_tick_reset(int32_t clock) {
    memset(demo_tick_states, 0, sizeof demo_tick_states);
    gw32(DEMO_CONTEXT + 0x7ec, (u32)clock);
    demo_game_hold = demo_game_other = 0;
}
static void test_demo_tick(void) {
    memset(memory, 0, sizeof memory);
    const u64 ns = step.step;
    gw64(DEMO_OBJECT + 0x3b0, DEMO_CONTEXT);
    gw32(DEMO_OBJECT + 0x3b8, 3);
    demo_tick_original = demo_game_step;
    /* 30 FPS: 2 a call, as the game */
    demo_tick_reset(0);
    for (unsigned k = 1; k <= 30; k++) assert(demo_tick_call(DEMO_OBJECT, 33333334) == (int32_t)(2 * k));
    /* 60 FPS: 1 a call, 60 a second */
    demo_tick_reset(0);
    for (unsigned k = 1; k <= 60; k++) assert(demo_tick_call(DEMO_OBJECT, 16666667) == (int32_t)k);
    /* 19.2 ms frames, 52 a second: 60 units within 1, never more than 2 a call */
    demo_tick_reset(0);
    int32_t clock = 0;
    for (unsigned k = 0; k < 52; k++) {
        const int32_t next = demo_tick_call(DEMO_OBJECT, 19200000);
        assert(next - clock >= 0 && next - clock <= 2);
        clock = next;
    }
    assert(clock >= 59 && clock <= 61);
    /* a 100 ms frame at 60 FPS: 2 there, its carry of 4 units 2 a call over the next calls, then 1 a call: 13 units
       for 216.7 ms, nothing lost */
    demo_tick_reset(0);
    clock = 0;
    static const int32_t gains[8] = {1, 1, 2, 2, 2, 2, 2, 1};
    static const u64 steps[8] = {16666667, 16666667, 100000000, 16666667, 16666667, 16666667, 16666667, 16666667};
    for (unsigned k = 0; k < 8; k++) {
        const int32_t next = demo_tick_call(DEMO_OBJECT, steps[k]);
        assert(next - clock == gains[k]);
        clock = next;
    }
    /* five 100 ms frames: 2 a call, and the carry is capped at 4 units, so at 60 FPS after them four calls give 2 and
       then 1 a call again (the time beyond the cap is not owed) */
    demo_tick_reset(0);
    for (unsigned k = 0; k < 5; k++) (void)demo_tick_call(DEMO_OBJECT, 100000000);
    clock = (int32_t)gr32(DEMO_CONTEXT + 0x7ec);
    assert(clock == 10);
    for (unsigned k = 0; k < 6; k++) {
        const int32_t next = demo_tick_call(DEMO_OBJECT, 16666667);
        assert(next - clock == (k < 4 ? 2 : 1));
        clock = next;
    }
    /* a hold adds nothing and clears the carry: after it, 1 a call at 60 again */
    demo_tick_reset(0);
    (void)demo_tick_call(DEMO_OBJECT, 100000000);
    demo_game_hold = 1;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 2);
    demo_game_hold = 0;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 3);
    /* a carry from a 100 ms frame is gone when the same controller, same context, is stepped again after more than a
       second: 1 at 60, as from a fresh budget. Within the second it stays (2, as above). */
    demo_tick_reset(0);
    (void)demo_tick_call(DEMO_OBJECT, 100000000);
    host_ns += 1000000001;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 3);
    demo_tick_reset(0);
    (void)demo_tick_call(DEMO_OBJECT, 100000000);
    host_ns += 1000000000;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 4);
    /* a negative clock gives 0 + n; a change other than +2 is left as the game made it; another state is untouched */
    demo_tick_reset(-1);
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 1);
    demo_game_other = 1;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 77);
    demo_game_other = 0;
    gw32(DEMO_OBJECT + 0x3b8, 4); /* the skip state steps the same way */
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 78);
    gw32(DEMO_OBJECT + 0x3b8, 5);
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 78);
    gw32(DEMO_OBJECT + 0x3b8, 3);
    /* a full table of controllers stepped in the last second: that controller keeps the game's +2, counted; a reused
       pointer with another context is fresh */
    demo_tick_reset(0);
    for (unsigned k = 0; k < DEMO_TICK_CONTROLLERS; k++)
        demo_tick_states[k] = (DemoTickState){.object = 3000 + k, .context = 1, .seen_ns = host_ns - 1000000000};
    const u64 full = demo_tick_full;
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 2 && demo_tick_full == full + 1);
    /* one of them not stepped for more than a second is gone: the new controller takes its state, paced from its first
       call; the others keep theirs */
    demo_tick_reset(0);
    host_ns += 2000000000; /* so that a second before it is not below 0 */
    for (unsigned k = 0; k < DEMO_TICK_CONTROLLERS; k++)
        demo_tick_states[k] = (DemoTickState){.object = 3000 + k, .context = 1, .budget = DEMO_TICK_UNIT,
                                              .seen_ns = host_ns - (k == 5 ? 1000000001 : 1000000000)};
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 1 && demo_tick_full == full + 1);
    assert(demo_tick_states[5].object == DEMO_OBJECT && demo_tick_states[5].seen_ns == host_ns);
    for (unsigned k = 0; k < DEMO_TICK_CONTROLLERS; k++)
        if (k != 5) assert(demo_tick_states[k].object == 3000 + k && demo_tick_states[k].budget == DEMO_TICK_UNIT);
    /* a controller playing for 1.5 s beside 7 others keeps its state: a new one then finds the table full */
    demo_tick_reset(0);
    for (unsigned k = 1; k < DEMO_TICK_CONTROLLERS; k++)
        demo_tick_states[k] = (DemoTickState){.object = 3000 + k, .context = 1};
    for (unsigned k = 1; k <= 90; k++) {
        host_ns += 16666667;
        for (unsigned j = 1; j < DEMO_TICK_CONTROLLERS; j++) demo_tick_states[j].seen_ns = host_ns;
        assert(demo_tick_call(DEMO_OBJECT, 16666667) == (int32_t)k);
    }
    gw64(1200 + 0x3b0, DEMO_CONTEXT);
    gw32(1200 + 0x3b8, 3);
    assert(demo_tick_call(1200, 16666667) == 92 && demo_tick_full == full + 2);
    assert(demo_tick_states[0].object == DEMO_OBJECT);
    /* states stepped by another thread a few ns after this one read its clock are live, not a second old (the
       reviewer's control on cc604bdb): the table is full and the carried budget stays */
    demo_tick_reset(0);
    for (unsigned k = 0; k < DEMO_TICK_CONTROLLERS; k++)
        demo_tick_states[k] =
            (DemoTickState){.object = 3000 + k, .context = 1, .budget = 12345, .seen_ns = host_ns + 5};
    assert(!demo_tick_state(DEMO_OBJECT, DEMO_CONTEXT, host_ns) && demo_tick_states[0].budget == 12345);
    assert(!past_second(host_ns, host_ns + 5) && !past_second(host_ns, host_ns - 1000000000) &&
           past_second(host_ns, host_ns - 1000000001));
    /* 20 controllers one after another, each playing 30 calls: every one is paced, the table never full */
    demo_tick_reset(0);
    for (unsigned n = 0; n < 20; n++) {
        const u64 controller = 1200 + 16 * n; /* its +3B0 and +3B8 below the context's clock */
        gw64(controller + 0x3b0, DEMO_CONTEXT);
        gw32(controller + 0x3b8, 3);
        gw32(DEMO_CONTEXT + 0x7ec, 0);
        for (unsigned k = 1; k <= 30; k++) {
            host_ns += 16666667;
            assert(demo_tick_call(controller, 16666667) == (int32_t)k);
        }
        host_ns += 1000000000;
    }
    assert(demo_tick_full == full + 2);
    demo_tick_reset(0);
    demo_tick_states[0] = (DemoTickState){.object = DEMO_OBJECT, .context = 999, .budget = 3 * DEMO_TICK_UNIT};
    assert(demo_tick_call(DEMO_OBJECT, 16666667) == 1 && demo_tick_states[0].context == DEMO_CONTEXT);
    /* inside the demo probe the probe reads the clock as the tick left it */
    demo_tick_reset(0);
    demo_probe_original = demo_tick_step;
    demo_probe_ns = demo_probe_object = 0;
    CPU c = {.x = {[0] = DEMO_OBJECT, [1] = 9, [30] = return_pc}, .pc = entry_pc};
    step.step = 16666667;
    demo_probe_step(&c, entry_pc);
    assert(gr32(DEMO_CONTEXT + 0x7ec) == 1 && demo_probe_clock == 1);
    demo_probe_original = NULL;
    /* with the demo tick alone the frames are counted too, and its line gives their rate (the reviewer's note on
       69038158) */
    {
        const int probes[2] = {event_probe, demo_probe}, ticked = demo_tick;
        event_probe = demo_probe = 0;
        demo_tick = 0;
        assert(!probe_frames_wanted());
        demo_tick = 1;
        assert(probe_frames_wanted());
        demo_tick_reset(0);
        demo_tick_ns = 0;
        logged_count = 0;
        for (unsigned k = 0; k < 330; k++) {
            host_ns += 16666667;
            if (probe_frames_wanted()) probe_frame();
            (void)demo_tick_call(DEMO_OBJECT, 16666667);
        }
        assert(logged_count == 1 && strstr(logged[0], "arceus-demo-tick: seconds=5.00 calls_per_s=60.00 "
                                                      "units_per_s=60.00 frames_per_s=60.00 held="));
        event_probe = probes[0];
        demo_probe = probes[1];
        demo_tick = ticked;
        logged_count = 0;
    }
    /* with the demo probe alone, the frame count runs and no event probe line comes (the reviewer's note) */
    const int event = event_probe;
    event_probe = 0;
    event_probe_ns = 0;
    logged_count = 0;
    for (unsigned k = 0; k < 10; k++) {
        host_ns += 1000000000;
        probe_frame();
    }
    assert(!logged_count);
    event_probe = event;
    demo_tick_original = NULL;
    step.step = ns;
    memset(memory, 0, sizeof memory);
}
/* The text probe on its real wrappers: a stand-in reveal does a set number of glyph steps through the step's wrapper,
   as the game's loop does (return address FBFC24, the status word on its stack), and answers 1 while text remains */
#define TEXT_OBJECT 512
#define TEXT_STATUS 1800
static unsigned text_stub_steps, text_stub_answer, text_stub_status;
static u64 text_stub_caller;
static void text_glyph_stub(CPU* c, u64 entry) {
    (void)entry;
    gw32(c->x[1], text_stub_status);
    c->x[0] = 1;
    c->pc = c->x[30];
}
static void text_reveal_stub(CPU* c, u64 entry) {
    assert(entry == entry_pc && c->x[0] == TEXT_OBJECT);
    for (unsigned k = 0; k < text_stub_steps; k++) {
        CPU s = {.x = {[0] = TEXT_OBJECT, [1] = TEXT_STATUS, [30] = text_stub_caller}, .pc = entry_pc};
        text_probe_step(&s, entry_pc);
        assert(s.pc == text_stub_caller);
    }
    c->x[0] = text_stub_answer;
    c->pc = c->x[30];
}
/* The outer text: 2 glyph steps, a reveal of TEXT_INNER (4 steps), then 1 more step */
#define TEXT_INNER 1200
static void text_nest_stub(CPU* c, u64 entry) {
    const u64 text = c->x[0];
    const unsigned before = text == TEXT_INNER ? 4 : 2;
    for (unsigned k = 0; k < before; k++) {
        CPU s = {.x = {[0] = text, [1] = TEXT_STATUS, [30] = text_stub_caller}, .pc = entry_pc};
        text_probe_step(&s, entry_pc);
    }
    if (text != TEXT_INNER) {
        CPU n = {.x = {[0] = TEXT_INNER, [30] = return_pc}, .pc = entry_pc};
        text_probe_reveal(&n, entry);
        assert(text_probe_running == text);
        CPU s = {.x = {[0] = text, [1] = TEXT_STATUS, [30] = text_stub_caller}, .pc = entry_pc};
        text_probe_step(&s, entry_pc);
    }
    c->x[0] = 1;
    c->pc = c->x[30];
}
static void text_setup_stub(CPU* c, u64 entry) {
    (void)entry;
    *GP(c->x[0] + 0x9d) = *GP(c->x[0] + 0xa5) = 0;
    *GP(c->x[0] + 0xa4) = *GP(c->x[0] + 0xa6) = 4;
    c->pc = c->x[30];
}
static void test_text_probe(void) {
    memset(memory, 0, sizeof memory);
    const u64 ns = host_ns, frames = event_probe_frames;
    const int clocks = clocks_installed;
    clocks_installed = 1;
    text_probe_base = 3000 - 0x40f0138ull; /* the speed int at 3000 */
    gw32(3000, 2);
    *GP(TEXT_OBJECT + 0x9d) = 0;
    *GP(TEXT_OBJECT + 0xa4) = 2;
    text_probe_original[0] = text_reveal_stub;
    text_probe_original[1] = text_glyph_stub;
    memset(text_probe_texts, 0, sizeof text_probe_texts);
    text_probe_calls = text_probe_steps = text_probe_ns = 0;
    text_probe_seen = 0;
    text_probe_option = -1; /* as installed with the options word not readable (here: outside the test memory) */
    text_stub_caller = text_probe_base + 0xfbfc24;
    text_stub_status = 0;
    text_stub_answer = 1;
    logged_count = 0;
    /* 60 calls a second with 2 steps each for 6 s, then the text ends */
    for (unsigned k = 0; k < 360; k++) {
        host_ns += 16666667;
        event_probe_frames++;
        text_stub_steps = 2;
        if (k == 359) text_stub_answer = 0;
        CPU c = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
        text_probe_reveal(&c, entry_pc);
        assert(c.pc == return_pc && c.x[0] == (k == 359 ? 0u : 1u));
    }
    assert(logged_count == 3);
    assert(strstr(logged[0], "arceus-text-probe: text 200 first seen: +9D=0 +A4=2 +A5=0 +A6=0"));
    assert(strstr(logged[1], "arceus-text-probe: seconds=5.00 reveal_fbfbd0_per_s=60.00 glyph_steps_per_s=120.00 "
                             "frames_per_s=60.00 texts=1 speed=2 ") &&
           strstr(logged[1], " +9D=0 +A4=2 ") && strstr(logged[1], " live(+9D/+A4/+A6)=200:0/2/0\n"));
    assert(strstr(logged[2], "arceus-text-probe: text 200 ended: calls=360 glyph_steps=720 ms=5983.3"));
    /* steps from another caller, with a status other than 0, or outside a reveal are not counted */
    const u64 counted = text_probe_steps;
    text_stub_answer = 1;
    text_stub_caller = text_probe_base + 0x2b31568;
    CPU c = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
    text_probe_reveal(&c, entry_pc);
    text_stub_caller = text_probe_base + 0xfbfc24;
    text_stub_status = 1;
    CPU d = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
    text_probe_reveal(&d, entry_pc);
    text_stub_status = 0;
    CPU s = {.x = {[0] = TEXT_OBJECT, [1] = TEXT_STATUS, [30] = text_stub_caller}, .pc = entry_pc};
    text_probe_step(&s, entry_pc);
    assert(text_probe_steps == counted);
    /* a change of the bytes between two calls gives one line, with the call and the caller */
    logged_count = 0;
    text_probe_change_lines = 0;
    *GP(TEXT_OBJECT + 0xa4) = 4;
    CPU v = {.x = {[0] = TEXT_OBJECT, [30] = text_probe_base + 0x27379f0}, .pc = entry_pc};
    host_ns += 16666667;
    text_probe_reveal(&v, entry_pc);
    CPU w = {.x = {[0] = TEXT_OBJECT, [30] = text_probe_base + 0x27379f0}, .pc = entry_pc};
    host_ns += 16666667;
    text_probe_reveal(&w, entry_pc);
    assert(logged_count == 1 && strstr(logged[0], "arceus-text-probe: text 200 bytes changed at call ") &&
           strstr(logged[0], ": +9D 0 -> 0, +A4 2 -> 4, +A6 0 -> 0 (FBFBD0 called from main+27379f0)"));
    /* the options word [[[main+4279560]]+230]+68: its bits 0-1 at install and on each change, polled each frame */
    {
        const u64 probe_base = text_probe_base;
        text_probe_base = 2000 - 0x4279560ull;
        gw64(2000, 2100);
        gw64(2100, 2200);
        gw64(2200 + 0x230, 2600);
        gw32(2600 + 0x68, 0x101);
        text_probe_option = -2;
        logged_count = 0;
        text_probe_option_poll(host_ns);
        text_probe_frame();
        gw32(2600 + 0x68, 0x102);
        host_ns += 2000000000;
        text_probe_frame();
        gw32(2600 + 0x68, 0x106); /* bit 2 is not the option's */
        text_probe_frame();
        assert(logged_count == 2 &&
               strstr(logged[0], "arceus-text-probe: options word bits 0-1 at install -1 -> 1 at ") &&
               strstr(logged[1], "arceus-text-probe: options word bits 0-1 1 -> 2 at ") &&
               strstr(logged[1], " (2.000 s after the previous)"));
        gw64(2200 + 0x230, 0);
        text_probe_frame();
        assert(logged_count == 3 && strstr(logged[2], "bits 0-1 2 -> -1 "));
        text_probe_base = probe_base;
        memset(memory + 2000, 0, 1100);
    }
    /* the setup FBF57C: its arguments and the language byte before, the bytes it set after, the game's call once */
    logged_count = 0;
    text_probe_setup_lines = 0;
    text_probe_setup_original = text_setup_stub;
    CPU u = {.x = {[0] = TEXT_OBJECT, [1] = 1, [2] = 0, [30] = return_pc}, .pc = entry_pc};
    text_probe_setup(&u, entry_pc);
    assert(u.pc == return_pc && logged_count == 1);
    assert(strstr(logged[0],
                  "arceus-text-probe: setup FBF57C text=200 w1=1 w2=0 language=255: +9D=0 +A4=4 +A5=0 +A6=4"));
    /* a text not seen for more than a second starts a new record in its old slot: no calls or steps carried */
    text_probe_original[0] = text_reveal_stub;
    text_stub_steps = 2;
    text_stub_answer = 1;
    for (unsigned k = 0; k < 2; k++) { /* the options' 2 s above retired the record: these two make a new one */
        host_ns += 16666667;
        CPU e = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
        text_probe_reveal(&e, entry_pc);
    }
    TextProbeText* tp = NULL;
    for (unsigned k = 0; k < TEXT_PROBE_TEXTS; k++)
        if (text_probe_texts[k].text == TEXT_OBJECT) tp = &text_probe_texts[k];
    assert(tp && tp->calls == 2 && tp->steps == 4);
    const unsigned seen = text_probe_seen;
    host_ns += 1500000000;
    CPU g = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
    text_probe_reveal(&g, entry_pc);
    assert(tp->text == TEXT_OBJECT && tp->calls == 1 && tp->steps == 2 && tp->first_ns == host_ns);
    assert(text_probe_seen == seen + 1);
    /* a record another thread refreshed a few ns after this one read its clock is live: found as it is */
    tp->last_ns = host_ns + 5;
    assert(text_probe_text(TEXT_OBJECT, host_ns) == tp && tp->calls == 1 && tp->last_ns == host_ns);
    /* 16 records of other texts, one not seen for more than a second: a new text takes that one */
    {
        TextProbeText saved[TEXT_PROBE_TEXTS];
        memcpy(saved, text_probe_texts, sizeof saved);
        for (unsigned k = 0; k < TEXT_PROBE_TEXTS; k++)
            text_probe_texts[k] = (TextProbeText){.text = 0x8000 + k, .last_ns = host_ns - (k == 11 ? 1000000001 : 0)};
        assert(text_probe_text(0x9000, host_ns) == &text_probe_texts[11] && text_probe_texts[11].text == 0x9000);
        text_probe_texts[11].last_ns = host_ns;
        assert(!text_probe_text(0x9200, host_ns));
        memcpy(text_probe_texts, saved, sizeof saved);
    }
    /* a reveal of another text inside a reveal: each text gets its own glyph steps, the total all of them */
    text_probe_original[0] = text_nest_stub;
    const u64 total = text_probe_steps;
    host_ns += 16666667;
    CPU o = {.x = {[0] = TEXT_OBJECT, [30] = return_pc}, .pc = entry_pc};
    text_probe_reveal(&o, entry_pc);
    assert(o.pc == return_pc && text_probe_running == 0 && text_probe_own == 0);
    TextProbeText* inner = NULL;
    for (unsigned k = 0; k < TEXT_PROBE_TEXTS; k++)
        if (text_probe_texts[k].text == TEXT_INNER) inner = &text_probe_texts[k];
    assert(tp->calls == 2 && tp->steps == 2 + 3 && inner && inner->calls == 1 && inner->steps == 4);
    assert(text_probe_steps == total + 7);
    text_probe_setup_original = NULL;
    text_probe_original[0] = text_probe_original[1] = NULL;
    text_probe_base = 0;
    clocks_installed = clocks;
    host_ns = ns;
    event_probe_frames = frames;
    logged_count = 0;
    memset(memory, 0, sizeof memory);
}
/* The text probe's install with a failed compile, at either site: both stock words back (the text tick's guards read
   them), each changed range invalidated, nothing published (g_disp is NULL here). The two sites lie 28 MB apart, so
   this test has a guest space of its own. */
static void test_text_probe_install(void) {
    u8* const saved_mem = g_mem;
    const u64 saved_size = g_as_size, saved_lo = g_code_lo, saved_hi = g_code_hi;
    const SnxModule* const saved_mod = g_mod;
    GuestFn* const saved_disp = g_disp;
    const u64 pc[2] = {0x1000, 0x1000 + 0x2b315e8 - 0xfbfbd0}, base = pc[0] - 0xfbfbd0;
    g_as_size = pc[1] + 0x1000;
    g_mem = calloc(1, g_as_size);
    assert(g_mem);
    static const u32 words[2][5][2] = {
        {{0, 0xd100c3ff}, {0x10, 0xaa0003f3}, {0x24, 0xb9418a68}, {0x12c, 0x7100053f}, {0x158, 0x17ffffe7}},
        {{0, 0xfc1b0fe8}, {0x3c, 0xaa0103f4}, {0x18, 0xb900003f}, {0x868, 0xd65f03c0}, {0x18, 0xb900003f}},
    };
    for (unsigned k = 0; k < 2; k++)
        for (unsigned w = 0; w < 5; w++) gw32(pc[k] + words[k][w][0], words[k][w][1]);
    static const u64 fn_lo[2] = {0x1000, 0x1000 + 0x2b315e8 - 0xfbfbd0};
    static const u32 fn_len[2] = {0x15c, 0x9c4};
    static u8 stale[2];
    static const GuestFn entries[1] = {text_glyph_stub};
    const SnxModule module = {.nfns = 2, .fn_lo = fn_lo, .fn_len = fn_len, .stale = stale, .entry_fn = entries};
    g_mod = &module;
    g_code_lo = pc[0];
    g_code_hi = pc[1] + 0x9c4;
    g_disp = NULL;
    u8 before[2][0x9c4];
    for (unsigned k = 0; k < 2; k++) memcpy(before[k], GP(pc[k]), sizeof before[k]);
    flag("SNX_ARCEUS_TEXT_PROBE", "1");
    jit_stub_on = 1;
    for (unsigned failed = 0; failed < 2; failed++) {
        jit_bodies[0] = failed ? text_glyph_stub : NULL;
        jit_bodies[1] = NULL;
        jit_waits = jit_changes = 0;
        text_probe_init(base);
        assert(!text_probe && !text_probe_base && !text_probe_original[0] && !text_probe_original[1]);
        for (unsigned k = 0; k < 2; k++) assert(!memcmp(before[k], GP(pc[k]), sizeof before[k]));
        /* the compiles in order, then one change for each site put back */
        assert(jit_waits == failed + 1 && jit_changes == 2 * (failed + 1));
        if (!failed) assert(jit_changed[0] == pc[0] && jit_changed[1] == pc[0]);
        else assert(jit_changed[0] == pc[0] && jit_changed[1] == pc[1] && jit_changed[2] == pc[0] &&
                    jit_changed[3] == pc[1]);
    }
    jit_stub_on = 0;
    unset_flag("SNX_ARCEUS_TEXT_PROBE");
    free(g_mem);
    g_mem = saved_mem;
    g_as_size = saved_size;
    g_code_lo = saved_lo;
    g_code_hi = saved_hi;
    g_mod = saved_mod;
    g_disp = saved_disp;
}
/* The demo and event probes' installs with a failed compile: the stock word back (the demo and event ticks' guards
   read it), the range invalidated again, nothing installed. Each in a guest space of its own. */
static void test_probe_compile_restore(void) {
    u8* const saved_mem = g_mem;
    const u64 saved_size = g_as_size, saved_lo = g_code_lo, saved_hi = g_code_hi, saved_base = main_base;
    const SnxModule* const saved_mod = g_mod;
    GuestFn* const saved_disp = g_disp;
    static u8 stale[1];
    static const GuestFn entries[1] = {text_glyph_stub};
    jit_stub_on = 1;
    /* the demo probe: the census's B at +1D8 */
    {
        const u64 pc = 0x1000, size = 0x1154, base = pc - 0x1458300;
        g_as_size = pc + size + 0x1000;
        g_mem = calloc(1, g_as_size);
        assert(g_mem);
        gw32(pc, 0xd10343ff);
        gw32(pc + 0x1d8, 0x14000069);
        static u64 fn_lo[1];
        static u32 fn_len[1];
        fn_lo[0] = pc;
        fn_len[0] = (u32)size;
        const SnxModule module = {.nfns = 1, .fn_lo = fn_lo, .fn_len = fn_len, .stale = stale, .entry_fn = entries};
        g_mod = &module;
        g_code_lo = pc;
        g_code_hi = pc + size;
        g_disp = NULL;
        jit_bodies[0] = jit_bodies[1] = NULL;
        jit_waits = jit_changes = 0;
        demo_probe = 0;
        flag("SNX_ARCEUS_DEMO_PROBE", "1");
        demo_probe_init(base);
        unset_flag("SNX_ARCEUS_DEMO_PROBE");
        assert(!demo_probe && !demo_probe_original && gr32(pc + 0x1d8) == 0x14000069);
        assert(jit_waits == 1 && jit_changes == 2 && jit_changed[0] == pc && jit_changed[1] == pc);
        free(g_mem);
    }
    /* the event probe: the job's MOV at +A4; the resume's site has no stock words, so its guard refuses it */
    {
        const u64 pc = 0x1000, base = pc - 0x15ba460;
        g_as_size = pc + 0x2000;
        g_mem = calloc(1, g_as_size);
        assert(g_mem);
        gw32(pc, 0xd10703ff);
        gw32(pc + 0xa4, 0xaa0003f3);
        gw32(pc + 0x134, 0xd65f03c0);
        static u64 fn_lo[1];
        static u32 fn_len[1];
        fn_lo[0] = pc;
        fn_len[0] = 0x138;
        const SnxModule module = {.nfns = 1, .fn_lo = fn_lo, .fn_len = fn_len, .stale = stale, .entry_fn = entries};
        g_mod = &module;
        g_code_lo = pc;
        g_code_hi = pc + 0x1000;
        g_disp = NULL;
        main_base = base;
        jit_bodies[0] = jit_bodies[1] = NULL;
        jit_waits = jit_changes = 0;
        event_probe = 0;
        event_probe_original[0] = event_probe_original[1] = NULL;
        flag("SNX_ARCEUS_EVENT_PROBE", "1");
        event_probe_init();
        unset_flag("SNX_ARCEUS_EVENT_PROBE");
        assert(!event_probe && !event_probe_original[0] && !event_probe_original[1] && gr32(pc + 0xa4) == 0xaa0003f3);
        assert(jit_waits == 1 && jit_changes == 2 && jit_changed[0] == pc && jit_changed[1] == pc);
        free(g_mem);
    }
    jit_stub_on = 0;
    logged_count = 0;
    g_mem = saved_mem;
    g_as_size = saved_size;
    g_code_lo = saved_lo;
    g_code_hi = saved_hi;
    main_base = saved_base;
    g_mod = saved_mod;
    g_disp = saved_disp;
}
/* TEXT_TICK on its real wrappers: a stand-in reveal with the game's do-while loop (the step from FBFC24, counted while
   it answers 1 with status 0, up to +A4), and a stand-in step that counts the delay in +9E, moves the cursor +100 and
   the state +A8, as a glyph does */
#define TT_A 512
#define TT_B 1024
#define TT_STATUS 1800
static unsigned tt_answer;
static void tt_step(CPU* c, u64 entry) {
    (void)entry;
    const u64 t = c->x[0];
    gw32(c->x[1], 0);
    *GP(t + 0x9e) = (u8)(*GP(t + 0x9e) + 1);
    gw32(t + 0x100, gr32(t + 0x100) + 1);
    gw32(t + 0xa8, 3);
    c->x[0] = 1;
    c->pc = c->x[30];
}
static void tt_reveal(CPU* c, u64 entry) {
    const u64 t = c->x[0];
    unsigned n = 0;
    do {
        CPU s = {.x = {[0] = t, [1] = TT_STATUS, [30] = main_base + 0xfbfc24}, .pc = entry};
        text_tick_step(&s, entry);
        assert(s.pc == main_base + 0xfbfc24 && gr32(TT_STATUS) == 0 && s.x[0] == 1);
    } while (++n < *GP(t + 0xa4));
    c->x[0] = tt_answer;
    c->pc = c->x[30];
}
/* A stand-in reveal that first reveals tt_inner through the wrapper (once, not inside itself), then runs its own loop */
static u64 tt_inner;
static unsigned tt_nesting;
static void tt_reveal_nest(CPU* c, u64 entry) {
    if (tt_inner && !tt_nesting) {
        tt_nesting++;
        CPU n = {.x = {[0] = tt_inner, [30] = return_pc}, .pc = entry};
        text_tick_reveal(&n, entry);
        assert(n.pc == return_pc);
        tt_nesting--;
    }
    tt_reveal(c, entry);
}
/* one call of T's reveal; the glyphs it made */
static unsigned tt_call(u64 t) {
    const u32 before = gr32(t + 0x100);
    CPU c = {.x = {[0] = t, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&c, entry_pc);
    assert(c.pc == return_pc);
    return gr32(t + 0x100) - before;
}
static void tt_reset(void) {
    memset(text_tick_states, 0, sizeof text_tick_states);
    for (unsigned k = 0; k < 2; k++) {
        const u64 t = k ? TT_B : TT_A;
        memset(memory + t, 0, 0x110);
        gw64(t + 0x78, 0x7000 + k);
        *GP(t + 0xa4) = 2;
    }
    tt_answer = 1;
}
static void tt_frame(u64 ns) {
    step.step = ns;
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
}
/* The waiting text's step (TEXT_TICK_END): a stand-in reveal with FBFBD0's loop as the game has it. Status 1 ends it,
   another non-zero status loops uncounted, status 0 with answer 1 counts to +A4, status 0 with another answer ends it,
   and the reveal answers the last answer. A step for a shown text (+A8 and +BC 0) answers tt_shown and writes no T */
static u32 tt_shown;
static unsigned tt_shown_steps;
static void tt_step_shown(CPU* c, u64 entry) {
    (void)entry;
    gw32(c->x[1], 0);
    c->x[0] = tt_shown;
    c->x[30] = 0xdead; /* a body may leave X30 changed: the wrapper keeps the return it was called with */
    tt_shown_steps++;
}
static void tt_reveal_game(CPU* c, u64 entry) {
    const u64 t = c->x[0];
    unsigned n = 0;
    u64 answer;
    for (;;) {
        CPU s = {.x = {[0] = t, [1] = TT_STATUS, [30] = main_base + 0xfbfc24}, .pc = entry};
        text_tick_step(&s, entry);
        answer = s.x[0];
        const u32 status = gr32(TT_STATUS);
        if (status == 1) break;
        if (status) continue;
        if (answer != 1 || ++n >= *GP(t + 0xa4)) break;
    }
    c->x[0] = answer;
    c->pc = c->x[30];
}
/* eight frames at 120 FPS of T's reveal: its answers, one digit a frame */
static void tt_end_frames(char out[9]) {
    for (unsigned k = 0; k < 8; k++) {
        tt_frame(8333334);
        CPU c = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
        text_tick_reveal(&c, entry_pc);
        out[k] = (char)('0' + c.x[0]);
    }
    out[8] = 0;
}
static void test_text_tick_end(void) {
    char seen[9];
    memset(memory, 0, sizeof memory);
    const u64 ns = step.step, frames = frame_step_frames, base = main_base;
    main_base = 0x10000;
    text_tick_original[0] = tt_reveal_game;
    text_tick_original[1] = tt_step_shown;
    /* the key: on when unset, off for 0 only, and the totals start at 0 */
    const char* const values[7] = {NULL, "0", "1", "3", "", "2", "00"};
    const int forms[7] = {3, 0, 3, 3, 3, 3, 3};
    for (unsigned k = 0; k < 7; k++) {
        if (values[k]) flag("SNX_ARCEUS_TEXT_TICK_END", values[k]);
        else unset_flag("SNX_ARCEUS_TEXT_TICK_END");
        text_tick_end_passed = text_tick_end_held = text_tick_end_kept_back = 9;
        text_tick_end_read();
        assert(text_tick_end == forms[k] && !text_tick_end_passed && !text_tick_end_held && !text_tick_end_kept_back);
    }
    unset_flag("SNX_ARCEUS_TEXT_TICK_END");
    /* 0, a shown text waiting (the game answers 0): the skipped frames answer 1, the 0 and 1 of the stuck box */
    tt_reset();
    tt_shown = 0, tt_shown_steps = 0, text_tick_end = 0;
    tt_end_frames(seen);
    assert(!strcmp(seen, "01110111") && tt_shown_steps == 2);
    /* 3: every wait runs the game's step on every frame, a revealing text is skipped as today */
    text_tick_end = 3, text_tick_end_passed = 0;
    const struct { u32 answer; const char* seen; unsigned steps; } waits[4] = {
        {0, "00000000", 8}, /* the end-of-text wait (the save box) */
        {2, "22222222", 8}, /* a page break */
        {4, "44444444", 8}, /* closed */
        {1, "11111111", 4}, /* revealing: the ticks only, two steps each (+A4 2) */
    };
    for (unsigned k = 0; k < 4; k++) {
        tt_reset();
        tt_shown = waits[k].answer, tt_shown_steps = 0;
        tt_end_frames(seen);
        assert(!strcmp(seen, waits[k].seen) && tt_shown_steps == waits[k].steps);
    }
    assert(text_tick_end_passed == 3 * 6);
    /* 3, a wait that ends: once the step answers 1 again the frames after it are skipped */
    tt_reset();
    tt_shown = 2, tt_shown_steps = 0;
    tt_frame(8333334);
    CPU w = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&w, entry_pc);
    assert(w.x[0] == 2 && tt_shown_steps == 1);
    tt_shown = 1; /* the press: the next real step goes on revealing */
    tt_end_frames(seen);
    assert(!strcmp(seen, "11111111") && tt_shown_steps == 1 + 1 + 2 * 2); /* that frame's step, then 2 ticks of 2 */
    /* a real step after its reveal ended (no reveal running): its answer is not kept (the reviewer's N3), so a
       revealing text's next skipped frame is still skipped */
    tt_reset();
    tt_shown = 1, tt_shown_steps = 0;
    tt_frame(8333334);
    CPU r0 = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&r0, entry_pc);
    assert(r0.x[0] == 1 && tt_shown_steps == 2);
    tt_shown = 4;
    CPU r = {.x = {[0] = TT_A, [1] = TT_STATUS, [30] = main_base + 0xfbfc24}, .pc = entry_pc};
    text_tick_step(&r, entry_pc);
    assert(r.x[0] == 4 && tt_shown_steps == 3);
    tt_frame(8333334);
    CPU r1 = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&r1, entry_pc);
    assert(r1.x[0] == 1 && tt_shown_steps == 3);
    /* a skipped step of another T than the reveal's (A's state kept 0), or with no reveal running: 1 */
    tt_shown = 0;
    tt_reset();
    tt_end_frames(seen);
    TextTickState* a_state = NULL;
    for (unsigned k = 0; k < TEXT_TICK_TEXTS; k++)
        if (text_tick_states[k].text == TT_A) a_state = &text_tick_states[k];
    assert(a_state && a_state->answered && a_state->answer == 0);
    for (unsigned k = 0; k < 2; k++) {
        text_tick_current = k ? NULL : a_state;
        text_tick_skip = TT_B;
        CPU o = {.x = {[0] = TT_B, [1] = TT_STATUS, [30] = main_base + 0xfbfc24}, .pc = entry_pc};
        text_tick_step(&o, entry_pc);
        assert(o.x[0] == 1 && o.pc == main_base + 0xfbfc24);
    }
    /* A's kept answer is A's own: a real step of a nested B, or of A from outside the reveal loop, keeps nothing */
    text_tick_skip = 0, text_tick_current = a_state, tt_shown = 4;
    const u64 from[2][2] = {{TT_B, main_base + 0xfbfc24}, {TT_A, main_base + 0xfbfc28}};
    for (unsigned k = 0; k < 2; k++) {
        CPU n = {.x = {[0] = from[k][0], [1] = TT_STATUS, [30] = from[k][1]}, .pc = entry_pc};
        text_tick_step(&n, entry_pc);
        assert(n.x[0] == 4 && a_state->answer == 0);
    }
    /* the step's own test leaves on a count past the threshold, also at 255 where the stored +1 would wrap */
    assert(text_auto_completes((TextAuto){.count = 255, .threshold = 37}));
    assert(!text_auto_completes((TextAuto){.count = 5, .threshold = 37}));
    text_tick_skip = 0, text_tick_current = NULL;
    text_tick_end = 0;
    text_tick_original[0] = text_tick_original[1] = NULL;
    memset(text_tick_states, 0, sizeof text_tick_states);
    main_base = base, step.step = ns, frame_step_frames = frames;
    memset(memory, 0, sizeof memory);
}
/* The hold: a step with the game's waits. A press (tt_press) leaves. In the end wait (+A8 0) modes 2 and 3 count +A3
   toward +A2 as 2B31D44 does, or store tt_count when set. Leaving sets tt_left and answers 4, the end wait 0, a page
   break (+A8 2) 2, and +BC set 0 with no count (2B31634). tt_mode, when set, goes to +90 with a count (a new setup).
   The count leaves when +A3 > +A2, else it stores +A3 + 1 and leaves when +A2 is below that */
static int tt_press, tt_left, tt_count = -1, tt_mode = -1;
static void tt_step_auto(CPU* c, u64 entry) {
    (void)entry;
    const u64 t = c->x[0];
    gw32(c->x[1], 0);
    c->x[30] = 0xdead;
    const u32 mode = gr32(t + 0x90), state = gr32(t + 0xa8);
    int leave = tt_press;
    if (!leave && !state && !gr32(t + 0xbc) && (mode == 2 || mode == 3)) {
        const u8 count = *GP(t + 0xa3), threshold = *GP(t + 0xa2);
        if (tt_count >= 0) *GP(t + 0xa3) = (u8)tt_count;
        else if (tt_mode >= 0) *GP(t + 0xa3) = (u8)(count + 1), gw32(t + 0x90, (u32)tt_mode);
        else if (count > threshold) leave = 1;
        else {
            *GP(t + 0xa3) = (u8)(count + 1);
            leave = threshold < (u8)(count + 1);
        }
    }
    if (leave) tt_left = 1, gw32(t + 0xa8, 9);
    c->x[0] = leave ? 4 : state == 2 ? 2 : 0;
}
/* frames at 120 FPS of a waiting T until it leaves (at most n): the frame it left on, -1 if it did not, and +A3 after
   each frame */
static int tt_auto_run(int end, u32 mode, u8 threshold, u8 count, int press_at, unsigned n, u8* counts) {
    tt_reset();
    text_tick_end = end, tt_left = 0;
    gw32(TT_A + 0x90, mode);
    *GP(TT_A + 0xa2) = threshold, *GP(TT_A + 0xa3) = count;
    for (unsigned k = 0; k < n; k++) {
        tt_frame(8333334);
        tt_press = (int)k == press_at;
        CPU c = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
        text_tick_reveal(&c, entry_pc);
        assert(c.pc == return_pc);
        if (counts) counts[k] = *GP(TT_A + 0xa3);
        if (tt_left) {
            tt_press = 0;
            return (int)k;
        }
    }
    tt_press = 0;
    return -1;
}
static void test_text_tick_hold(void) {
    memset(memory, 0, sizeof memory);
    const u64 ns = step.step, frames = frame_step_frames, base = main_base;
    main_base = 0x10000;
    text_tick_original[0] = tt_reveal_game;
    text_tick_original[1] = tt_step_auto;
    /* check (a): a mode 2 or 3 text leaves on the same frame with END=3 as with the tick alone, and +A3 is the same
       after every frame. Thresholds 0, 1, 2, 5, 37 and 255 (a byte that wraps: never), counts from 0 and past */
    const u8 thresholds[6] = {0, 1, 2, 5, 37, 255};
    for (u32 mode = 2; mode <= 3; mode++)
        for (unsigned k = 0; k < 6; k++)
            for (unsigned past = 0; past < 2; past++) {
                const u8 count = past ? (u8)(thresholds[k] == 255 ? 255 : thresholds[k] + 1) : 0;
                static u8 alone[1100], hold[1100]; /* 255 runs past the byte's wrap (256 ticks, 1024 frames) */
                const unsigned n = thresholds[k] == 255 ? 1100 : 200;
                memset(alone, 0, sizeof alone), memset(hold, 0, sizeof hold);
                const int a = tt_auto_run(0, mode, thresholds[k], count, -1, n, alone);
                text_tick_end_held = text_tick_end_kept_back = 0;
                const int h = tt_auto_run(3, mode, thresholds[k], count, -1, n, hold);
                assert(a == h && !memcmp(alone, hold, sizeof alone));
                const unsigned th = thresholds[k];
                if (th == 255) assert(a == -1); /* the step's byte wraps from 255 to 0 */
                else assert(a == (past ? 0 : 4 * (int)th)); /* the tick on frame 4 x +A2 leaves */
                /* three extra frames a tick are held, and the three before the leaving tick are kept back: there the
                   loop counts the skipped step's 1 up to +A4 (2), so 6 calls */
                if (!past)
                    assert(th == 255 ? text_tick_end_held == 825 && !text_tick_end_kept_back
                                     : text_tick_end_held == (th ? 3 * (th - 1) : 0) &&
                                           text_tick_end_kept_back == (th ? 6u : 0));
            }
    /* a press on any frame of the wait is seen on that frame with END=3: modes 0 and 1 (no count), and a counting
       mode 2 far from its end. With the tick alone only on a tick frame (every fourth) */
    const u32 modes[3] = {0, 1, 2};
    for (unsigned m = 0; m < 3; m++)
        for (int k = 1; k < 8; k++) {
            assert(tt_auto_run(3, modes[m], 200, 0, k, 8, NULL) == k);
            assert(tt_auto_run(0, modes[m], 200, 0, k, 8, NULL) == (k % 4 ? -1 : k));
        }
    /* a fresh count in the wait (the game writing +A3 itself, not +1) is kept, not put back */
    tt_auto_run(3, 2, 200, 0, -1, 1, NULL);
    tt_count = 9;
    tt_frame(8333334);
    CPU f = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&f, entry_pc);
    tt_count = -1;
    assert(*GP(TT_A + 0xa3) == 9);
    /* and a count of +1 that comes with another mode is not the same wait: kept too */
    tt_auto_run(3, 2, 200, 0, -1, 1, NULL);
    tt_mode = 3;
    tt_frame(8333334);
    CPU g = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
    text_tick_reveal(&g, entry_pc);
    tt_mode = -1;
    assert(*GP(TT_A + 0xa3) == 2 && gr32(TT_A + 0x90) == 3);
    /* a page break (+A8 2) of a mode 2 text is no auto-advance wait: a press there is seen on any frame with END=3,
       whatever +A3 and +A2 hold */
    for (int k = 1; k < 8; k++) {
        tt_reset();
        text_tick_end = 3, tt_left = 0;
        gw32(TT_A + 0x90, 2), gw32(TT_A + 0xa8, 2);
        int left = -1;
        for (int f8 = 0; f8 < 8 && left < 0; f8++) {
            tt_frame(8333334);
            tt_press = f8 == k;
            CPU p = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
            text_tick_reveal(&p, entry_pc);
            if (tt_left) left = f8;
        }
        tt_press = 0;
        assert(left == k);
    }
    /* +BC set in a mode 2 text with a count that would complete: no auto-advance wait, so nothing is held or kept
       back and a press on any frame is seen with END=3 */
    for (int k = 1; k < 8; k++) {
        tt_reset();
        text_tick_end = 3, tt_left = 0, text_tick_end_held = text_tick_end_kept_back = 0;
        gw32(TT_A + 0x90, 2), gw32(TT_A + 0xbc, 1);
        int left = -1;
        for (int f8 = 0; f8 < 8 && left < 0; f8++) {
            tt_frame(8333334);
            tt_press = f8 == k;
            CPU b = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
            text_tick_reveal(&b, entry_pc);
            if (tt_left) left = f8;
        }
        tt_press = 0;
        assert(left == k && !text_tick_end_held && !text_tick_end_kept_back && !*GP(TT_A + 0xa3));
    }
    /* 0: +A3 is the game's own, counted on the ticks only, never written back */
    u8 counts[8];
    text_tick_end_held = 0;
    tt_auto_run(0, 2, 200, 0, -1, 8, counts);
    assert(!memcmp(counts, "\1\1\1\1\2\2\2\2", 8) && !text_tick_end_held);
    text_tick_end = 0;
    text_tick_original[0] = text_tick_original[1] = NULL;
    memset(text_tick_states, 0, sizeof text_tick_states);
    main_base = base, step.step = ns, frame_step_frames = frames;
    memset(memory, 0, sizeof memory);
}
static void test_text_tick(void) {
    memset(memory, 0, sizeof memory);
    const u64 ns = step.step, frames = frame_step_frames, base = main_base;
    main_base = 0x10000;
    text_tick_original[0] = tt_reveal;
    text_tick_original[1] = tt_step;
    /* 30 FPS: every call a tick, 2 glyphs a call as the game */
    tt_reset();
    for (unsigned k = 0; k < 30; k++) {
        tt_frame(33333334);
        assert(tt_call(TT_A) == 2);
    }
    /* 60 FPS: a tick every other frame; on the other frames nothing of T moves, and the reveal still answers */
    tt_reset();
    for (unsigned k = 0; k < 12; k++) {
        tt_frame(16666667);
        u8 before[0x110];
        memcpy(before, memory + TT_A, sizeof before);
        const unsigned glyphs = tt_call(TT_A);
        assert(glyphs == (k % 2 ? 0u : 2u)); /* the first call a tick */
        if (!glyphs) assert(!memcmp(before, memory + TT_A, sizeof before));
    }
    /* two T a frame, each called twice in it: one decision a frame each, its budget fed once (research 13:50) */
    tt_reset();
    unsigned a = 0, b = 0;
    for (unsigned k = 0; k < 60; k++) {
        tt_frame(16666667);
        a += tt_call(TT_A) + tt_call(TT_A);
        b += tt_call(TT_B);
    }
    assert(a == 30 * 4 && b == 30 * 2);
    /* a fast finish (+A4 100) and the whole page (255) on a frame that is not a tick run the game's steps */
    tt_reset();
    tt_frame(16666667);
    (void)tt_call(TT_A);
    tt_frame(16666667);
    *GP(TT_A + 0xa4) = 100;
    assert(tt_call(TT_A) == 100);
    tt_frame(16666667);
    tt_frame(16666667);
    *GP(TT_A + 0xa4) = 255;
    assert(tt_call(TT_A) == 255);
    /* a step from another return address runs */
    tt_reset();
    tt_frame(16666667);
    (void)tt_call(TT_A);
    tt_frame(16666667);
    text_tick_skip = TT_A;
    CPU s = {.x = {[0] = TT_A, [1] = TT_STATUS, [30] = main_base + 0x2b31568}, .pc = entry_pc};
    text_tick_step(&s, entry_pc);
    text_tick_skip = 0;
    assert(gr32(TT_A + 0x100) == 3);
    /* a reveal inside another runs as today, without the outer one's skip (CodeRabbit on 47a62a26):
       (1) B nested in A on A's tick frame, in a frame that is not B's own tick, still makes its 2 glyphs */
    text_tick_original[0] = tt_reveal_nest;
    tt_reset();
    tt_inner = 0;
    tt_frame(16666667);
    assert(tt_call(TT_B) == 2);
    tt_frame(16666667);
    assert(tt_call(TT_B) == 0); /* B's own decision this frame: no tick */
    tt_inner = TT_B;
    u32 inner_before = gr32(TT_B + 0x100);
    assert(tt_call(TT_A) == 2 && gr32(TT_B + 0x100) - inner_before == 2);
    /* (2) A nested in A on a frame that is not A's tick: the inner reveal's glyphs run, the outer one's are skipped */
    tt_reset();
    tt_inner = 0;
    tt_frame(16666667);
    assert(tt_call(TT_A) == 2);
    tt_frame(16666667);
    tt_inner = TT_A;
    assert(tt_call(TT_A) == 2);
    assert(!text_tick_skip && !text_tick_depth);
    tt_inner = 0;
    text_tick_original[0] = tt_reveal;
    /* a T paused for a frame and resumed starts fresh, its first call a tick (where the old state would skip); one
       called in consecutive frames keeps its state */
    tt_reset();
    tt_frame(16666667);
    assert(tt_call(TT_A) == 2);
    tt_frame(16666667);
    assert(tt_call(TT_A) == 0);
    tt_frame(16666667);
    assert(tt_call(TT_A) == 2);
    tt_frame(16666667); /* not called in this frame */
    tt_frame(16666667);
    assert(tt_call(TT_A) == 2);
    tt_frame(16666667);
    assert(tt_call(TT_A) == 0);
    /* a new text at the same T ([T +78] changed) is fresh too */
    tt_frame(16666667);
    gw64(TT_A + 0x78, 0x7100);
    assert(tt_call(TT_A) == 2);
    /* 100 texts one after another, each shown 5 frames, beside a T shown in every frame: with 16 states the table
       never runs out (research's retirement ruling) */
    memset(text_tick_states, 0, sizeof text_tick_states);
    u64 frame = 1000;
    for (unsigned k = 0; k < 100; k++)
        for (unsigned f = 0; f < 5; f++, frame++) {
            assert(text_tick_state(TT_B, 1, frame));
            assert(text_tick_state(0x100000 + 0x200 * k, 1, frame));
        }
    /* a full table takes a state not seen in the previous frame either, never one seen then (it may be called later
       in this frame): 14 seen this frame, one in the previous frame, one before that */
    for (unsigned k = 0; k < TEXT_TICK_TEXTS; k++)
        text_tick_states[k] = (TextTickState){.text = 3000 + k, .generation = 1,
                                              .last = k == 7 ? frame - 1 : k == 9 ? frame - 2 : frame};
    TextTickState* taken = text_tick_state(0x200000, 1, frame);
    assert(taken == &text_tick_states[9] && taken->fresh && taken->text == 0x200000);
    assert(!text_tick_state(0x200200, 1, frame) && text_tick_states[7].text == 3000 + 7);
    /* 17 texts shown every frame, the one without a state called first: the 16 keep their states and budgets, and
       only the 17th reveals as today (the cascade CodeRabbit found on PR #52) */
    for (unsigned k = 0; k < TEXT_TICK_TEXTS; k++)
        text_tick_states[k] = (TextTickState){.text = 3000 + k, .generation = 1, .last = frame - 1};
    for (unsigned f = 0; f < 8; f++, frame++) {
        assert(!text_tick_state(0x300000, 1, frame));
        for (unsigned k = 0; k < TEXT_TICK_TEXTS; k++) {
            TextTickState* kept = text_tick_state(3000 + k, 1, frame);
            assert(kept == &text_tick_states[k] && !kept->fresh);
        }
    }
    /* every state seen this frame: the new T reveals as today, counted */
    tt_reset();
    tt_frame(16666667);
    const u64 now = __atomic_load_n(&frame_step_frames, __ATOMIC_ACQUIRE);
    for (unsigned k = 0; k < TEXT_TICK_TEXTS; k++)
        text_tick_states[k] = (TextTickState){.text = 3000 + k, .generation = 1, .last = now};
    const u64 full = text_tick_full;
    assert(tt_call(TT_A) == 2 && text_tick_full == full + 1);
    /* with the text probe: the reveal inside the probe's, the step outside it, as text_tick_init chains them, so the
       probe counts only the glyphs made (60 in 60 calls at 60 FPS: 30 ticks of 2) */
    tt_reset();
    text_probe_base = main_base;
    text_probe_original[0] = text_tick_reveal;
    text_tick_original[0] = tt_reveal;
    text_tick_original[1] = text_probe_step;
    text_probe_original[1] = tt_step;
    const u64 probed = text_probe_steps;
    for (unsigned k = 0; k < 60; k++) {
        tt_frame(16666667);
        CPU c = {.x = {[0] = TT_A, [30] = return_pc}, .pc = entry_pc};
        text_probe_reveal(&c, entry_pc);
    }
    assert(text_probe_steps - probed == 60 && gr32(TT_A + 0x100) == 60);
    text_probe_original[0] = text_probe_original[1] = NULL;
    text_probe_base = 0;
    text_tick_original[0] = text_tick_original[1] = NULL;
    memset(text_tick_states, 0, sizeof text_tick_states);
    main_base = base;
    step.step = ns;
    frame_step_frames = frames;
    memset(memory, 0, sizeof memory);
}
/* EVENT_TICK-2's decision on the measured step: runs over frame times, the budget never above two ticks */
static unsigned event_decide_runs(const u64* steps, unsigned count, unsigned repeat, char* pattern, u64* cut_total) {
    ScriptTick t = {0};
    unsigned runs = 0, n = 0;
    *cut_total = 0;
    for (unsigned r = 0; r < repeat; r++)
        for (unsigned k = 0; k < count; k++, n++) {
            u64 cut;
            const int run = event_tick_decide(&t, n + 1, steps[k], &cut);
            *cut_total += cut;
            assert(event_tick_decide(&t, n + 1, steps[k], &cut) == run && !cut); /* once a frame */
            assert(t.budget >= -SCRIPT_TICK_NS / 2 && t.budget <= 2 * SCRIPT_TICK_NS); /* half a step early at most */
            runs += (unsigned)run;
            if (pattern && n < 63) pattern[n] = run ? '1' : '0', pattern[n + 1] = 0;
        }
    return runs;
}
static void test_event_tick_decide(void) {
    char pattern[64];
    u64 cut;
    /* steady 19.2 ms frames (52 a second): 30 runs a second */
    static const u64 steady[1] = {19200000};
    assert(event_decide_runs(steady, 1, 52, NULL, &cut) == 30);
    /* 2, 2, 53.6 ms (51 frames, 979 ms): 28 runs with nothing cut, where the 1/30 s cap gave 19 */
    static const u64 uneven[3] = {2000000, 2000000, 53600000};
    const unsigned runs = event_decide_runs(uneven, 3, 17, NULL, &cut);
    assert(runs >= 28 && runs <= 29 && !cut);
    {
        ScriptTick t = {0};
        unsigned old = 0;
        for (unsigned k = 0; k < 51; k++) old += (unsigned)script_tick_decide(&t, k + 1, uneven[k % 3]);
        assert(old <= 19);
    }
    /* 16.67 ms: every other frame, 2 and 2 apart; 33.33 ms and 35 ms: every frame */
    static const u64 sixty[1] = {16666667}, thirty[1] = {33333334}, slow[1] = {35000000};
    (void)event_decide_runs(sixty, 1, 12, pattern, &cut);
    assert(!strcmp(pattern, "010101010101"));
    (void)event_decide_runs(thirty, 1, 12, pattern, &cut);
    assert(!strcmp(pattern, "111111111111"));
    (void)event_decide_runs(slow, 1, 12, pattern, &cut);
    assert(!strcmp(pattern, "111111111111"));
    /* one 100 ms frame at 60 FPS: its three ticks caught up one a frame, then every other frame again, nothing cut */
    static const u64 hitch[13] = {16666667, 16666667, 16666667, 16666667, 100000000, 16666667, 16666667, 16666667,
                                  16666667, 16666667, 16666667, 16666667, 16666667};
    (void)event_decide_runs(hitch, 13, 1, pattern, &cut);
    assert(!strcmp(pattern, "0101111110101") && !cut);
    /* steady 100 ms frames: a run every frame, and what one run a frame cannot take is cut above two ticks */
    static const u64 ten[1] = {100000000};
    (void)event_decide_runs(ten, 1, 5, pattern, &cut);
    assert(!strcmp(pattern, "11111") && cut > 266000000 && cut < 267000000);
}
/* The event tick on its real wrappers: a stand-in job that, when it runs, asks IsPadTrigger through the wrapper. */
#define EVENT_PAD 3200
static unsigned event_job_runs[3], event_trigger_seen[3], event_release_seen[3], event_repeat_seen[3];
static u64 event_job_answer;
static void event_trigger_stub(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = gr64(EVENT_PAD + 0x118) != 0;
    c->pc = c->x[30];
}
static void event_release_stub(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = gr64(EVENT_PAD + 0xb0) != 0;
    c->pc = c->x[30];
}
static void event_repeat_stub(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = (gr64(EVENT_PAD + 0xa8) | gr64(EVENT_PAD + 0xb8)) != 0;
    c->pc = c->x[30];
}
/* the jobs the jobs at 1024 and 1536 call inside their runs, before their own natives */
static u64 event_job_nested[2];
static void event_job_stub(CPU* c, u64 entry) {
    assert(entry == entry_pc);
    const unsigned which = c->x[0] == 1024 ? 0 : c->x[0] == 1536 ? 1 : 2;
    event_job_runs[which]++;
    if (which < 2 && event_job_nested[which]) {
        CPU j = {.x = {[0] = event_job_nested[which], [30] = return_pc}, .pc = entry_pc};
        event_tick_update(&j, entry_pc);
        assert(j.pc == return_pc);
    }
    CPU n = {.x = {[0] = 5, [30] = return_pc}, .pc = entry_pc};
    event_tick_trigger(&n, entry_pc);
    event_trigger_seen[which] += (unsigned)n.x[0];
    CPU m = {.x = {[0] = 5, [30] = return_pc}, .pc = entry_pc};
    event_tick_release(&m, entry_pc);
    assert(m.pc == return_pc);
    event_release_seen[which] += (unsigned)m.x[0];
    CPU q = {.x = {[0] = 5, [30] = return_pc}, .pc = entry_pc};
    event_tick_repeat(&q, entry_pc);
    assert(q.pc == return_pc);
    event_repeat_seen[which] += (unsigned)q.x[0];
    c->x[0] = event_job_answer;
    c->pc = c->x[30];
}
static void event_frame(u64 ns) {
    step.step = ns;
    __atomic_add_fetch(&frame_step_frames, 1, __ATOMIC_RELEASE);
}
/* One call of the job at the object; 1 when it ran. A skip answers 0 and leaves the job's memory alone. */
static int event_call(u64 object) {
    u8 before[0x148];
    memcpy(before, memory + object, sizeof before);
    const unsigned runs = event_job_runs[0] + event_job_runs[1] + event_job_runs[2];
    CPU c = {.x = {[0] = object, [30] = return_pc}, .pc = entry_pc};
    event_tick_update(&c, entry_pc);
    assert(c.pc == return_pc);
    const int ran = event_job_runs[0] + event_job_runs[1] + event_job_runs[2] != runs;
    if (!ran) assert(c.x[0] == 0 && !memcmp(before, memory + object, sizeof before));
    return ran;
}
static void event_reset(void) {
    memset(event_tick_jobs, 0, sizeof event_tick_jobs);
    memset(event_job_runs, 0, sizeof event_job_runs);
    memset(event_trigger_seen, 0, sizeof event_trigger_seen);
    memset(event_release_seen, 0, sizeof event_release_seen);
    memset(event_repeat_seen, 0, sizeof event_repeat_seen);
    event_job_answer = 0;
}
static void test_event_tick(void) {
    const u64 base = main_base, ns = step.step, frames = frame_step_frames;
    memset(memory, 0, sizeof memory);
    main_base = 3000 - EVENT_TICK_PAD; /* the pad's slot at 3000, its holder at 3100, the pad at 3200 */
    gw64(3000, 3100);
    gw64(3100, EVENT_PAD);
    gw64(1024 + 0x140, 0x700);
    gw64(1536 + 0x140, 0x800);
    event_tick_original = event_job_stub;
    event_tick_pad_original[0] = event_trigger_stub;
    event_tick_pad_original[1] = event_release_stub;
    event_tick_pad_original[2] = event_repeat_stub;
    /* the runs a second at 30, 60 and 120 FPS and at uneven 12 and 20 ms frames: 30, the first call at once */
    static const u64 steps[4][2] = {
        {33333334, 33333334}, {16666667, 16666667}, {8333334, 8333333}, {12000000, 20000000}};
    static const unsigned frames_per_second[4] = {30, 60, 120, 62};
    for (unsigned k = 0; k < 4; k++) {
        event_reset();
        unsigned ran = 0, first = 0;
        for (unsigned f = 0; f < frames_per_second[k]; f++) {
            event_frame(steps[k][f & 1]);
            const int run = event_call(1024);
            if (!f) first = (unsigned)run;
            ran += (unsigned)run;
        }
        assert(first && ran >= 30 && ran <= 31);
        if (!k) assert(ran == 30); /* at 30 FPS every frame runs, as the game does */
    }
    /* at 60 FPS a one-frame trigger in a skipped frame is seen by the next run once, and only once; the pad's word is
       as before afterwards, and the held word is never written */
    event_reset();
    gw64(EVENT_PAD + 0xa0, 0x40);
    event_frame(16666667);
    assert(event_call(1024));
    event_frame(16666667);
    gw64(EVENT_PAD + 0x118, 0x40);
    assert(!event_call(1024));
    gw64(EVENT_PAD + 0x118, 0);
    event_frame(16666667);
    assert(event_call(1024) && event_trigger_seen[0] == 1 && gr64(EVENT_PAD + 0x118) == 0);
    for (unsigned f = 0; f < 4; f++) {
        event_frame(16666667);
        (void)event_call(1024);
    }
    assert(event_trigger_seen[0] == 1 && gr64(EVENT_PAD + 0xa0) == 0x40);
    /* the release (+B0) and the repeat's two words (+A8, +B8) the same way: each seen once, each word put back, and
       the other natives see nothing of them */
    for (unsigned w = 0; w < 3; w++) {
        static const u32 at[3] = {0xb0, 0xa8, 0xb8};
        event_reset();
        event_frame(16666667);
        assert(event_call(1024));
        event_frame(16666667);
        gw64(EVENT_PAD + at[w], 0x10);
        assert(!event_call(1024));
        gw64(EVENT_PAD + at[w], 0);
        event_frame(16666667);
        assert(event_call(1024) && gr64(EVENT_PAD + at[w]) == 0);
        for (unsigned f = 0; f < 4; f++) {
            event_frame(16666667);
            (void)event_call(1024);
        }
        assert(event_release_seen[0] == (w == 0) && event_repeat_seen[0] == (w > 0) && !event_trigger_seen[0]);
    }
    /* two jobs at once, each on its own budget and latch: the trigger seen once by each */
    event_reset();
    event_frame(16666667);
    assert(event_call(1024));
    event_frame(16666667);
    assert(!event_call(1024) && event_call(1536)); /* the second job's first call runs at once */
    event_frame(16666667);
    gw64(EVENT_PAD + 0x118, 0x8);
    assert(event_call(1024) && !event_call(1536));
    gw64(EVENT_PAD + 0x118, 0);
    event_frame(16666667);
    assert(!event_call(1024) && event_call(1536));
    assert(event_trigger_seen[0] == 1 && event_trigger_seen[1] == 1);
    /* a finished job's state goes: a new job at the same address runs at once with nothing carried */
    event_reset();
    event_frame(16666667);
    assert(event_call(1024));
    event_frame(16666667);
    gw64(EVENT_PAD + 0x118, 0x2);
    assert(!event_call(1024));
    gw64(EVENT_PAD + 0x118, 0);
    event_job_answer = 1;
    event_frame(16666667);
    assert(event_call(1024) && event_trigger_seen[0] == 1);
    assert(!event_tick_jobs[0].object);
    event_job_answer = 0;
    event_frame(16666667);
    assert(event_call(1024) && event_trigger_seen[0] == 1);
    /* the same address with another script context is another job: fresh too */
    event_frame(16666667);
    assert(!event_call(1024));
    gw64(1024 + 0x140, 0x900);
    assert(event_call(1024));
    /* a job run inside another's run: as today, its natives see the pad, not the outer job's carried edges, and the
       outer job still sees them after it */
    event_reset();
    gw64(1024 + 0x140, 0x700);
    event_frame(16666667);
    assert(event_call(1024));
    event_frame(16666667);
    gw64(EVENT_PAD + 0x118, 0x4);
    assert(!event_call(1024));
    gw64(EVENT_PAD + 0x118, 0);
    event_job_nested[0] = 1536;
    event_frame(16666667);
    assert(event_call(1024) && event_job_runs[1] == 1);
    assert(event_trigger_seen[0] == 1 && event_trigger_seen[1] == 0 && gr64(EVENT_PAD + 0x118) == 0);
    /* three levels (the reviewer's control on cc604bdb): 1024 runs 1536, 1536 runs 2048. At 120 FPS with 1024 fresh
       each frame both nested jobs run in every run of 1024, with no state of their own, as today */
    event_reset();
    gw64(2048 + 0x140, 0x900);
    event_job_nested[0] = 1536;
    event_job_nested[1] = 2048;
    for (unsigned f = 0; f < 120; f++) {
        event_frame(8333334);
        gw64(1024 + 0x140, 0x1000 + f);
        assert(event_call(1024));
    }
    assert(event_job_runs[0] == 120 && event_job_runs[1] == 120 && event_job_runs[2] == 120);
    for (unsigned k = 0; k < EVENT_TICK_JOBS; k++)
        assert(event_tick_jobs[k].object != 1536 && event_tick_jobs[k].object != 2048);
    event_job_nested[0] = event_job_nested[1] = 0;
    gw64(1024 + 0x140, 0x700);
    /* the line comes every 600 frames, also with two jobs called each frame */
    event_reset();
    gw64(1024 + 0x140, 0x700);
    logged_count = 0;
    event_tick_line_frame = 0;
    for (unsigned f = 0; f < 1300; f++) {
        event_frame(16666667);
        (void)event_call(1024);
        (void)event_call(1536);
    }
    assert(logged_count == 2 && strstr(logged[0], "(in the last 600 frames: ran 600, skipped 600, fed ") &&
           strstr(logged[0], " ms, cut by the cap 0.0 ms)"));
    logged_count = 0;
    /* a full table: a ninth job runs every frame as today, counted */
    event_reset();
    const u64 full = event_tick_full;
    for (unsigned k = 0; k < EVENT_TICK_JOBS; k++)
        event_tick_jobs[k] = (EventTickJob){.object = 2000 + k, .generation = 1};
    for (unsigned f = 0; f < 4; f++) {
        event_frame(16666667);
        assert(event_call(1536));
    }
    assert(event_tick_full == full + 4);
    event_reset();
    event_tick_original = NULL;
    event_tick_pad_original[0] = event_tick_pad_original[1] = event_tick_pad_original[2] = NULL;
    main_base = base;
    step.step = ns;
    frame_step_frames = frames;
    memset(memory, 0, sizeof memory);
}
int main(void) {
    test_trainer_line();
    test_tick_end();
    test_mon_read();
    test_battle_mon();
    test_scene_scale();
    test_throw_guard();
    test_throw_guard_init();
    test_probe_master_switch();
    test_event_probe();
    test_event_tick();
    test_event_tick_decide();
    test_demo_probe();
    test_demo_tick();
    test_text_probe();
    test_text_probe_install();
    test_probe_compile_restore();
    test_text_tick();
    test_text_tick_end();
    test_text_tick_hold();
    test_ride_swap();
    test_defaults_on();
    test_fixed_resolution();
    test_discord_default();
    test_tick_switch_defaults();
    test_discord_layer_off();
    test_discord_bare();
    test_body_sites();
    test_discord_bare_sites();
    test_capped_fall_body();
    test_fade_slots_free();
    test_list_scroll();
    test_timer_carry();
    test_cloud_phases();
    test_ai_tick();
    test_turn();
    test_air_steering();
    const char* disabled[] = {"SNX_ARCEUS_TARGET_FPS", "SNX_ARCEUS_FPS60", "SNX_ARCEUS_CLOCK_TRACE",
        "SNX_ARCEUS_DIALOGUE_TRACE", "SNX_ARCEUS_FIXED_RESOLUTION", "SNX_ARCEUS_CAMERA_TRACE",
        "SNX_ARCEUS_TIMING_RESEARCH", "SNX_ARCEUS_DIRECT_MOVE_FIX", "SNX_ARCEUS_FIELD_WAZA_FIX",
        "SNX_ARCEUS_WAZA_OBJECT_FIX", "SNX_ARCEUS_WAZA_SHOT_FIX"};
    for (unsigned i = 0; i < sizeof disabled / sizeof disabled[0]; i++) flag(disabled[i], "");
    test_sequence();
    test_sequence_even();
    test_script_tick();
    test_script_tick_30();
    test_rotation_blend();
    test_script_window();
    test_fail_safe();
    test_diagnostics();
    test_climb_adrp();
    test_climb_stop();
    test_wall_jump();
    test_ledge();
    test_ledge_smoothing();
    test_target();
    test_direct_move();
    test_field_waza();
    test_waza_object();
    test_waza_shot();
    test_whole_step();
    puts("arceus_clock: all checks passed");
    return 0;
}
