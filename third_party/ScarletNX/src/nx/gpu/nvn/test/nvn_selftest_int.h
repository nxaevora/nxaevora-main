/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_INT_H
#define SNX_NVN_SELFTEST_INT_H

/* The internal parts of the NVN selftest (nvn-unobserved.md, "The selftest"): one side of a comparison, the call of
   a library function or of its host handler, the report of one case, and the object cases that nvn_selftest_obj.c
   adds. The public entry stays in nvn_selftest.h. */

#include "nx/gpu/nvn/test/nvn_selftest.h"
#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
#include "nx/gpu/nvn/test/nvn_selftest_mem.h"

typedef struct {
    GuestFn fn;
    u64 entry;
} NvnStCallee;

typedef struct {
    NvnStCallee lib, host;
    const char* name;
} NvnStProc;

typedef struct {
    unsigned cases, failed, expected, skipped;
    NvnStArena arena;
} NvnStRun;

/* The device word that SetViewports reads (d84acb0): the window origin mode, for the command buffer cases. */
#define NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET 0x19d8u

/* The memory pool flags of the resource and command buffer cases (analysis/enum-run2.txt). */
#define NVN_ST_RES_POOL_CPU_CACHED 0x24u /* CPU cached, GPU cached */
#define NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED 0x22u

/* The memory pool of one side of a resource or command buffer script, with its builder object and storage block. */
typedef struct {
    uint64_t builder, pool, storage;
} NvnStResPool;

#define NVN_ST_RES_CACHE_MAX 64u

typedef struct {
    const char* name;
    NvnStProc proc;
    unsigned resolved;
} NvnStResEntry;

/* The functions of one script, resolved once. A missing name skips the script with one line, as in 4-u3-1. */
typedef struct {
    NvnStResEntry e[NVN_ST_RES_CACHE_MAX];
    unsigned n;
} NvnStResCache;

NvnStProc* nvn_st_res_proc(NvnStResCache* cache, const char* name);
int nvn_st_res_pool_create(CPU* c, NvnStRun* run, NvnStResCache* cache, int host, NvnStResPool* mp, uint32_t flags,
                           uint64_t bytes);
void nvn_st_res_pool_destroy(CPU* c, NvnStResCache* cache, int host, const NvnStResPool* mp);

/* One call. x0..x5 are the integer arguments and s0..s3 the float arguments (their bit patterns). s0_out, when not
   NULL, receives the s0 bits of the result before the CPU context is restored. */
typedef struct {
    uint64_t x[6];
    uint32_t s[4];
    unsigned n_s;
    uint32_t* s0_out;
} NvnStCall;

int nvn_st_proc_find(NvnStProc* p, const char* name);
uint64_t nvn_st_call(CPU* c, const NvnStCallee* f, const NvnStCall* a);
void nvn_st_report(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d);
/* A case of a handler that is a logged no-op by design: a difference is expected, and no difference is a fail. */
void nvn_st_report_noop(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d);
/* The device that the game made, for the builders that need one. */
uint64_t nvn_st_device(void);

/* The builder and state object cases, called from nvn_selftest_run after the two first cases. */
void nvn_selftest_object_cases(CPU* c, NvnStRun* run);

/* The resource object cases of nvn_selftest_res.c, called from nvn_selftest_run after the builder and state cases. */
void nvn_selftest_resource_cases(CPU* c, NvnStRun* run);

/* The command buffer cases of nvn_selftest_cmd.c, called from nvn_selftest_run after the resource cases. */
void nvn_selftest_command_cases(CPU* c, NvnStRun* run);

/* The command buffer state (R) cases of nvn_selftest_cbstate.c, called from nvn_selftest_run before the command
   cases. */
void nvn_selftest_command_state_cases(CPU* c, NvnStRun* run);

/* Builds one sampler object from the sampler builder values of nvn_selftest_obj.c. `variant` 2 gives a filter that
   makes the sampler different from variant 0. 1 when every function was found. */
int nvn_st_sampler_build(CPU* c, uint64_t builder, uint64_t sampler, uint64_t data, int host, unsigned variant);

/* The state objects that the command buffer Bind cases build, one for each side. `xor_value` changes the first setter
   value (1 for the host's copy of a mutation). 1 when every function was found. */
enum {
    NVN_ST_STATE_BLEND,
    NVN_ST_STATE_COLOR,
    NVN_ST_STATE_CHANNEL_MASK,
    NVN_ST_STATE_DEPTH_STENCIL,
    NVN_ST_STATE_MULTISAMPLE,
    NVN_ST_STATE_POLYGON,
    NVN_ST_STATE_COUNT
};
int nvn_st_state_build(CPU* c, unsigned kind, int host, uint64_t obj, uint32_t xor_value);

/* One 2D texture of the command buffer cases in the side's pool at offset, and one descriptor pool of entries at
   offset. 1 when every function was found and the object was made. */
int nvn_st_texture_build(CPU* c, NvnStResCache* cache, int host, uint64_t builder, uint64_t texture,
                         const NvnStResPool* pool, uint64_t offset, uint32_t width, uint32_t height, uint32_t format);
int nvn_st_descriptor_pool_build(CPU* c, NvnStResCache* cache, int host, uint64_t pool_obj,
                                 const NvnStResPool* memory, uint64_t offset, uint32_t entries, int sampler);

#endif
