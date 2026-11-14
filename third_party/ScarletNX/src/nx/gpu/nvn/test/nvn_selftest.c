/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The NVN selftest (nvn-unobserved.md, "The selftest"). The first cases are kinds R and M of the object layer:
   nvnDeviceGetInteger (the word the call writes, kind M), and the texture builder storage queries and getters (kind R).
   The calls run on the guest thread that triggers the run, with the CPU context saved and restored around each call.
   Nothing here writes an object of the game: the builders are in the arena of the selftest. */
#include "nx/gpu/nvn/test/nvn_selftest.h"
#include "nx/gpu/nvn/nvn_native.h"
#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/gpu/nvn/test/nvn_selftest_mem.h"
#include "nx/gpu/nvn/diag/nvn_trace.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* 2 MiB: each side of each resource script has its own 0x1000-aligned storage block and object blocks (4-u3-2). */
#define ARENA_BYTES 0x200000u
#define BUILDER_BYTES 0x200u /* more than the library's builder (a reserved block) and the host's record */
#define NAME_MAX_LEN 64
#define OUT_GUARD_BYTES 16u /* untouched bytes before and after the output word: an extra write shows as kind M */
#define OUT_WORD_BYTES 4u
#define OUT_FILL 0xA5
#define DEVICE_TEXTURE_WORD_OFFSET 0x1b18u     /* the buffer kind word of the device (nvn_params.h) */
#define DEVICE_COMPRESSION_BYTE_OFFSET 0x1b1cu /* the compression switch of the device, source unknown */

#define DEVICE_CLIP_LIMIT_OFFSET 0x18u   /* the limit that SetRenderTargets applies to SURFACE_CLIP (d856414), source unknown */
#define DEVICE_DEPTH_MODE_OFFSET 0x1a04u /* compared with 0x08c8b771 by SetRenderTargets (d856218) */

static uint64_t g_device;
static int g_ran;

void nvn_selftest_note_device(uint64_t device) { g_device = device; }

uint64_t nvn_st_device(void) { return g_device; }

int nvn_st_proc_find(NvnStProc* p, const char* name) {
    p->name = name;
    if (!nvn_trace_library(name, &p->lib.fn, &p->lib.entry)) return 0;
    p->host.fn = nvn_native_handler(name);
    p->host.entry = p->lib.entry;
    return p->host.fn != NULL;
}

/* x0..x5 are the arguments, and s0..s3 the float arguments. Returns x0. The whole context is restored, so the
   caller's thread goes on as before; a float result is copied out before the restore. */
uint64_t nvn_st_call(CPU* c, const NvnStCallee* f, const NvnStCall* a) {
    CPU saved = *c;
    for (unsigned i = 0; i < 6; i++) c->x[i] = a->x[i];
    for (unsigned i = 0; i < a->n_s; i++) SNX_VSET_S(c->v[i], 0, a->s[i]);
    f->fn(c, f->entry);
    uint64_t r = c->x[0];
    if (a->s0_out) *a->s0_out = SNX_VS(c->v[0], 0);
    *c = saved;
    return r;
}

/* The report of one comparison: the line, and the first difference of each failed kind with both values. */
void nvn_st_report(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d) {
    char line[256];
    nvn_st_format_line(line, sizeof line, name, args, ref, d);
    run->cases++;
    if (d->failed) run->failed++;
    LOG("%s\n", line);
    for (int k = 0; k < NVN_ST_KINDS; k++)
        if (d->failed & (1u << k)) LOG("selftest:   first difference (%c): %s\n", "RMSY"[k], d->first[k]);
}

/* The report of a case whose host handler is a logged no-op by design: a difference is expected here. A case with no
   difference is a fail, because the host changed and the row must be checked again. */
void nvn_st_report_noop(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d) {
    char line[256];
    run->cases++;
    if (!d->failed) {
        run->failed++;
        LOG("selftest: %s expected a no-op difference, none seen\n", name);
        return;
    }
    run->expected++;
    const int n = nvn_st_format_line(line, sizeof line, name, args, ref, d);
    if (n > 0 && (size_t)n < sizeof line) snprintf(line + n, sizeof line - (size_t)n, " (expected no-op)");
    LOG("%s\n", line);
    for (int k = 0; k < NVN_ST_KINDS; k++)
        if (d->failed & (1u << k)) LOG("selftest:   first difference (%c): %s\n", "RMSY"[k], d->first[k]);
}

/* nvnDeviceGetInteger(device, what, int* out): the call writes one word into memory that the game can see (kind M).
   The values are the ones that the run of 2026-11-02 asked for; an unobserved value is a later step. */
static const uint32_t device_what[] = {4,  15, 16, 17, 18, 19, 20, 40, 41, 42, 50, 59, 66, 67,
                                       68, 69, 71, 72, 73, 74, 75, 76, 82, 85, 87, 90, 92};

static void case_device_get_integer(CPU* c, NvnStRun* run) {
    NvnStProc p;
    if (!nvn_st_proc_find(&p, "nvnDeviceGetInteger")) {
        run->skipped++;
        LOG("selftest: nvnDeviceGetInteger skipped (the game did not resolve it, or the host has no handler)\n");
        return;
    }
    enum { REGION = OUT_GUARD_BYTES + OUT_WORD_BYTES + OUT_GUARD_BYTES };
    uint64_t region = nvn_st_arena_alloc(&run->arena, REGION, 16);
    uint64_t out = region + OUT_GUARD_BYTES;
    for (size_t i = 0; region && i < sizeof device_what / sizeof device_what[0]; i++) {
        uint8_t lib_bytes[REGION], host_bytes[REGION];
        memset(GP(region), OUT_FILL, REGION); /* the word must be overwritten, the guards must not */
        nvn_st_call(c, &p.lib, &(NvnStCall){.x = {g_device, device_what[i], out}});
        memcpy(lib_bytes, GP(region), REGION);
        memset(GP(region), OUT_FILL, REGION);
        nvn_st_call(c, &p.host, &(NvnStCall){.x = {g_device, device_what[i], out}});
        memcpy(host_bytes, GP(region), REGION);
        NvnStRec ref = {0}, got = {0};
        NvnStDiff d;
        nvn_st_add_blob(&ref, "out", lib_bytes, NULL, REGION);
        nvn_st_add_blob(&got, "out", host_bytes, NULL, REGION);
        nvn_st_compare(&ref, &got, &d);
        char args[32];
        snprintf(args, sizeof args, "what=%u", device_what[i]);
        nvn_st_report(run, p.name, args, &ref, &d);
    }
}

/* The texture builder: the setters fill the builder (in the arena for the library, in a record for the host), then the
   storage queries and the getters answer. Sets are formats, sizes, levels and flags that the game used (flags 9, formats
   0x25 and 0x26), the target 1 (2D). */
typedef struct {
    NvnStProc set_device, set_defaults, set_flags, set_target, set_size, set_levels, set_format;
    NvnStProc get_size, get_align, get_width, get_height;
} TexProcs;

static int tex_procs_find(TexProcs* t) {
    return nvn_st_proc_find(&t->set_device, "nvnTextureBuilderSetDevice") &&
           nvn_st_proc_find(&t->set_defaults, "nvnTextureBuilderSetDefaults") &&
           nvn_st_proc_find(&t->set_flags, "nvnTextureBuilderSetFlags") &&
           nvn_st_proc_find(&t->set_target, "nvnTextureBuilderSetTarget") &&
           nvn_st_proc_find(&t->set_size, "nvnTextureBuilderSetSize2D") &&
           nvn_st_proc_find(&t->set_levels, "nvnTextureBuilderSetLevels") &&
           nvn_st_proc_find(&t->set_format, "nvnTextureBuilderSetFormat") &&
           nvn_st_proc_find(&t->get_size, "nvnTextureBuilderGetStorageSize") &&
           nvn_st_proc_find(&t->get_align, "nvnTextureBuilderGetStorageAlignment") &&
           nvn_st_proc_find(&t->get_width, "nvnTextureBuilderGetWidth") &&
           nvn_st_proc_find(&t->get_height, "nvnTextureBuilderGetHeight");
}

typedef struct {
    uint32_t flags, format, width, height, levels;
} TexCase;

static void tex_set(CPU* c, const TexProcs* t, int host, uint64_t b, const TexCase* k) {
#define SIDE(p) (host ? &(p).host : &(p).lib)
    nvn_st_call(c, SIDE(t->set_defaults), &(NvnStCall){.x = {b}});
    nvn_st_call(c, SIDE(t->set_device), &(NvnStCall){.x = {b, g_device}});
    nvn_st_call(c, SIDE(t->set_flags), &(NvnStCall){.x = {b, k->flags}});
    nvn_st_call(c, SIDE(t->set_target), &(NvnStCall){.x = {b, 1}});
    nvn_st_call(c, SIDE(t->set_size), &(NvnStCall){.x = {b, k->width, k->height}});
    nvn_st_call(c, SIDE(t->set_levels), &(NvnStCall){.x = {b, k->levels}});
    nvn_st_call(c, SIDE(t->set_format), &(NvnStCall){.x = {b, k->format}});
}

static void tex_query(CPU* c, const TexProcs* t, int host, uint64_t b, NvnStRec* rec) {
    nvn_st_add_ret(rec, "size", nvn_st_call(c, SIDE(t->get_size), &(NvnStCall){.x = {b}}));
    nvn_st_add_ret(rec, "alignment", nvn_st_call(c, SIDE(t->get_align), &(NvnStCall){.x = {b}}));
    nvn_st_add_ret(rec, "width", (uint32_t)nvn_st_call(c, SIDE(t->get_width), &(NvnStCall){.x = {b}}));
    nvn_st_add_ret(rec, "height", (uint32_t)nvn_st_call(c, SIDE(t->get_height), &(NvnStCall){.x = {b}}));
}
#undef SIDE

static void case_texture_builder(CPU* c, NvnStRun* run) {
    TexProcs t;
    if (!tex_procs_find(&t)) {
        run->skipped++;
        LOG("selftest: nvnTextureBuilder skipped (a function was not resolved, or the host has no handler)\n");
        return;
    }
    uint64_t b = nvn_st_arena_alloc(&run->arena, BUILDER_BYTES, 16);
    if (!b) return;
    static const uint32_t flags[] = {0, 9}, formats[] = {0x25, 0x26}, levels[] = {1, 4};
    static const uint32_t sizes[][2] = {{64, 64}, {1280, 720}, {1920, 1080}};
    for (size_t f = 0; f < 2; f++)
        for (size_t m = 0; m < 2; m++)
            for (size_t s = 0; s < 3; s++)
                for (size_t l = 0; l < 2; l++) {
                    TexCase k = {flags[f], formats[m], sizes[s][0], sizes[s][1], levels[l]};
                    NvnStRec ref = {0}, got = {0};
                    NvnStDiff d;
                    memset(GP(b), 0, BUILDER_BYTES);
                    tex_set(c, &t, 0, b, &k);
                    tex_query(c, &t, 0, b, &ref);
                    tex_set(c, &t, 1, b, &k);
                    tex_query(c, &t, 1, b, &got);
                    nvn_st_compare(&ref, &got, &d);
                    char args[96];
                    snprintf(args, sizeof args, "flags=%u fmt=0x%x %ux%u levels=%u", k.flags, k.format, k.width, k.height,
                             k.levels);
                    nvn_st_report(run, "nvnTextureBuilderStorage", args, &ref, &d);
                }
}

void nvn_selftest_run(CPU* c) {
    if (__atomic_exchange_n(&g_ran, 1, __ATOMIC_ACQ_REL)) return;
    if (nvn_native_active()) {
        LOG("selftest: not run, nvn_native=1 (the library must run as the reference)\n");
        return;
    }
    if (!g_device) {
        LOG("selftest: not run, the game made no device before nvnQueueInitialize\n");
        return;
    }
    NvnStRun run = {0};
    u64 base = kmem_alloc(ARENA_BYTES, MS_NORMAL, PERM_RW);
    nvn_st_arena_init(&run.arena, base, ARENA_BYTES);
    LOG("selftest: start, device=%" PRIx64 ", arena=%" PRIx64 "+%x\n", g_device, base, ARENA_BYTES);
    LOG("selftest: device texture words +%x=%08x +%x=%08x\n", DEVICE_TEXTURE_WORD_OFFSET,
        gr32(g_device + DEVICE_TEXTURE_WORD_OFFSET), DEVICE_COMPRESSION_BYTE_OFFSET,
        (u32)*(const uint8_t*)GP(g_device + DEVICE_COMPRESSION_BYTE_OFFSET));
    LOG("selftest: device render target words +%x=%08x +%x=%08x +%x=%08x\n", DEVICE_CLIP_LIMIT_OFFSET,
        gr32(g_device + DEVICE_CLIP_LIMIT_OFFSET), NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET,
        gr32(g_device + NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET), DEVICE_DEPTH_MODE_OFFSET,
        gr32(g_device + DEVICE_DEPTH_MODE_OFFSET));
    case_device_get_integer(c, &run);
    case_texture_builder(c, &run);
    nvn_selftest_object_cases(c, &run);
    nvn_selftest_resource_cases(c, &run);
    nvn_selftest_command_state_cases(c, &run);
    nvn_selftest_command_cases(c, &run);
    LOG("selftest: %u cases, %u failed, %u expected no-op, %u skipped, arena used %" PRIu64 " of %u bytes\n",
        run.cases, run.failed, run.expected, run.skipped, run.arena.high_water, ARENA_BYTES);
}
