/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PASS_GUEST's switch, draw rule and line (pass_guest.h). Nothing is allocated, and with the switch unset
   nothing but its one read runs. The copies are texture.c's vk_rt_guest, bound by render.c's stage_set_ */
#include "nx/gpu/vk/pass_guest.h"
#include "nx/runtime/platform/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_pass_guest = -1;
uint32_t g_pass_shown_w, g_pass_shown_h;

int pass_guest_on(float scale) {
    if (g_pass_guest < 0) {
        const char* v = getenv("SNX_ARCEUS_PASS_GUEST");
        g_pass_guest = 0;
        if (v && !strcmp(v, "1")) {
            if (scale == 2.0f) {
                g_pass_guest = 1;
                LOG("arceus-pass-guest: on at render scale 2.0, the only scale it acts at: draws of at most 4 "
                    "vertices and one instance into colour targets of at most half the presented size read the scaled "
                    "colour targets they sample through copies at guest size, a line every %u presents "
                    "(SNX_ARCEUS_PASS_GUEST=1)\n",
                    PASS_GUEST_WINDOW);
            } else
                WARN("arceus-pass-guest: SNX_ARCEUS_PASS_GUEST=1 is off at render scale %g: its copy is the mean of "
                     "each 2x2 block only at exactly 2.0\n",
                     (double)scale);
        } else if (v && *v)
            WARN("arceus-pass-guest: SNX_ARCEUS_PASS_GUEST=%s is not 1, ignored\n", v);
    }
    return g_pass_guest;
}

int pass_guest_reduced(uint32_t count, uint32_t instances, const uint32_t* tw, const uint32_t* th, uint32_t n,
                       uint32_t shown_w, uint32_t shown_h) {
    int bound = 0;
    if (count > 4 || instances != 1 || !shown_w || !shown_h) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!tw[i] && !th[i]) continue;
        if ((uint64_t)tw[i] * 2 > shown_w || (uint64_t)th[i] * 2 > shown_h) return 0;
        bound = 1;
    }
    return bound;
}

/* the window's counts: draws served and the textures they read as copies, blits, fallbacks, and the first
   PASS_GUEST_PROGRAMS programs served with their draws (more counts the rest) */
static struct {
    uint32_t start, started, nprog;
    uint64_t draws, textures, blits, more, why[PASS_GUEST_WHYS];
    uint64_t prog[PASS_GUEST_PROGRAMS], prog_draws[PASS_GUEST_PROGRAMS];
} g_pg;

void pass_guest_fallback(int why) {
    if (why >= 0 && why < PASS_GUEST_WHYS) g_pg.why[why]++;
}

void pass_guest_blit(void) { g_pg.blits++; }

void pass_guest_served(uint64_t fs, uint32_t textures) {
    uint32_t i = 0;
    g_pg.draws++;
    g_pg.textures += textures;
    while (i < g_pg.nprog && g_pg.prog[i] != fs) i++;
    if (i == g_pg.nprog && g_pg.nprog < PASS_GUEST_PROGRAMS) g_pg.prog[g_pg.nprog++] = fs;
    if (i < g_pg.nprog)
        g_pg.prog_draws[i]++;
    else
        g_pg.more++;
}

void pass_guest_window(uint32_t present) {
    if (!g_pg.started) {
        g_pg.started = 1;
        g_pg.start = present;
        return;
    }
    uint32_t presents = present - g_pg.start;
    if (presents < PASS_GUEST_WINDOW) return;
    char text[PASS_GUEST_PROGRAMS * 32 + 64];
    size_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < g_pg.nprog && at + 32 < sizeof text; i++) {
        at += (size_t)snprintf(text + at, sizeof text - at, "%s %016llx %llu", i ? "," : "",
                               (unsigned long long)g_pg.prog[i], (unsigned long long)g_pg.prog_draws[i]);
        if (at >= sizeof text) at = sizeof text - 1; /* a cut entry ends the list */
    }
    if (g_pg.more && at + 1 < sizeof text)
        snprintf(text + at, sizeof text - at, ", other programs %llu", (unsigned long long)g_pg.more);
    LOG("arceus-pass-guest: presents %u-%u: %llu draws served (%.1f a present), %llu textures read as guest copies, "
        "%llu blits (%.1f a present), fallbacks %llu integer, %llu format, %llu memory; programs served:%s\n",
        g_pg.start, present - 1, (unsigned long long)g_pg.draws, (double)g_pg.draws / presents,
        (unsigned long long)g_pg.textures, (unsigned long long)g_pg.blits, (double)g_pg.blits / presents,
        (unsigned long long)g_pg.why[PASS_GUEST_INTEGER], (unsigned long long)g_pg.why[PASS_GUEST_FORMAT],
        (unsigned long long)g_pg.why[PASS_GUEST_MEMORY], g_pg.nprog ? text : " none");
    memset(&g_pg, 0, sizeof g_pg);
    g_pg.started = 1;
    g_pg.start = present;
}
