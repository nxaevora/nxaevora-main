#include <stdlib.h>
/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Unattended runs, for the windowed and the headless backend alike: the run's limit, screenshots and scripted
   presses */
#include "nx/runtime/platform/unattended.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/tracy.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* one press of the script: at a time in ms, or at a present ("Nf") */
typedef struct {
    u64 mask;
    u64 held_since; /* plat_ns() when a present press began, 0 before */
    u32 at;
    u8 at_present;
    u8 done;
} ScriptPress;

/* The script: its presses and, after them in the same allocation, its text (to tell a reload of the same script
   from a new one). press is the allocation, NULL without a script */
typedef struct {
    ScriptPress* press;
    char* text;
    size_t n;
} Script;

/* the settings are g_settings' (settings.txt's g_unattended_ini): seconds (0: no time limit), frames, screenshot,
   screenshot_every, press_length, script */
static Script g_script;
static u64 g_t0;           /* unattended_start's plat_ns() */
static int g_shot_bucket;  /* frames / shot_every at the last periodic shot */
static int g_shot_count;

static int script_parse_press(const char** s, ScriptPress* p) {
    const char* c = *s;
    char* end;
    unsigned long at;
    unsigned long long mask;
    if (*c < '0' || *c > '9') return 0;
    at = strtoul(c, &end, 10);
    c = end;
    memset(p, 0, sizeof *p);
    if (*c == 'f') {
        p->at_present = 1;
        c++;
    }
    if (*c++ != ':' || at > UINT_MAX) return 0;
    if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F'))) return 0;
    mask = strtoull(c, &end, 16);
    c = end;
    if (*c && *c != ',') return 0;
    p->at = (u32)at;
    p->mask = (u64)mask;
    *s = *c ? c + 1 : c;
    return 1;
}

void unattended_script_set(const IniOpt* o, const char* v) {
    size_t len = strlen(v), max = 1, n;
    const char* c;
    ScriptPress* press;
    (void)o;
    if (g_script.press && !strcmp(g_script.text, v)) return; /* a reload: keep the progress, presses aren't replayed */
    if (!*v) {
        free(g_script.press);
        memset(&g_script, 0, sizeof g_script);
        return;
    }
    for (c = v; *c; c++) max += *c == ',';
    if (max > (SIZE_MAX - len - 1) / sizeof *press) {
        WARN("unattended: the script is too long, the current one stays\n");
        return;
    }
    press = malloc(max * sizeof *press + len + 1);
    if (!press) {
        WARN("unattended: out of memory for the script, the current one stays\n");
        return;
    }
    for (n = 0, c = v; *c && n < max; n++)
        if (!script_parse_press(&c, &press[n])) {
            WARN("unattended: the script stops at \"%s\", which is not ms:hex or Nf:hex\n", c);
            break;
        }
    free(g_script.press);
    g_script.press = press;
    g_script.text = (char*)(press + max);
    memcpy(g_script.text, v, len + 1);
    g_script.n = n;
}

int unattended_known(const char* k) { return ini_find(g_unattended_ini, k) != NULL; }
int unattended_ini(const char* k, const char* v) {
    const IniOpt* o = ini_find(g_unattended_ini, k);
    if (!o) return 0;
    ini_read(o, v);
    return 1;
}

/* the presses start over too: a look at them before (the backend's, during main.c's prepare_wait) began a present
   press, which would end before the game's presents reach it */
void unattended_start(void) {
    g_t0 = plat_ns();
    for (size_t i = 0; i < g_script.n; i++) {
        g_script.press[i].held_since = 0;
        g_script.press[i].done = 0;
    }
}
u64 unattended_ms(void) { return (plat_ns() - g_t0) / 1000000ull; }
int unattended_active(void) { return g_settings.seconds > 0 || g_settings.frames >= 0; }

static int frames_now(void) { return __atomic_load_n(&g_frames, __ATOMIC_RELAXED); }

u64 unattended_buttons(void) {
    u64 now = plat_ns(), held = 0, len_ns = (u64)g_settings.press_length * 1000000u, ms;
    int frames = frames_now();
    size_t i;
    if (!g_t0) g_t0 = now;
    ms = (now - g_t0) / 1000000u;
    for (i = 0; i < g_script.n; i++) {
        ScriptPress* p = &g_script.press[i];
        if (!p->at_present) {
            if (ms >= p->at && ms - p->at < (u64)g_settings.press_length) held |= p->mask;
            continue;
        }
        const char *duration=getenv("ARCEUS_PRESS_PRESENTS");
        if(duration && p->at_present){
            unsigned span=(unsigned)atoi(duration);
            if(frames>=0 && (unsigned)frames>=p->at && (unsigned)frames-p->at<span)held|=p->mask;
            continue;
        }
        if (p->done) continue;
        /* from the first look at or after present N, even when the presents went past it between two looks: the
           game advances a step per present, so a time in ms could come too early on a slow machine */
        if (!p->held_since && frames >= 0 && (u32)frames >= p->at) p->held_since = now;
        if (!p->held_since) continue;
        if (now - p->held_since < len_ns)
            held |= p->mask;
        else
            p->done = 1;
    }

    static int last=-1,through=-1;
    static unsigned sequence;
    static unsigned long long control;
    if(frames!=last){
        last=frames;
        const char *path=getenv("ARCEUS_QA_CONTROLLER");
        FILE *file=path?fopen(path,"r"):NULL;
        if(file){
            unsigned command,span;unsigned long long mask;
            if(fscanf(file,"%u %u %llx",&command,&span,&mask)==3 && command>sequence && span<=2400){
                sequence=command;through=frames+(int)span;control=mask;
                LOG("field-route: command=%u frame=%d span=%u mask=%llx\n",command,frames,span,mask);
            }
            fclose(file);
        }
    }
    if(frames<through)held|=control;
    return held;
}

const char* unattended_shot(void) { return g_settings.screenshot[0] ? g_settings.screenshot : NULL; }

int unattended_periodic_shot(char* path, size_t n) {
    int bucket;
    if (!g_settings.screenshot[0] || g_settings.screenshot_every <= 0) return 0;
    /* by bucket, not frames % shot_every: frames may move by two between looks */
    int presented=frames_now();
    const char *first=getenv("ARCEUS_SHOT_FIRST"),*last=getenv("ARCEUS_SHOT_LAST");
    if(first && presented<atoi(first))return 0;
    if(last && presented>atoi(last))return 0;
    bucket=presented/g_settings.screenshot_every;
    if (bucket == g_shot_bucket) return 0;
    g_shot_bucket = bucket;
    snprintf(path, n, "%s_%04d", g_settings.screenshot, presented/60%2);
    return 1;
}

int unattended_done(void) {
    const char *finish=getenv("ARCEUS_QA_END_ROUTE");
    if(finish){FILE *f=fopen(finish,"r");if(f){fclose(f);return 1;}}
    if (g_settings.frames >= 0 && frames_now() >= g_settings.frames) return 1;
    return g_settings.seconds > 0 && g_t0 && plat_ns() - g_t0 >= (u64)g_settings.seconds * 1000000000u;
}

void unattended_exit(u64 hash, int w, int h) {
    if (w > 0 && h > 0)
        fprintf(stderr, "\nframes=%d hash=%016llx %dx%d\n", frames_now(), (unsigned long long)hash, w, h);
    else
        fprintf(stderr, "\nframes=%d hash=%016llx\n", frames_now(), (unsigned long long)hash);
    ini_poll(1); /* the settings changed during the run, as a quit saves them */
    snx_save_before_exit();
    /* The same close as process_exit: it ends the frontend's open zone and lets the capture write. */
    snx_tracy_shutdown();
    plat_exit_now(0);
}

void input_key_sticks(u64 k, i32 st[4]) {
    int s;
    for (s = 0; s < 2; s++) {
        u64 d = k >> (40 + 4 * s); /* left, up, right, down */
        st[2 * s] = ((i32)((d >> 2) & 1) - (i32)(d & 1)) * 32767;
        st[2 * s + 1] = ((i32)((d >> 1) & 1) - (i32)((d >> 3) & 1)) * 32767;
    }
}

void input_stick_clamp(i32* x, i32* y) {
    double r;
    if (*x < -32767) *x = -32767;
    if (*x > 32767) *x = 32767;
    if (*y < -32767) *y = -32767;
    if (*y > 32767) *y = 32767;
    r = sqrt((double)*x * *x + (double)*y * *y);
    if (r > 32767.0) {
        *x = (i32)(*x * (32767.0 / r));
        *y = (i32)(*y * (32767.0 / r));
    }
}
