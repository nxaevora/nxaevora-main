/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PASS_GUEST's switch, draw rule and line (gpu/vk/pass_guest.c), with the log kept by a stand-in
   (pass_guest_stub): read once, on only at render scale exactly 2.0, the reduced passes of the game's bloom chain and
   nothing at the scene's size, and the 600-present line with its programs, blits and fallbacks. Every case runs
   pass_guest.c afresh, since its state is static */
#include <stdlib.h>
#include "nx/gpu/vk/pass_guest.c"

char g_test_log[65536];
int g_test_lines;
static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static int has(const char* part) { return strstr(g_test_log, part) != NULL; }

static void set(const char* v) {
#ifdef _WIN32
    _putenv_s("SNX_ARCEUS_PASS_GUEST", v ? v : "");
#else
    if (v)
        setenv("SNX_ARCEUS_PASS_GUEST", v, 1);
    else
        unsetenv("SNX_ARCEUS_PASS_GUEST");
#endif
}

/* one draw into a single target of tw x th, with 1920x1080 presented */
static int one(uint32_t count, uint32_t instances, uint32_t tw, uint32_t th) {
    return pass_guest_reduced(count, instances, &tw, &th, 1, 1920, 1080);
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "";
    if (!strcmp(which, "unset")) {
        set(NULL);
        CHECK(pass_guest_on(2.0f) == 0 && g_pass_guest == 0 && g_test_lines == 0);
        set("1"); /* read once */
        CHECK(pass_guest_on(2.0f) == 0 && g_test_lines == 0);
    } else if (!strcmp(which, "on")) {
        set("1");
        CHECK(pass_guest_on(2.0f) == 1 && g_pass_guest == 1 && g_test_lines == 1);
        CHECK(has("arceus-pass-guest: on at render scale 2.0, the only scale") && has("(SNX_ARCEUS_PASS_GUEST=1)"));
        CHECK(pass_guest_on(2.0f) == 1 && g_test_lines == 1);
    } else if (!strcmp(which, "scale")) {
        /* any scale but exactly 2.0: off, and said once */
        set("1");
        CHECK(pass_guest_on(1.5f) == 0 && g_pass_guest == 0 && g_test_lines == 1);
        CHECK(has("is off at render scale 1.5"));
        CHECK(pass_guest_on(2.0f) == 0 && g_test_lines == 1);
    } else if (!strcmp(which, "scale3")) {
        set("1");
        CHECK(pass_guest_on(3.0f) == 0 && has("is off at render scale 3"));
    } else if (!strcmp(which, "bad")) {
        set("2");
        CHECK(pass_guest_on(2.0f) == 0 && g_test_lines == 1 && has("SNX_ARCEUS_PASS_GUEST=2 is not 1, ignored"));
    } else if (!strcmp(which, "rule")) {
        /* the bloom chain's reduced passes, at most half of 1920x1080 */
        CHECK(one(3, 1, 960, 540) && one(4, 1, 480, 270) && one(3, 1, 64, 33) && one(3, 1, 32, 1) && one(3, 1, 8, 4));
        /* the scene's size, a little under half, more vertices, more instances */
        CHECK(!one(3, 1, 1920, 1080) && !one(3, 1, 961, 540) && !one(3, 1, 960, 541) && !one(5, 1, 480, 270));
        CHECK(!one(3, 2, 480, 270) && !one(3, 0, 480, 270));
        /* no present yet */
        uint32_t w = 480, h = 270;
        CHECK(!pass_guest_reduced(3, 1, &w, &h, 1, 0, 0) && !pass_guest_reduced(3, 1, &w, &h, 1, 1920, 0));
        /* every bound target counts, an empty slot doesn't, and a draw needs one bound */
        uint32_t tw[3] = {480, 0, 1920}, th[3] = {270, 0, 1080};
        CHECK(!pass_guest_reduced(3, 1, tw, th, 3, 1920, 1080) && pass_guest_reduced(3, 1, tw, th, 2, 1920, 1080));
        CHECK(!pass_guest_reduced(3, 1, tw + 1, th + 1, 1, 1920, 1080));
        CHECK(!pass_guest_reduced(3, 1, tw, th, 0, 1920, 1080));
        /* sizes past 31 bits don't wrap */
        uint32_t big = 0x80000001u, one_h = 1;
        CHECK(!pass_guest_reduced(3, 1, &big, &one_h, 1, 0xFFFFFFFFu, 0xFFFFFFFFu));
    } else if (!strcmp(which, "window")) {
        pass_guest_window(10); /* the first call starts the window: no line */
        CHECK(g_test_lines == 0);
        for (uint32_t i = 0; i < 30; i++)
            pass_guest_served(i % 3 == 0 ? 0x08eaa0c815b7673cull : 0x61158d54f6596673ull, 1);
        pass_guest_served(0xb46268adda62e218ull, 2);
        for (int i = 0; i < 5; i++) pass_guest_blit();
        pass_guest_fallback(PASS_GUEST_FORMAT);
        pass_guest_fallback(PASS_GUEST_MEMORY);
        pass_guest_fallback(PASS_GUEST_MEMORY);
        pass_guest_fallback(PASS_GUEST_WHYS); /* not a reason: not counted */
        pass_guest_fallback(-1);
        pass_guest_window(609);
        CHECK(g_test_lines == 0); /* 599 presents */
        pass_guest_window(610);
        CHECK(g_test_lines == 1 && has("arceus-pass-guest: presents 10-609: 31 draws served (0.1 a present)"));
        CHECK(has("32 textures read as guest copies, 5 blits (0.0 a present)"));
        CHECK(has("fallbacks 0 integer, 1 format, 2 memory; programs served: 08eaa0c815b7673c 10, "
                  "61158d54f6596673 20, b46268adda62e218 1\n"));
        /* the next window starts empty */
        pass_guest_window(1210);
        CHECK(g_test_lines == 2 && has("presents 610-1209: 0 draws served (0.0 a present), 0 textures") &&
              has("0 memory; programs served: none\n"));
    } else if (!strcmp(which, "programs")) {
        /* more programs than a line names: the rest counted together */
        pass_guest_window(0);
        for (uint64_t i = 0; i < PASS_GUEST_PROGRAMS + 3; i++) pass_guest_served(0x1000 + i, 1);
        pass_guest_served(0x1000 + PASS_GUEST_PROGRAMS + 1, 1);
        pass_guest_served(0x1000, 1);
        pass_guest_window(PASS_GUEST_WINDOW);
        CHECK(has(" 0000000000001000 2,") && has(" 000000000000100f 1, other programs 4\n"));
        CHECK(!has("0000000000001010"));
    } else if (!strcmp(which, "full")) {
        /* the longest lines: 20-digit counts, one of 16 digits so an entry starts 35 bytes short of the end and
           runs past it, and others after: cut at the buffer, never past it */
        pass_guest_window(0);
        for (uint64_t i = 0; i < PASS_GUEST_PROGRAMS + 1; i++) pass_guest_served(0xF000000000000000ull + i, 1);
        for (uint32_t i = 0; i < PASS_GUEST_PROGRAMS; i++) g_pg.prog_draws[i] = ~0ull;
        g_pg.prog_draws[13] = 1000000000000000ull;
        g_pg.more = ~0ull;
        pass_guest_window(PASS_GUEST_WINDOW);
        CHECK(g_test_lines == 1 && has(" f000000000000000 18446744073709551615,") && has("\n"));
        CHECK(strlen(g_test_log) < 1000);
    } else {
        printf("unknown case %s\n", which);
        return 2;
    }
    printf("%s %s\n", failures ? "FAIL" : "PASS", which);
    return failures != 0;
}
