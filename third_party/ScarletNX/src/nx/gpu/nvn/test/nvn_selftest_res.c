/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The resource object cases of the NVN selftest (nvn-unobserved.md, "The selftest") for step 4-u3-2: the memory
   pools, buffers, textures, texture and sampler pools, samplers, programs, the device queries and the builder setters
   that take a pool, a texture or an address. Each script goes to the library and to the host handler for the same
   values. A side has its own object and storage blocks, because the library maps its storage and the host keeps a
   record. Addresses differ between the sides, so the scripts compare relative offsets, same-object results and
   nonzero flags, never a raw address. */
#include "nx/gpu/nvn/test/nvn_selftest_int.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define RES_OBJ_BYTES 0x1000u        /* more than the largest library object of this file */
#define RES_POOL_BYTES 0x4000u       /* the memory pool of the buffer, builder and pool cases */
#define RES_TEX_POOL_BYTES 0x10000u  /* the memory pool of the texture cases, 0x1000 a texture */
#define RES_DESC_POOL_BYTES 0x10000u /* two descriptor tables of 512 entries and one small texture */
#define RES_POOL_ALIGN 0x1000u
#define RES_OUT_BYTES 0x40u /* 16 guard bytes, four 8-byte outputs and 16 guard bytes */
#define RES_OUT_GUARD 16u
#define RES_OUT_SLOT 8u
#define RES_OUT_FILL 0xA5
#define RES_DESC_BYTES 32u
#define RES_DESC_ENTRIES 512u
#define RES_DESC_ID_A 256u
#define RES_DESC_ID_B 257u
#define RES_TEX_SIDE 32u
#define RES_TEX_REGION 16u
#define RES_TEX_STRIDE 256u
#define RES_TEX_BUF_BYTES (RES_TEX_STRIDE * RES_TEX_REGION)
#define RES_TEX_OFFSET 0x8000u /* the texture of the descriptor pool cases, after the two tables */
#define RES_TIMESTAMP_TICKS 0x123456789ull
#define RES_SAMPLE_SHADING 1u /* a value that the library stores at the program and the host ignores */
#define RES_DATA_BYTES 0x20u /* the border color words of one sampler builder case */

/* The pool flags of analysis/enum-run2.txt, CPU cached first (decision 4). The host refuses only VIRTUAL and IMPORT. */
#define RES_POOL_CPU_CACHED 0x24u /* CPU cached, GPU cached */
#define RES_POOL_CPU_UNCACHED_GPU_CACHED NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED /* CPU uncached, GPU cached */
#define RES_POOL_CPU_UNCACHED_GPU_UNCACHED 0x12u
#define RES_POOL_CPU_NO_ACCESS 0x21u
#define RES_POOL_VIRTUAL 0x200u /* a virtual pool has no storage; only nvnMemoryPoolMapVirtual uses it */

#define RES_TEX_FLAG_LINEAR 0x10u
#define RES_TEX_TARGET_2D 1u
#define RES_TEX_FORMAT_A 0x25u
#define RES_TEX_FORMAT_B 0x26u

/* Kinds R and M: a getter with up to four pointer outputs, and a return value of x0, s0 or nothing. */
typedef struct {
    const char* field;
    unsigned outs;
    unsigned ret; /* 0 nothing, 1 x0, 2 the s0 bits */
} ResGet;

NvnStProc* nvn_st_res_proc(NvnStResCache* cache, const char* name) {
    for (unsigned i = 0; i < cache->n; i++)
        if (!strcmp(cache->e[i].name, name)) return cache->e[i].resolved ? &cache->e[i].proc : NULL;
    if (cache->n >= NVN_ST_RES_CACHE_MAX) return NULL;
    NvnStResEntry* entry = &cache->e[cache->n++];
    entry->name = name;
    entry->resolved = (unsigned)nvn_st_proc_find(&entry->proc, name);
    if (!entry->resolved)
        LOG("selftest: %s skipped (the game did not resolve it, or the host has no handler)\n", name);
    return entry->resolved ? &entry->proc : NULL;
}

static uint64_t res_alloc(NvnStRun* run, uint64_t size, uint64_t align) {
    uint64_t address = nvn_st_arena_alloc(&run->arena, size, align);
    if (!address) LOG("selftest: the resource cases are skipped, the arena is full\n");
    return address;
}

static uint64_t res_obj(NvnStRun* run) { return res_alloc(run, RES_OBJ_BYTES, 16); }

#define RES_CALL(c, p, host, ...) \
    nvn_st_call((c), (host) ? &(p)->host : &(p)->lib, &(NvnStCall){.x = {__VA_ARGS__}})

static void res_add(NvnStRec* rec, const char* name, uint64_t value) { nvn_st_add_ret(rec, name, value); }
static void res_add_same(NvnStRec* rec, const char* name, uint64_t value, uint64_t object) {
    nvn_st_add_ret(rec, name, value == object ? 1u : 0u);
}
static void res_add_nonzero(NvnStRec* rec, const char* name, uint64_t value) {
    nvn_st_add_ret(rec, name, value != 0 ? 1u : 0u);
}
/* The offset of an address from the pool of the same side. The relative value is the same on both sides. */
static void res_add_delta(NvnStRec* rec, const char* name, uint64_t value, uint64_t base) {
    nvn_st_add_ret(rec, name, value >= base ? value - base : UINT64_MAX);
}

static void res_get(CPU* c, const NvnStProc* p, int host, uint64_t object, uint64_t out, const ResGet* g,
                    NvnStRec* rec) {
    NvnStCall a = {.x = {object}};
    for (unsigned i = 0; i < g->outs; i++) a.x[1 + i] = out + RES_OUT_GUARD + (uint64_t)i * RES_OUT_SLOT;
    uint32_t s0 = 0;
    if (g->ret == 2) a.s0_out = &s0;
    if (g->outs) memset(GP(out), RES_OUT_FILL, RES_OUT_BYTES);
    uint64_t result = nvn_st_call(c, host ? &p->host : &p->lib, &a);
    if (g->ret == 2) res_add(rec, g->field, s0);
    else if (g->ret == 1) res_add(rec, g->field, result);
    if (g->outs) nvn_st_add_blob(rec, g->field, GP(out), NULL, RES_OUT_BYTES);
}

static void res_report(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref,
                       const NvnStRec* got) {
    NvnStDiff diff;
    nvn_st_compare(ref, got, &diff);
    nvn_st_report(run, name, args, ref, &diff);
}

/* Makes one memory pool of bytes for one side, with its own builder object and storage block. */
int nvn_st_res_pool_create(CPU* c, NvnStRun* run, NvnStResCache* cache, int host, NvnStResPool* mp, uint32_t flags,
                           uint64_t bytes) {
    NvnStProc* set_defaults = nvn_st_res_proc(cache, "nvnMemoryPoolBuilderSetDefaults");
    NvnStProc* set_device = nvn_st_res_proc(cache, "nvnMemoryPoolBuilderSetDevice");
    NvnStProc* set_flags = nvn_st_res_proc(cache, "nvnMemoryPoolBuilderSetFlags");
    NvnStProc* set_storage = nvn_st_res_proc(cache, "nvnMemoryPoolBuilderSetStorage");
    NvnStProc* initialize = nvn_st_res_proc(cache, "nvnMemoryPoolInitialize");
    if (!set_defaults || !set_device || !set_flags || !set_storage || !initialize) return 0;
    mp->builder = res_obj(run);
    mp->pool = res_obj(run);
    mp->storage = res_alloc(run, bytes, RES_POOL_ALIGN);
    if (!mp->builder || !mp->pool || !mp->storage) return 0;
    memset(GP(mp->builder), 0, RES_OBJ_BYTES);
    RES_CALL(c, set_defaults, host, mp->builder);
    RES_CALL(c, set_device, host, mp->builder, nvn_st_device());
    RES_CALL(c, set_flags, host, mp->builder, flags);
    RES_CALL(c, set_storage, host, mp->builder, mp->storage, bytes);
    return (uint32_t)RES_CALL(c, initialize, host, mp->pool, mp->builder) != 0;
}

void nvn_st_res_pool_destroy(CPU* c, NvnStResCache* cache, int host, const NvnStResPool* mp) {
    NvnStProc* finalize = nvn_st_res_proc(cache, "nvnMemoryPoolFinalize");
    if (finalize) RES_CALL(c, finalize, host, mp->pool);
}

/* One 2D texture of the command buffer cases: level 1, the format, and the side's pool at the offset. The caller
   clears the builder and gives the builder and texture objects. */
int nvn_st_texture_build(CPU* c, NvnStResCache* cache, int host, uint64_t builder, uint64_t texture,
                         const NvnStResPool* pool, uint64_t offset, uint32_t width, uint32_t height,
                         uint32_t format) {
    NvnStProc* defaults = nvn_st_res_proc(cache, "nvnTextureBuilderSetDefaults");
    NvnStProc* device = nvn_st_res_proc(cache, "nvnTextureBuilderSetDevice");
    NvnStProc* target = nvn_st_res_proc(cache, "nvnTextureBuilderSetTarget");
    NvnStProc* size = nvn_st_res_proc(cache, "nvnTextureBuilderSetSize2D");
    NvnStProc* levels = nvn_st_res_proc(cache, "nvnTextureBuilderSetLevels");
    NvnStProc* fmt = nvn_st_res_proc(cache, "nvnTextureBuilderSetFormat");
    NvnStProc* storage = nvn_st_res_proc(cache, "nvnTextureBuilderSetStorage");
    NvnStProc* initialize = nvn_st_res_proc(cache, "nvnTextureInitialize");
    if (!defaults || !device || !target || !size || !levels || !fmt || !storage || !initialize) return 0;
    RES_CALL(c, defaults, host, builder);
    RES_CALL(c, device, host, builder, nvn_st_device());
    RES_CALL(c, target, host, builder, RES_TEX_TARGET_2D);
    RES_CALL(c, size, host, builder, width, height);
    RES_CALL(c, levels, host, builder, 1);
    RES_CALL(c, fmt, host, builder, format);
    RES_CALL(c, storage, host, builder, pool->pool, offset);
    return (uint32_t)RES_CALL(c, initialize, host, texture, builder) != 0;
}

/* One texture or sampler pool of the command buffer cases at the offset of the side's memory pool. */
int nvn_st_descriptor_pool_build(CPU* c, NvnStResCache* cache, int host, uint64_t pool_obj,
                                 const NvnStResPool* memory, uint64_t offset, uint32_t entries, int sampler) {
    NvnStProc* initialize =
        nvn_st_res_proc(cache, sampler ? "nvnSamplerPoolInitialize" : "nvnTexturePoolInitialize");
    if (!initialize) return 0;
    return (uint32_t)RES_CALL(c, initialize, host, pool_obj, memory->pool, offset, (uint64_t)entries) != 0;
}

/* The memory pool: the builder setters and getters, then Initialize and the pool getters (decision 3). */
static const uint32_t res_pool_flags[] = {RES_POOL_CPU_CACHED, RES_POOL_CPU_UNCACHED_GPU_CACHED,
                                          RES_POOL_CPU_UNCACHED_GPU_UNCACHED, RES_POOL_CPU_NO_ACCESS};

static void case_memory_pool(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* set_defaults = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetDefaults");
    NvnStProc* set_device = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetDevice");
    NvnStProc* set_flags = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetFlags");
    NvnStProc* set_storage = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetStorage");
    NvnStProc* get_device = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderGetDevice");
    NvnStProc* get_memory = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderGetMemory");
    NvnStProc* get_builder_size = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderGetSize");
    NvnStProc* get_builder_flags = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderGetFlags");
    NvnStProc* initialize = nvn_st_res_proc(&cache, "nvnMemoryPoolInitialize");
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnMemoryPoolFinalize");
    NvnStProc* get_address = nvn_st_res_proc(&cache, "nvnMemoryPoolGetBufferAddress");
    NvnStProc* get_flags = nvn_st_res_proc(&cache, "nvnMemoryPoolGetFlags");
    NvnStProc* get_size = nvn_st_res_proc(&cache, "nvnMemoryPoolGetSize");
    NvnStProc* map = nvn_st_res_proc(&cache, "nvnMemoryPoolMap");
    NvnStProc* label = nvn_st_res_proc(&cache, "nvnMemoryPoolSetDebugLabel");
    NvnStProc* flush = nvn_st_res_proc(&cache, "nvnMemoryPoolFlushMappedRange");
    NvnStProc* invalidate = nvn_st_res_proc(&cache, "nvnMemoryPoolInvalidateMappedRange");
    if (!set_defaults || !set_device || !set_flags || !set_storage || !get_device || !get_memory ||
        !get_builder_size || !get_builder_flags || !initialize || !finalize || !get_address || !get_flags ||
        !get_size || !map || !label || !flush || !invalidate) {
        run->skipped++;
        return;
    }
    for (size_t f = 0; f < ARRAY_LEN(res_pool_flags); f++) {
        NvnStRec ref = {0}, got = {0};
        uint64_t builder[2], pool[2], storage[2];
        int ok = 1;
        for (int host = 0; host < 2; host++) {
            builder[host] = res_obj(run);
            pool[host] = res_obj(run);
            storage[host] = res_alloc(run, RES_POOL_BYTES, RES_POOL_ALIGN);
            if (!builder[host] || !pool[host] || !storage[host]) ok = 0;
        }
        if (!ok) {
            run->skipped++;
            return;
        }
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            memset(GP(builder[host]), 0, RES_OBJ_BYTES);
            RES_CALL(c, set_defaults, host, builder[host]);
            RES_CALL(c, set_device, host, builder[host], nvn_st_device());
            RES_CALL(c, set_flags, host, builder[host], res_pool_flags[f]);
            RES_CALL(c, set_storage, host, builder[host], storage[host], (uint64_t)RES_POOL_BYTES);
            res_add(rec, "builder.device", RES_CALL(c, get_device, host, builder[host]));
            res_add_nonzero(rec, "builder.memory", RES_CALL(c, get_memory, host, builder[host]));
            res_add(rec, "builder.size", RES_CALL(c, get_builder_size, host, builder[host]));
            res_add(rec, "builder.flags", RES_CALL(c, get_builder_flags, host, builder[host]));
            res_add(rec, "initialize", RES_CALL(c, initialize, host, pool[host], builder[host]));
            uint64_t address = RES_CALL(c, get_address, host, pool[host]);
            res_add_nonzero(rec, "address", address);
            res_add(rec, "addressAligned", (address & (RES_POOL_ALIGN - 1u)) == 0);
            res_add(rec, "flags", RES_CALL(c, get_flags, host, pool[host]));
            res_add(rec, "size", RES_CALL(c, get_size, host, pool[host]));
            res_add_nonzero(rec, "map", RES_CALL(c, map, host, pool[host]));
            RES_CALL(c, label, host, pool[host], 0);
            RES_CALL(c, flush, host, pool[host], 0, (uint64_t)RES_POOL_BYTES); /* the library tail-calls the driver */
            RES_CALL(c, invalidate, host, pool[host], 0, (uint64_t)RES_POOL_BYTES);
            RES_CALL(c, finalize, host, pool[host]);
        }
        char args[32];
        snprintf(args, sizeof args, "flags=0x%x", res_pool_flags[f]);
        res_report(run, "nvnMemoryPool", args, &ref, &got);
    }
}

/* nvnMemoryPoolMapVirtual: a physical pool and a virtual pool, then one request of 64 KiB (decision 6 of the U3
   open items). The virtual flag is not in enum-run2: only this function needs it. */
static void case_memory_pool_map_virtual(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* set_defaults = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetDefaults");
    NvnStProc* set_device = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetDevice");
    NvnStProc* set_flags = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetFlags");
    NvnStProc* set_storage = nvn_st_res_proc(&cache, "nvnMemoryPoolBuilderSetStorage");
    NvnStProc* initialize = nvn_st_res_proc(&cache, "nvnMemoryPoolInitialize");
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnMemoryPoolFinalize");
    NvnStProc* map_virtual = nvn_st_res_proc(&cache, "nvnMemoryPoolMapVirtual");
    if (!set_defaults || !set_device || !set_flags || !set_storage || !initialize || !finalize || !map_virtual) {
        run->skipped++;
        return;
    }
    NvnStResPool physical[2];
    if (!nvn_st_res_pool_create(c, run, &cache, 0, &physical[0], RES_POOL_CPU_CACHED, RES_TEX_POOL_BYTES) ||
        !nvn_st_res_pool_create(c, run, &cache, 1, &physical[1], RES_POOL_CPU_CACHED, RES_TEX_POOL_BYTES)) {
        run->skipped++;
        return;
    }
    uint64_t virtual_builder[2], virtual_pool[2], request = res_alloc(run, 0x40, 16);
    NvnStRec ref = {0}, got = {0};
    int ok = request != 0;
    for (int host = 0; host < 2; host++) {
        virtual_builder[host] = res_obj(run);
        virtual_pool[host] = res_obj(run);
        if (!virtual_builder[host] || !virtual_pool[host]) ok = 0;
    }
    if (!ok) {
        run->skipped++;
        return;
    }
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        memset(GP(virtual_builder[host]), 0, RES_OBJ_BYTES);
        RES_CALL(c, set_defaults, host, virtual_builder[host]);
        RES_CALL(c, set_device, host, virtual_builder[host], nvn_st_device());
        RES_CALL(c, set_flags, host, virtual_builder[host], RES_POOL_VIRTUAL);
        RES_CALL(c, set_storage, host, virtual_builder[host], 0, (uint64_t)RES_TEX_POOL_BYTES);
        res_add(rec, "initialize", RES_CALL(c, initialize, host, virtual_pool[host], virtual_builder[host]));
        memset(GP(request), 0, 0x40);
        gw64(request, physical[host].pool);
        gw64(request + 0x18, RES_TEX_POOL_BYTES);
        res_add(rec, "mapped", RES_CALL(c, map_virtual, host, virtual_pool[host], 1, request));
        RES_CALL(c, finalize, host, virtual_pool[host]);
    }
    nvn_st_res_pool_destroy(c, &cache, 0, &physical[0]);
    nvn_st_res_pool_destroy(c, &cache, 1, &physical[1]);
    res_report(run, "nvnMemoryPoolMapVirtual", "one 64 KiB page", &ref, &got);
}

/* The buffer: the builder storage, Initialize, the getters, Map and the debug label. The address is relative to the
   pool of the side. */
typedef struct {
    uint64_t offset, size;
} ResBufferCase;

static const ResBufferCase res_buffer_cases[] = {{0, 0x1000}, {0x1000, 0x2000}, {0x2000, 0x1000}};

static void case_buffer(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* builder_defaults = nvn_st_res_proc(&cache, "nvnBufferBuilderSetDefaults");
    NvnStProc* builder_device = nvn_st_res_proc(&cache, "nvnBufferBuilderSetDevice");
    NvnStProc* builder_storage = nvn_st_res_proc(&cache, "nvnBufferBuilderSetStorage");
    NvnStProc* builder_size = nvn_st_res_proc(&cache, "nvnBufferBuilderGetSize");
    NvnStProc* builder_get_device = nvn_st_res_proc(&cache, "nvnBufferBuilderGetDevice");
    NvnStProc* builder_pool = nvn_st_res_proc(&cache, "nvnBufferBuilderGetMemoryPool");
    NvnStProc* builder_offset = nvn_st_res_proc(&cache, "nvnBufferBuilderGetMemoryOffset");
    NvnStProc* initialize = nvn_st_res_proc(&cache, "nvnBufferInitialize");
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnBufferFinalize");
    NvnStProc* address = nvn_st_res_proc(&cache, "nvnBufferGetAddress");
    NvnStProc* size = nvn_st_res_proc(&cache, "nvnBufferGetSize");
    NvnStProc* pool = nvn_st_res_proc(&cache, "nvnBufferGetMemoryPool");
    NvnStProc* offset = nvn_st_res_proc(&cache, "nvnBufferGetMemoryOffset");
    NvnStProc* debug_id = nvn_st_res_proc(&cache, "nvnBufferGetDebugID");
    NvnStProc* map = nvn_st_res_proc(&cache, "nvnBufferMap");
    NvnStProc* label = nvn_st_res_proc(&cache, "nvnBufferSetDebugLabel");
    NvnStProc* flush = nvn_st_res_proc(&cache, "nvnBufferFlushMappedRange");
    NvnStProc* invalidate = nvn_st_res_proc(&cache, "nvnBufferInvalidateMappedRange");
    NvnStProc* pool_address = nvn_st_res_proc(&cache, "nvnMemoryPoolGetBufferAddress");
    if (!builder_defaults || !builder_device || !builder_storage || !builder_size || !builder_get_device ||
        !builder_pool || !builder_offset || !initialize || !finalize || !address || !size || !pool || !offset ||
        !debug_id || !map || !label || !flush || !invalidate || !pool_address) {
        run->skipped++;
        return;
    }
    NvnStResPool memory[2];
    if (!nvn_st_res_pool_create(c, run, &cache, 0, &memory[0], RES_POOL_CPU_CACHED, RES_POOL_BYTES) ||
        !nvn_st_res_pool_create(c, run, &cache, 1, &memory[1], RES_POOL_CPU_CACHED, RES_POOL_BYTES)) {
        run->skipped++;
        return;
    }
    for (size_t k = 0; k < ARRAY_LEN(res_buffer_cases); k++) {
        NvnStRec ref = {0}, got = {0};
        uint64_t builder[2], buffer[2];
        int ok = 1;
        for (int host = 0; host < 2; host++) {
            builder[host] = res_obj(run);
            buffer[host] = res_obj(run);
            if (!builder[host] || !buffer[host]) ok = 0;
        }
        if (!ok) {
            run->skipped++;
            return;
        }
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            memset(GP(builder[host]), 0, RES_OBJ_BYTES);
            RES_CALL(c, builder_defaults, host, builder[host]);
            RES_CALL(c, builder_device, host, builder[host], nvn_st_device());
            RES_CALL(c, builder_storage, host, builder[host], memory[host].pool, res_buffer_cases[k].offset,
                     res_buffer_cases[k].size);
            res_add(rec, "builder.size", RES_CALL(c, builder_size, host, builder[host]));
            res_add(rec, "builder.device", RES_CALL(c, builder_get_device, host, builder[host]));
            res_add_same(rec, "builder.pool", RES_CALL(c, builder_pool, host, builder[host]), memory[host].pool);
            res_add(rec, "builder.offset", RES_CALL(c, builder_offset, host, builder[host]));
            res_add(rec, "initialize", RES_CALL(c, initialize, host, buffer[host], builder[host]));
            res_add_delta(rec, "address", RES_CALL(c, address, host, buffer[host]),
                          RES_CALL(c, pool_address, host, memory[host].pool));
            res_add(rec, "size", RES_CALL(c, size, host, buffer[host]));
            res_add_same(rec, "pool", RES_CALL(c, pool, host, buffer[host]), memory[host].pool);
            res_add(rec, "offset", RES_CALL(c, offset, host, buffer[host]));
            res_add_nonzero(rec, "debugIdNonzero", RES_CALL(c, debug_id, host, buffer[host]));
            res_add_nonzero(rec, "map", RES_CALL(c, map, host, buffer[host]));
            RES_CALL(c, label, host, buffer[host], 0);
            RES_CALL(c, flush, host, buffer[host], 0, res_buffer_cases[k].size);
            RES_CALL(c, invalidate, host, buffer[host], 0, res_buffer_cases[k].size);
            RES_CALL(c, finalize, host, buffer[host]);
        }
        char args[48];
        snprintf(args, sizeof args, "offset=0x%llx size=0x%llx", (unsigned long long)res_buffer_cases[k].offset,
                 (unsigned long long)res_buffer_cases[k].size);
        res_report(run, "nvnBuffer", args, &ref, &got);
    }
    nvn_st_res_pool_destroy(c, &cache, 0, &memory[0]);
    nvn_st_res_pool_destroy(c, &cache, 1, &memory[1]);
}

/* The texture: linear 2D formats of enum-run2, one block-linear 2D texture, and one equal copy. The transfers use the
   block-linear texture (the host refuses a linear texture, as the library does). */
typedef struct {
    uint32_t flags, format, stride;
    uint64_t offset;
    const char* args;
} ResTexCase;

static const ResTexCase res_texture_cases[] = {
    {RES_TEX_FLAG_LINEAR, RES_TEX_FORMAT_A, 128u, 0, "linear fmt=0x25 32x32"},
    {0, RES_TEX_FORMAT_A, 0, 0x4000, "block fmt=0x25 32x32"},
    {0, RES_TEX_FORMAT_B, 0, 0x8000, "block fmt=0x26 32x32"},
    {RES_TEX_FLAG_LINEAR, RES_TEX_FORMAT_A, 128u, 0xC000, "linear fmt=0x25 32x32 equal pair"},
};

static const ResGet res_texture_gets[] = {
    {"width", 0, 1},
    {"height", 0, 1},
    {"depth", 0, 1},
    {"stride", 0, 1},
    {"target", 0, 1},
    {"format", 0, 1},
    {"levels", 0, 1},
    {"flags", 0, 1},
    {"samples", 0, 1},
    {"depthStencilMode", 0, 1},
    {"storageSize", 0, 1},
    {"storageClass", 0, 1},
    {"rawStorageClass", 0, 1},
    {"zcullStorageSize", 0, 1},
    {"memoryOffset", 0, 1},
};

static const char* const res_texture_get_names[] = {
    "nvnTextureGetWidth",     "nvnTextureGetHeight",        "nvnTextureGetDepth",
    "nvnTextureGetStride",    "nvnTextureGetTarget",        "nvnTextureGetFormat",
    "nvnTextureGetLevels",    "nvnTextureGetFlags",         "nvnTextureGetSamples",
    "nvnTextureGetDepthStencilMode", "nvnTextureGetStorageSize", "nvnTextureGetStorageClass",
    "nvnTextureGetRawStorageClass", "nvnTextureGetZCullStorageSize", "nvnTextureGetMemoryOffset",
};

static void texture_transfer(CPU* c, const NvnStProc* write, const NvnStProc* read, uint64_t texture,
                             uint64_t region, uint64_t source, uint64_t destination, uint32_t row_stride, int host,
                             NvnStRec* rec) {
    memset(GP(destination), 0, RES_TEX_BUF_BYTES);
    RES_CALL(c, write, host, texture, 0, region, source, row_stride, 0);
    RES_CALL(c, read, host, texture, 0, region, destination, row_stride, 0);
    nvn_st_add_blob(rec, "read", GP(destination), NULL, RES_TEX_BUF_BYTES);
}

static void case_texture(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* builder_defaults = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDefaults");
    NvnStProc* builder_device = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDevice");
    NvnStProc* builder_flags = nvn_st_res_proc(&cache, "nvnTextureBuilderSetFlags");
    NvnStProc* builder_target = nvn_st_res_proc(&cache, "nvnTextureBuilderSetTarget");
    NvnStProc* builder_size = nvn_st_res_proc(&cache, "nvnTextureBuilderSetSize2D");
    NvnStProc* builder_levels = nvn_st_res_proc(&cache, "nvnTextureBuilderSetLevels");
    NvnStProc* builder_format = nvn_st_res_proc(&cache, "nvnTextureBuilderSetFormat");
    NvnStProc* builder_stride = nvn_st_res_proc(&cache, "nvnTextureBuilderSetStride");
    NvnStProc* builder_storage = nvn_st_res_proc(&cache, "nvnTextureBuilderSetStorage");
    NvnStProc* initialize = nvn_st_res_proc(&cache, "nvnTextureInitialize");
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnTextureFinalize");
    NvnStProc* compare = nvn_st_res_proc(&cache, "nvnTextureCompare");
    NvnStProc* pool = nvn_st_res_proc(&cache, "nvnTextureGetMemoryPool");
    NvnStProc* address = nvn_st_res_proc(&cache, "nvnTextureGetTextureAddress");
    NvnStProc* view_offset = nvn_st_res_proc(&cache, "nvnTextureGetViewOffset");
    NvnStProc* swizzle = nvn_st_res_proc(&cache, "nvnTextureGetSwizzle");
    NvnStProc* sparse = nvn_st_res_proc(&cache, "nvnTextureGetSparseTileLayout");
    NvnStProc* debug_id = nvn_st_res_proc(&cache, "nvnTextureGetDebugID");
    NvnStProc* label = nvn_st_res_proc(&cache, "nvnTextureSetDebugLabel");
    NvnStProc* flush = nvn_st_res_proc(&cache, "nvnTextureFlushTexels");
    NvnStProc* invalidate = nvn_st_res_proc(&cache, "nvnTextureInvalidateTexels");
    NvnStProc* write = nvn_st_res_proc(&cache, "nvnTextureWriteTexels");
    NvnStProc* read = nvn_st_res_proc(&cache, "nvnTextureReadTexels");
    NvnStProc* write_strided = nvn_st_res_proc(&cache, "nvnTextureWriteTexelsStrided");
    NvnStProc* read_strided = nvn_st_res_proc(&cache, "nvnTextureReadTexelsStrided");
    NvnStProc* pool_address = nvn_st_res_proc(&cache, "nvnMemoryPoolGetBufferAddress");
    NvnStProc* gets[ARRAY_LEN(res_texture_get_names)];
    if (!builder_defaults || !builder_device || !builder_flags || !builder_target || !builder_size ||
        !builder_levels || !builder_format || !builder_stride || !builder_storage || !initialize || !finalize ||
        !compare || !pool || !address || !view_offset || !swizzle || !sparse || !debug_id || !label || !flush ||
        !invalidate || !write || !read || !write_strided || !read_strided || !pool_address) {
        run->skipped++;
        return;
    }
    for (size_t i = 0; i < ARRAY_LEN(res_texture_get_names); i++) {
        gets[i] = nvn_st_res_proc(&cache, res_texture_get_names[i]);
        if (!gets[i]) {
            run->skipped++;
            return;
        }
    }
    NvnStResPool memory[2];
    if (!nvn_st_res_pool_create(c, run, &cache, 0, &memory[0], RES_POOL_CPU_CACHED, RES_TEX_POOL_BYTES) ||
        !nvn_st_res_pool_create(c, run, &cache, 1, &memory[1], RES_POOL_CPU_CACHED, RES_TEX_POOL_BYTES)) {
        run->skipped++;
        return;
    }
    uint64_t tex[2][ARRAY_LEN(res_texture_cases)], pool_gpu[2];
    int ok = 1;
    for (int host = 0; host < 2; host++) {
        pool_gpu[host] = RES_CALL(c, pool_address, host, memory[host].pool);
        for (size_t k = 0; k < ARRAY_LEN(res_texture_cases); k++) {
            tex[host][k] = res_obj(run);
            if (!tex[host][k]) ok = 0;
        }
    }
    if (!ok) {
        run->skipped++;
        return;
    }
    for (size_t k = 0; k < ARRAY_LEN(res_texture_cases); k++) {
        NvnStRec ref = {0}, got = {0};
        const ResTexCase* kase = &res_texture_cases[k];
        const ResGet swizzle_get = {"swizzle", 4, 0};
        const ResGet sparse_get = {"sparse", 1, 0};
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            uint64_t builder = res_obj(run);
            uint64_t out = res_alloc(run, 2u * RES_OUT_BYTES, 16);
            if (!builder || !out) {
                run->skipped++;
                return;
            }
            memset(GP(builder), 0, RES_OBJ_BYTES);
            RES_CALL(c, builder_defaults, host, builder);
            RES_CALL(c, builder_device, host, builder, nvn_st_device());
            RES_CALL(c, builder_flags, host, builder, kase->flags);
            RES_CALL(c, builder_target, host, builder, RES_TEX_TARGET_2D);
            RES_CALL(c, builder_size, host, builder, RES_TEX_SIDE, RES_TEX_SIDE);
            RES_CALL(c, builder_levels, host, builder, 1);
            RES_CALL(c, builder_format, host, builder, kase->format);
            RES_CALL(c, builder_stride, host, builder, kase->stride);
            RES_CALL(c, builder_storage, host, builder, memory[host].pool, kase->offset);
            res_add(rec, "initialize", RES_CALL(c, initialize, host, tex[host][k], builder));
            for (size_t i = 0; i < ARRAY_LEN(gets); i++) res_get(c, gets[i], host, tex[host][k], out, &res_texture_gets[i], rec);
            res_add_same(rec, "memoryPool", RES_CALL(c, pool, host, tex[host][k]), memory[host].pool);
            res_add_delta(rec, "address", RES_CALL(c, address, host, tex[host][k]), pool_gpu[host]);
            res_add(rec, "viewOffset", RES_CALL(c, view_offset, host, tex[host][k], 0));
            res_get(c, swizzle, host, tex[host][k], out, &swizzle_get, rec);
            res_get(c, sparse, host, tex[host][k], out + RES_OUT_BYTES, &sparse_get, rec);
            res_add_nonzero(rec, "debugIdNonzero", RES_CALL(c, debug_id, host, tex[host][k]));
            RES_CALL(c, label, host, tex[host][k], 0);
            RES_CALL(c, flush, host, tex[host][k]);
            RES_CALL(c, invalidate, host, tex[host][k]);
        }
        res_report(run, "nvnTexture", kase->args, &ref, &got);
    }
    /* Two equal textures and two different ones (decision 7 of this step). */
    {
        NvnStRec ref = {0}, got = {0};
        for (int host = 0; host < 2; host++) {
            NvnStRec* rec = host ? &got : &ref;
            res_add(rec, "equal", RES_CALL(c, compare, host, tex[host][0], tex[host][3]));
            res_add(rec, "differentFormat", RES_CALL(c, compare, host, tex[host][0], tex[host][2]));
            res_add(rec, "differentFlags", RES_CALL(c, compare, host, tex[host][0], tex[host][1]));
        }
        res_report(run, "nvnTextureCompare", "equal and different pairs", &ref, &got);
    }
    /* WriteTexels and ReadTexels, tight and with a stride, on a 16x16 region of the block-linear texture. */
    {
        uint64_t region = res_alloc(run, 0x20, 16);
        uint64_t source = res_alloc(run, RES_TEX_BUF_BYTES, 16);
        uint64_t destination[2];
        int filled = region != 0 && source != 0;
        for (int host = 0; host < 2; host++) {
            destination[host] = res_alloc(run, RES_TEX_BUF_BYTES, 16);
            if (!destination[host]) filled = 0;
        }
        if (!filled) {
            run->skipped++;
            return;
        }
        memset(GP(region), 0, 0x20);
        gw32(region + 0x0c, RES_TEX_REGION);
        gw32(region + 0x10, RES_TEX_REGION);
        gw32(region + 0x14, 1);
        for (uint64_t i = 0; i < RES_TEX_BUF_BYTES; i++) *GP(source + i) = (uint8_t)(i * 37u + 11u);
        NvnStRec ref = {0}, got = {0};
        for (int host = 0; host < 2; host++)
            texture_transfer(c, write, read, tex[host][1], region, source, destination[host], 0, host,
                             host ? &got : &ref);
        res_report(run, "nvnTextureTransfer", "WriteTexels/ReadTexels 16x16", &ref, &got);
        memset(&ref, 0, sizeof ref);
        memset(&got, 0, sizeof got);
        for (int host = 0; host < 2; host++)
            texture_transfer(c, write_strided, read_strided, tex[host][1], region, source, destination[host],
                             RES_TEX_STRIDE, host, host ? &got : &ref);
        res_report(run, "nvnTextureTransferStrided", "16 rows of 256 bytes", &ref, &got);
    }
    for (int host = 0; host < 2; host++) {
        for (size_t k = 0; k < ARRAY_LEN(res_texture_cases); k++) RES_CALL(c, finalize, host, tex[host][k]);
    }
    nvn_st_res_pool_destroy(c, &cache, 0, &memory[0]);
    nvn_st_res_pool_destroy(c, &cache, 1, &memory[1]);
}

/* The descriptor pools: 512 entries, the registers at ids 256 and 257, the pool getters and the device handles. The
   texture descriptor holds the texture address in words 1 and 2, so those bytes are on the ignore list. */
static const uint8_t res_texture_descriptor_mask[RES_DESC_BYTES] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static void case_descriptor_pools(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* texture_pool_init = nvn_st_res_proc(&cache, "nvnTexturePoolInitialize");
    NvnStProc* texture_pool_final = nvn_st_res_proc(&cache, "nvnTexturePoolFinalize");
    NvnStProc* texture_pool_size = nvn_st_res_proc(&cache, "nvnTexturePoolGetSize");
    NvnStProc* texture_pool_memory = nvn_st_res_proc(&cache, "nvnTexturePoolGetMemoryPool");
    NvnStProc* texture_pool_offset = nvn_st_res_proc(&cache, "nvnTexturePoolGetMemoryOffset");
    NvnStProc* texture_pool_label = nvn_st_res_proc(&cache, "nvnTexturePoolSetDebugLabel");
    NvnStProc* register_texture = nvn_st_res_proc(&cache, "nvnTexturePoolRegisterTexture");
    NvnStProc* register_image = nvn_st_res_proc(&cache, "nvnTexturePoolRegisterImage");
    NvnStProc* sampler_pool_init = nvn_st_res_proc(&cache, "nvnSamplerPoolInitialize");
    NvnStProc* sampler_pool_final = nvn_st_res_proc(&cache, "nvnSamplerPoolFinalize");
    NvnStProc* sampler_pool_size = nvn_st_res_proc(&cache, "nvnSamplerPoolGetSize");
    NvnStProc* sampler_pool_memory = nvn_st_res_proc(&cache, "nvnSamplerPoolGetMemoryPool");
    NvnStProc* sampler_pool_offset = nvn_st_res_proc(&cache, "nvnSamplerPoolGetMemoryOffset");
    NvnStProc* sampler_pool_label = nvn_st_res_proc(&cache, "nvnSamplerPoolSetDebugLabel");
    NvnStProc* register_sampler = nvn_st_res_proc(&cache, "nvnSamplerPoolRegisterSampler");
    NvnStProc* register_builder = nvn_st_res_proc(&cache, "nvnSamplerPoolRegisterSamplerBuilder");
    NvnStProc* texture_handle = nvn_st_res_proc(&cache, "nvnDeviceGetTextureHandle");
    NvnStProc* image_handle = nvn_st_res_proc(&cache, "nvnDeviceGetImageHandle");
    NvnStProc* fetch_handle = nvn_st_res_proc(&cache, "nvnDeviceGetTexelFetchHandle");
    NvnStProc* separate_texture = nvn_st_res_proc(&cache, "nvnDeviceGetSeparateTextureHandle");
    NvnStProc* separate_sampler = nvn_st_res_proc(&cache, "nvnDeviceGetSeparateSamplerHandle");
    NvnStProc* texture_defaults = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDefaults");
    NvnStProc* texture_device = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDevice");
    NvnStProc* texture_size = nvn_st_res_proc(&cache, "nvnTextureBuilderSetSize2D");
    NvnStProc* texture_levels = nvn_st_res_proc(&cache, "nvnTextureBuilderSetLevels");
    NvnStProc* texture_format = nvn_st_res_proc(&cache, "nvnTextureBuilderSetFormat");
    NvnStProc* texture_storage = nvn_st_res_proc(&cache, "nvnTextureBuilderSetStorage");
    NvnStProc* texture_init = nvn_st_res_proc(&cache, "nvnTextureInitialize");
    NvnStProc* texture_final = nvn_st_res_proc(&cache, "nvnTextureFinalize");
    NvnStProc* sampler_builder = nvn_st_res_proc(&cache, "nvnSamplerBuilderSetDefaults");
    NvnStProc* sampler_init = nvn_st_res_proc(&cache, "nvnSamplerInitialize");
    NvnStProc* sampler_final = nvn_st_res_proc(&cache, "nvnSamplerFinalize");
    if (!texture_pool_init || !texture_pool_final || !texture_pool_size || !texture_pool_memory ||
        !texture_pool_offset || !texture_pool_label || !register_texture || !register_image || !sampler_pool_init ||
        !sampler_pool_final || !sampler_pool_size || !sampler_pool_memory || !sampler_pool_offset ||
        !sampler_pool_label || !register_sampler || !register_builder || !texture_handle || !image_handle ||
        !fetch_handle || !separate_texture || !separate_sampler || !texture_defaults || !texture_device ||
        !texture_size || !texture_levels || !texture_format || !texture_storage || !texture_init || !texture_final ||
        !sampler_builder || !sampler_init || !sampler_final) {
        run->skipped++;
        return;
    }
    NvnStResPool memory[2];
    if (!nvn_st_res_pool_create(c, run, &cache, 0, &memory[0], RES_POOL_CPU_CACHED, RES_DESC_POOL_BYTES) ||
        !nvn_st_res_pool_create(c, run, &cache, 1, &memory[1], RES_POOL_CPU_CACHED, RES_DESC_POOL_BYTES)) {
        run->skipped++;
        return;
    }
    uint64_t texture[2], texture_builder[2], sampler[2], sampler_builder_obj[2];
    NvnStRec ref = {0}, got = {0};
    int ok = 1;
    for (int host = 0; host < 2; host++) {
        texture[host] = res_obj(run);
        texture_builder[host] = res_obj(run);
        sampler[host] = res_obj(run);
        sampler_builder_obj[host] = res_obj(run);
        if (!texture[host] || !texture_builder[host] || !sampler[host] || !sampler_builder_obj[host]) ok = 0;
    }
    if (!ok) {
        run->skipped++;
        return;
    }
    /* The texture and the sampler that the tables hold. */
    for (int host = 0; host < 2; host++) {
        uint64_t data = res_alloc(run, RES_DATA_BYTES, 16);
        if (!data || !nvn_st_sampler_build(c, sampler_builder_obj[host], sampler[host], data, host, 0)) {
            run->skipped++;
            return;
        }
        memset(GP(texture_builder[host]), 0, RES_OBJ_BYTES);
        RES_CALL(c, texture_defaults, host, texture_builder[host]);
        RES_CALL(c, texture_device, host, texture_builder[host], nvn_st_device());
        RES_CALL(c, texture_size, host, texture_builder[host], RES_TEX_SIDE, RES_TEX_SIDE);
        RES_CALL(c, texture_levels, host, texture_builder[host], 1);
        RES_CALL(c, texture_format, host, texture_builder[host], RES_TEX_FORMAT_A);
        RES_CALL(c, texture_storage, host, texture_builder[host], memory[host].pool, RES_TEX_OFFSET);
        RES_CALL(c, texture_init, host, texture[host], texture_builder[host]);
    }
    uint64_t texture_pool[2], sampler_pool[2];
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        texture_pool[host] = res_obj(run);
        sampler_pool[host] = res_obj(run);
        if (!texture_pool[host] || !sampler_pool[host]) {
            run->skipped++;
            return;
        }
        res_add(rec, "texturePoolInitialize",
                RES_CALL(c, texture_pool_init, host, texture_pool[host], memory[host].pool, 0,
                         (uint64_t)RES_DESC_ENTRIES));
        res_add(rec, "texturePoolSize", RES_CALL(c, texture_pool_size, host, texture_pool[host]));
        res_add_same(rec, "texturePoolMemory", RES_CALL(c, texture_pool_memory, host, texture_pool[host]),
                     memory[host].pool);
        res_add(rec, "texturePoolOffset", RES_CALL(c, texture_pool_offset, host, texture_pool[host]));
        RES_CALL(c, texture_pool_label, host, texture_pool[host], 0);
        RES_CALL(c, register_texture, host, texture_pool[host], RES_DESC_ID_A, texture[host], 0);
        nvn_st_add_blob(rec, "registerTexture",
                        GP(memory[host].storage + RES_DESC_ID_A * RES_DESC_BYTES), res_texture_descriptor_mask,
                        RES_DESC_BYTES);
        RES_CALL(c, register_image, host, texture_pool[host], RES_DESC_ID_B, texture[host], 0);
        nvn_st_add_blob(rec, "registerImage",
                        GP(memory[host].storage + RES_DESC_ID_B * RES_DESC_BYTES), res_texture_descriptor_mask,
                        RES_DESC_BYTES);
        res_add(rec, "samplerPoolInitialize",
                RES_CALL(c, sampler_pool_init, host, sampler_pool[host], memory[host].pool, 0x4000,
                         (uint64_t)RES_DESC_ENTRIES));
        res_add(rec, "samplerPoolSize", RES_CALL(c, sampler_pool_size, host, sampler_pool[host]));
        res_add_same(rec, "samplerPoolMemory", RES_CALL(c, sampler_pool_memory, host, sampler_pool[host]),
                     memory[host].pool);
        res_add(rec, "samplerPoolOffset", RES_CALL(c, sampler_pool_offset, host, sampler_pool[host]));
        RES_CALL(c, sampler_pool_label, host, sampler_pool[host], 0);
        RES_CALL(c, register_sampler, host, sampler_pool[host], RES_DESC_ID_A, sampler[host]);
        nvn_st_add_blob(rec, "registerSampler",
                        GP(memory[host].storage + 0x4000 + RES_DESC_ID_A * RES_DESC_BYTES), NULL, RES_DESC_BYTES);
        RES_CALL(c, register_builder, host, sampler_pool[host], RES_DESC_ID_B, sampler_builder_obj[host]);
        nvn_st_add_blob(rec, "registerSamplerBuilder",
                        GP(memory[host].storage + 0x4000 + RES_DESC_ID_B * RES_DESC_BYTES), NULL, RES_DESC_BYTES);
        res_add(rec, "textureHandle", RES_CALL(c, texture_handle, host, nvn_st_device(), RES_DESC_ID_A, RES_DESC_ID_B));
        res_add(rec, "imageHandle", RES_CALL(c, image_handle, host, nvn_st_device(), RES_DESC_ID_A));
        res_add(rec, "fetchHandle", RES_CALL(c, fetch_handle, host, nvn_st_device(), RES_DESC_ID_A));
        res_add(rec, "separateTextureHandle", RES_CALL(c, separate_texture, host, nvn_st_device(), RES_DESC_ID_A));
        res_add(rec, "separateSamplerHandle", RES_CALL(c, separate_sampler, host, nvn_st_device(), RES_DESC_ID_B));
        RES_CALL(c, texture_pool_final, host, texture_pool[host]);
        RES_CALL(c, sampler_pool_final, host, sampler_pool[host]);
    }
    res_report(run, "nvnDescriptorPools", "512 entries, ids 256 and 257", &ref, &got);
    for (int host = 0; host < 2; host++) {
        RES_CALL(c, sampler_final, host, sampler[host]);
        RES_CALL(c, texture_final, host, texture[host]);
    }
    nvn_st_res_pool_destroy(c, &cache, 0, &memory[0]);
    nvn_st_res_pool_destroy(c, &cache, 1, &memory[1]);
}

/* The sampler objects: the values come from the sampler builder table of nvn_selftest_obj.c (decision 7). The getters
   with pointers compare their output region; the float getters compare the s0 bits. */
static void case_sampler(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnSamplerFinalize");
    NvnStProc* label = nvn_st_res_proc(&cache, "nvnSamplerSetDebugLabel");
    NvnStProc* min_mag = nvn_st_res_proc(&cache, "nvnSamplerGetMinMagFilter");
    NvnStProc* lod_clamp = nvn_st_res_proc(&cache, "nvnSamplerGetLodClamp");
    NvnStProc* compare = nvn_st_res_proc(&cache, "nvnSamplerGetCompare");
    NvnStProc* wrap = nvn_st_res_proc(&cache, "nvnSamplerGetWrapMode");
    NvnStProc* border = nvn_st_res_proc(&cache, "nvnSamplerGetBorderColor");
    NvnStProc* borderi = nvn_st_res_proc(&cache, "nvnSamplerGetBorderColori");
    NvnStProc* borderui = nvn_st_res_proc(&cache, "nvnSamplerGetBorderColorui");
    NvnStProc* lod_bias = nvn_st_res_proc(&cache, "nvnSamplerGetLodBias");
    NvnStProc* anisotropy = nvn_st_res_proc(&cache, "nvnSamplerGetMaxAnisotropy");
    NvnStProc* reduction = nvn_st_res_proc(&cache, "nvnSamplerGetReductionFilter");
    NvnStProc* debug_id = nvn_st_res_proc(&cache, "nvnSamplerGetDebugID");
    if (!finalize || !label || !min_mag || !lod_clamp || !compare || !wrap || !border || !borderi || !borderui ||
        !lod_bias || !anisotropy || !reduction || !debug_id) {
        run->skipped++;
        return;
    }
    uint64_t sampler[2][3], builder[2], data[2], out[2];
    NvnStRec ref = {0}, got = {0};
    int ok = 1;
    for (int host = 0; host < 2; host++) {
        builder[host] = res_obj(run);
        data[host] = res_alloc(run, RES_DATA_BYTES, 16);
        out[host] = res_alloc(run, 7u * RES_OUT_BYTES, 16);
        if (!builder[host] || !data[host] || !out[host]) ok = 0;
        for (unsigned i = 0; i < 3; i++) {
            sampler[host][i] = res_obj(run);
            if (!sampler[host][i]) ok = 0;
        }
    }
    if (!ok) {
        run->skipped++;
        return;
    }
    static const ResGet min_mag_get = {"minMagFilter", 2, 0};
    static const ResGet lod_clamp_get = {"lodClamp", 2, 0};
    static const ResGet compare_get = {"compare", 2, 0};
    static const ResGet wrap_get = {"wrapMode", 3, 0};
    static const ResGet border_get = {"borderColor", 1, 0};
    static const ResGet borderi_get = {"borderColori", 1, 0};
    static const ResGet borderui_get = {"borderColorui", 1, 0};
    static const ResGet lod_bias_get = {"lodBias", 0, 2};
    static const ResGet anisotropy_get = {"maxAnisotropy", 0, 2};
    static const ResGet reduction_get = {"reductionFilter", 0, 1};
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        for (unsigned i = 0; i < 3; i++) {
            if (!nvn_st_sampler_build(c, builder[host], sampler[host][i], data[host], host, i == 2 ? 2u : 0u)) {
                run->skipped++;
                return;
            }
        }
        uint64_t object = sampler[host][0];
        res_get(c, min_mag, host, object, out[host], &min_mag_get, rec);
        res_get(c, lod_clamp, host, object, out[host] + RES_OUT_BYTES, &lod_clamp_get, rec);
        res_get(c, compare, host, object, out[host] + 2u * RES_OUT_BYTES, &compare_get, rec);
        res_get(c, wrap, host, object, out[host] + 3u * RES_OUT_BYTES, &wrap_get, rec);
        res_get(c, border, host, object, out[host] + 4u * RES_OUT_BYTES, &border_get, rec);
        res_get(c, borderi, host, object, out[host] + 5u * RES_OUT_BYTES, &borderi_get, rec);
        res_get(c, borderui, host, object, out[host] + 6u * RES_OUT_BYTES, &borderui_get, rec);
        res_get(c, lod_bias, host, object, out[host], &lod_bias_get, rec);
        res_get(c, anisotropy, host, object, out[host], &anisotropy_get, rec);
        res_get(c, reduction, host, object, out[host], &reduction_get, rec);
        res_add_nonzero(rec, "debugIdNonzero", RES_CALL(c, debug_id, host, object));
        RES_CALL(c, label, host, object, 0);
    }
    res_report(run, "nvnSampler", "builder value sets", &ref, &got);
    for (int host = 0; host < 2; host++)
        for (unsigned i = 0; i < 3; i++) RES_CALL(c, finalize, host, sampler[host][i]);
}

/* The program: Initialize, the debug label, sample shading and Finalize (decision 9). SetShaders and the other two
   shader setters need shader code and are out of scope. */
static void case_program(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* initialize = nvn_st_res_proc(&cache, "nvnProgramInitialize");
    NvnStProc* finalize = nvn_st_res_proc(&cache, "nvnProgramFinalize");
    NvnStProc* label = nvn_st_res_proc(&cache, "nvnProgramSetDebugLabel");
    NvnStProc* shading = nvn_st_res_proc(&cache, "nvnProgramSetSampleShading");
    NvnStProc* get_depth = nvn_st_res_proc(&cache, "nvnDeviceGetDepthMode");
    if (!initialize || !finalize || !label || !shading || !get_depth) {
        run->skipped++;
        return;
    }
    /* The host keeps the device record lazy: this getter makes it, as the game's own device calls did for the library.
       The program functions of the host need the record and do not change the game device. */
    nvn_st_call(c, &get_depth->host, &(NvnStCall){.x = {nvn_st_device()}});
    uint64_t program[2];
    NvnStRec ref = {0}, got = {0};
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        program[host] = res_obj(run);
        if (!program[host]) {
            run->skipped++;
            return;
        }
        memset(GP(program[host]), 0, RES_OBJ_BYTES);
        res_add(rec, "initialize", RES_CALL(c, initialize, host, program[host], nvn_st_device()));
        RES_CALL(c, label, host, program[host], 0);
        NvnStCall call = {.x = {program[host], RES_SAMPLE_SHADING}};
        nvn_st_call(c, host ? &shading->host : &shading->lib, &call);
        RES_CALL(c, finalize, host, program[host]);
    }
    res_report(run, "nvnProgram", "initialize, label, sample shading, finalize", &ref, &got);
}

/* The device queries and the Set/Get pairs that the script restores (decision 8). Only functions without a lasting
   effect on the game device are here. */
static void case_device(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* get_depth = nvn_st_res_proc(&cache, "nvnDeviceGetDepthMode");
    NvnStProc* set_depth = nvn_st_res_proc(&cache, "nvnDeviceSetDepthMode");
    NvnStProc* get_origin = nvn_st_res_proc(&cache, "nvnDeviceGetWindowOriginMode");
    NvnStProc* set_origin = nvn_st_res_proc(&cache, "nvnDeviceSetWindowOriginMode");
    NvnStProc* debugger = nvn_st_res_proc(&cache, "nvnDeviceIsExternalDebuggerAttached");
    NvnStProc* domain = nvn_st_res_proc(&cache, "nvnDeviceGenerateDebugDomainId");
    NvnStProc* deferred = nvn_st_res_proc(&cache, "nvnDeviceApplyDeferredFinalizes");
    NvnStProc* now = nvn_st_res_proc(&cache, "nvnDeviceGetCurrentTimestampInNanoseconds");
    NvnStProc* timestamp = nvn_st_res_proc(&cache, "nvnDeviceGetTimestampInNanoseconds");
    if (!get_depth || !set_depth || !get_origin || !set_origin || !debugger || !domain || !deferred || !now ||
        !timestamp) {
        run->skipped++;
        return;
    }
    uint64_t counter = res_alloc(run, 16, 16);
    if (!counter) {
        run->skipped++;
        return;
    }
    gw64(counter + 8, RES_TIMESTAMP_TICKS);
    NvnStRec ref = {0}, got = {0};
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        uint64_t first_depth = RES_CALL(c, get_depth, host, nvn_st_device());
        RES_CALL(c, set_depth, host, nvn_st_device(), 1);
        res_add(rec, "depthModeOne", RES_CALL(c, get_depth, host, nvn_st_device()));
        RES_CALL(c, set_depth, host, nvn_st_device(), 0);
        res_add(rec, "depthModeZero", RES_CALL(c, get_depth, host, nvn_st_device()));
        RES_CALL(c, set_depth, host, nvn_st_device(), first_depth); /* restore the value of this side */
        uint64_t first_origin = RES_CALL(c, get_origin, host, nvn_st_device());
        RES_CALL(c, set_origin, host, nvn_st_device(), 1);
        res_add(rec, "originOne", RES_CALL(c, get_origin, host, nvn_st_device()));
        RES_CALL(c, set_origin, host, nvn_st_device(), 0);
        res_add(rec, "originZero", RES_CALL(c, get_origin, host, nvn_st_device()));
        RES_CALL(c, set_origin, host, nvn_st_device(), first_origin);
        res_add(rec, "debugger", RES_CALL(c, debugger, host, nvn_st_device()));
        (void)RES_CALL(c, domain, host, nvn_st_device());
        res_add_nonzero(rec, "domainId", RES_CALL(c, domain, host, nvn_st_device()));
        res_add(rec, "deferred", RES_CALL(c, deferred, host, nvn_st_device()));
        uint64_t first_time = RES_CALL(c, now, host, nvn_st_device());
        uint64_t second_time = RES_CALL(c, now, host, nvn_st_device());
        res_add_nonzero(rec, "timestampOne", first_time);
        res_add_nonzero(rec, "timestampTwo", second_time);
        res_add(rec, "timestampMonotonic", second_time >= first_time);
        res_add_nonzero(rec, "timestampNs", RES_CALL(c, timestamp, host, nvn_st_device(), counter));
    }
    res_report(run, "nvnDevice", "queries, restored modes, timestamps", &ref, &got);
}

/* The builder setters that take a pool, a texture or an address (decision 10). The object values and the values of
   4-u3-1 stay in nvn_selftest_obj.c; these cases add the storage and packaged data setters only. */
static void case_builder_storage(CPU* c, NvnStRun* run) {
    NvnStResCache cache = {0};
    NvnStProc* texture_defaults = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDefaults");
    NvnStProc* texture_device = nvn_st_res_proc(&cache, "nvnTextureBuilderSetDevice");
    NvnStProc* texture_storage = nvn_st_res_proc(&cache, "nvnTextureBuilderSetStorage");
    NvnStProc* texture_pool = nvn_st_res_proc(&cache, "nvnTextureBuilderGetMemoryPool");
    NvnStProc* texture_offset = nvn_st_res_proc(&cache, "nvnTextureBuilderGetMemoryOffset");
    NvnStProc* texture_alignment = nvn_st_res_proc(&cache, "nvnTextureBuilderGetStorageAlignment");
    NvnStProc* texture_size = nvn_st_res_proc(&cache, "nvnTextureBuilderGetStorageSize");
    NvnStProc* packaged_set = nvn_st_res_proc(&cache, "nvnTextureBuilderSetPackagedTextureData");
    NvnStProc* packaged_get = nvn_st_res_proc(&cache, "nvnTextureBuilderGetPackagedTextureData");
    NvnStProc* buffer_defaults = nvn_st_res_proc(&cache, "nvnBufferBuilderSetDefaults");
    NvnStProc* buffer_device = nvn_st_res_proc(&cache, "nvnBufferBuilderSetDevice");
    NvnStProc* buffer_storage = nvn_st_res_proc(&cache, "nvnBufferBuilderSetStorage");
    NvnStProc* buffer_get_size = nvn_st_res_proc(&cache, "nvnBufferBuilderGetSize");
    NvnStProc* buffer_get_device = nvn_st_res_proc(&cache, "nvnBufferBuilderGetDevice");
    NvnStProc* buffer_pool = nvn_st_res_proc(&cache, "nvnBufferBuilderGetMemoryPool");
    NvnStProc* buffer_offset = nvn_st_res_proc(&cache, "nvnBufferBuilderGetMemoryOffset");
    NvnStProc* event_defaults = nvn_st_res_proc(&cache, "nvnEventBuilderSetDefaults");
    NvnStProc* event_storage = nvn_st_res_proc(&cache, "nvnEventBuilderSetStorage");
    NvnStProc* event_pool = nvn_st_res_proc(&cache, "nvnEventBuilderGetMemoryPool");
    NvnStProc* event_offset = nvn_st_res_proc(&cache, "nvnEventBuilderGetMemoryOffset");
    NvnStProc* event_get_storage = nvn_st_res_proc(&cache, "nvnEventBuilderGetStorage");
    if (!texture_defaults || !texture_device || !texture_storage || !texture_pool || !texture_offset ||
        !texture_alignment || !texture_size || !packaged_set || !packaged_get || !buffer_defaults || !buffer_device ||
        !buffer_storage || !buffer_get_size || !buffer_get_device || !buffer_pool || !buffer_offset ||
        !event_defaults || !event_storage || !event_pool || !event_offset || !event_get_storage) {
        run->skipped++;
        return;
    }
    NvnStResPool memory[2];
    if (!nvn_st_res_pool_create(c, run, &cache, 0, &memory[0], RES_POOL_CPU_CACHED, RES_POOL_BYTES) ||
        !nvn_st_res_pool_create(c, run, &cache, 1, &memory[1], RES_POOL_CPU_CACHED, RES_POOL_BYTES)) {
        run->skipped++;
        return;
    }
    uint64_t packed = res_alloc(run, 0x40, 16), out[2];
    NvnStRec ref = {0}, got = {0};
    for (int host = 0; host < 2; host++) out[host] = res_alloc(run, RES_OUT_BYTES, 16);
    if (!packed || !out[0] || !out[1]) {
        run->skipped++;
        return;
    }
    for (int host = 0; host < 2; host++) {
        NvnStRec* rec = host ? &got : &ref;
        uint64_t texture = res_obj(run), buffer = res_obj(run), event = res_obj(run);
        if (!texture || !buffer || !event) {
            run->skipped++;
            return;
        }
        memset(GP(texture), 0, RES_OBJ_BYTES);
        RES_CALL(c, texture_defaults, host, texture);
        RES_CALL(c, texture_device, host, texture, nvn_st_device());
        RES_CALL(c, texture_storage, host, texture, memory[host].pool, 0x1000);
        res_add_same(rec, "texturePool", RES_CALL(c, texture_pool, host, texture), memory[host].pool);
        res_add(rec, "textureOffset", RES_CALL(c, texture_offset, host, texture));
        res_add(rec, "textureAlignment", RES_CALL(c, texture_alignment, host, texture));
        res_add(rec, "textureStorageSize", RES_CALL(c, texture_size, host, texture));
        RES_CALL(c, packaged_set, host, texture, packed);
        res_add(rec, "packagedData", RES_CALL(c, packaged_get, host, texture));
        memset(GP(buffer), 0, RES_OBJ_BYTES);
        RES_CALL(c, buffer_defaults, host, buffer);
        RES_CALL(c, buffer_device, host, buffer, nvn_st_device());
        RES_CALL(c, buffer_storage, host, buffer, memory[host].pool, 0x2000, 0x1000);
        res_add(rec, "bufferSize", RES_CALL(c, buffer_get_size, host, buffer));
        res_add(rec, "bufferDevice", RES_CALL(c, buffer_get_device, host, buffer));
        res_add_same(rec, "bufferPool", RES_CALL(c, buffer_pool, host, buffer), memory[host].pool);
        res_add(rec, "bufferOffset", RES_CALL(c, buffer_offset, host, buffer));
        memset(GP(event), 0, RES_OBJ_BYTES);
        RES_CALL(c, event_defaults, host, event);
        RES_CALL(c, event_storage, host, event, memory[host].pool, 0x1800);
        res_add_same(rec, "eventPool", RES_CALL(c, event_pool, host, event), memory[host].pool);
        res_add(rec, "eventOffset", RES_CALL(c, event_offset, host, event));
        memset(GP(out[host]), RES_OUT_FILL, RES_OUT_BYTES);
        uint64_t storage_pool = RES_CALL(c, event_get_storage, host, event, out[host] + RES_OUT_GUARD);
        res_add_same(rec, "eventStoragePool", storage_pool, memory[host].pool);
        nvn_st_add_blob(rec, "eventStorageOffset", GP(out[host]), NULL, RES_OUT_BYTES);
    }
    res_report(run, "nvnBuilderStorage", "texture, buffer and event pools", &ref, &got);
    nvn_st_res_pool_destroy(c, &cache, 0, &memory[0]);
    nvn_st_res_pool_destroy(c, &cache, 1, &memory[1]);
}

void nvn_selftest_resource_cases(CPU* c, NvnStRun* run) {
    case_memory_pool(c, run);
    case_memory_pool_map_virtual(c, run);
    case_buffer(c, run);
    case_texture(c, run);
    case_descriptor_pools(c, run);
    case_sampler(c, run);
    case_program(c, run);
    case_device(c, run);
    case_builder_storage(c, run);
}
