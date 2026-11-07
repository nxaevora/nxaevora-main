/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_clock.h"
#include "nx/runtime/loader.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

static GuestFn originals[3];
static u64 counts[3];
static u64 main_base;

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
    originals[kind](c, entry);
    if (count <= 80 || count % 600 == 0) {
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

/* This probe observes virtual calls only. It leaves all game state and pacing unchanged.
   Refuse other builds or changed instructions before installing any dispatch entry. */
void arceus_clock_init(void) {
    static const u8 id[32] = {0xae,0xe8,0xf1,0x50,0xdd,0xa1,0xb5,0xa8,0x38,0x80,0x6e,0x1a,0x5e,0xa6,0x82,0x7a,
                              0xd9,0xf3,0xc5,0x1e,0,0,0,0,0,0,0,0,0,0,0,0};
    static const u64 offsets[3] = {0x278303c, 0x2783048, 0x277bef4};
    static const u32 words[3] = {0x8b21d008, 0xf0008fc8, 0x8b21d008};
    static const GuestFn wrappers[3] = {read_clock, set_mode, set_duration};
    const char* enabled = getenv("SNX_ARCEUS_CLOCK_TRACE");
    const SnxModInfo* mod = NULL;
    if (!enabled || strcmp(enabled, "1")) return;
    for (u32 i = 0; g_mod && i < g_mod->nmods; i++)
        if (!strcmp(g_mod->mods[i].name, "main")) mod = &g_mod->mods[i];
    if (!mod || memcmp(mod->build_id, id, sizeof id)) {
        WARN("arceus-clock: refused unsupported main build\n");
        return;
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
    LOG("arceus-clock: installed read-only clock probe for Arceus 1.1.1\n");
}
