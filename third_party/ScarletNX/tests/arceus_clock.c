/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Exercise the real wrappers with an asset-free guest sequence and host stubs. */
#include "nx/runtime/arceus_clock.c"
#include "arceus_steps_30.h"
#include <assert.h>

static u8 memory[4096];
u8* g_mem = memory;
u64 g_as_size = sizeof memory;
MemLayout g_layout = {.base = 1};
const SnxModule* g_mod;
GuestFn* g_disp;
u64 g_code_lo, g_code_hi;
int g_opt_fps;
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }
static unsigned waza_dependency_warnings;
static unsigned low_fps_skip_logs;
void logf_(int level, const char* fmt, ...) {
    if (level == 0 && strstr(fmt, "arceus-waza-shot: enabled without WAZA_OBJECT_FIX"))
        waza_dependency_warnings++;
    if (level == 1 && strstr(fmt, "arceus-clock: timing layer skipped at fps_limit="))
        low_fps_skip_logs++;
}
static u64 host_ns = 1000000000;
u64 plat_ns(void) { return host_ns; }
void mem_query(u64 address, MemBlock* out) {
    (void)address;
    *out = (MemBlock){.addr = 1, .size = sizeof memory - 1, .perm = PERM_RW};
}
u64 mem_find_free(u64 lo, u64 hi, u64 size, u64 align) {
    (void)lo; (void)hi; (void)size; (void)align;
    assert(0); return 0;
}
int mem_map(u64 address, u64 size, u32 state, u32 perm) {
    (void)address; (void)size; (void)state; (void)perm;
    assert(0); return -1;
}
void jit_code_changed(u64 pc, u64 size) { (void)pc; (void)size; assert(0); }
GuestFn jit_wait(u64 pc) { (void)pc; assert(0); return NULL; }

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
        "SNX_ARCEUS_SCRIPT_WINDOWS"
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
        "SNX_ARCEUS_DIRECT_MOVE_FIX", "SNX_ARCEUS_FIELD_WAZA_FIX", "SNX_ARCEUS_WAZA_OBJECT_FIX"
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
        "SNX_ARCEUS_TIMING_RESEARCH", "SNX_ARCEUS_CADENCE_CENSUS", "SNX_ARCEUS_GRAVITY_PROBE",
        "SNX_ARCEUS_FIXED_RESOLUTION"
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
    }
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
int main(void) {
    test_ride_swap();
    test_defaults_on();
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
