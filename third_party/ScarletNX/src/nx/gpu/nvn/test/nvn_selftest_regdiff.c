/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/test/nvn_selftest_regdiff.h"
#include "nx/gpu/gpu.h"
#include <stddef.h>
#include <stdio.h>

#define ST_REG_NAME_BYTES 12u

/* The engines of the command buffer cases: the 3D registers end at MAXWELL3D_NUM_REGS, because the macro calls above
   them are not registers. */
const NvnStEngine nvn_st_engines[NVN_ST_ENGINE_COUNT] = {
    {"r3d", offsetof(GpuChannel, r3d), MAXWELL3D_NUM_REGS},
    {"r2d", offsetof(GpuChannel, r2d), FERMI2D_METHODS},
    {"rdma", offsetof(GpuChannel, rdma), MAXWELL_DMA_METHODS},
    {"rcomp", offsetof(GpuChannel, rcomp), KEPLER_COMPUTE_METHODS},
};

static char st_names_r3d[MAXWELL3D_NUM_REGS][ST_REG_NAME_BYTES];
static char st_names_r2d[FERMI2D_METHODS][ST_REG_NAME_BYTES];
static char st_names_rdma[MAXWELL_DMA_METHODS][ST_REG_NAME_BYTES];
static char st_names_rcomp[KEPLER_COMPUTE_METHODS][ST_REG_NAME_BYTES];

static char* const st_name_bases[NVN_ST_ENGINE_COUNT] = {
    &st_names_r3d[0][0], &st_names_r2d[0][0], &st_names_rdma[0][0], &st_names_rcomp[0][0],
};
static int st_names_ready;

static void st_build_names(void) {
    if (st_names_ready) return;
    for (unsigned e = 0; e < NVN_ST_ENGINE_COUNT; e++) {
        const NvnStEngine* const engine = &nvn_st_engines[e];
        char* const base = st_name_bases[e];
        for (unsigned r = 0; r < engine->count; r++)
            snprintf(base + (size_t)r * ST_REG_NAME_BYTES, ST_REG_NAME_BYTES, "%s.0x%03x", engine->name, r);
    }
    st_names_ready = 1;
}

unsigned nvn_st_regdiff(const u32* base, const u32* a, const u32* b, unsigned n, u32* out_regs, unsigned cap,
                        int* overflow) {
    unsigned count = 0;
    int more = 0;
    if (overflow) *overflow = 0;
    if (!base || !a || !b || (n && !out_regs)) {
        if (overflow) *overflow = 1;
        return 0;
    }
    for (unsigned r = 0; r < n; r++) {
        if (a[r] == base[r] && b[r] == base[r]) continue;
        if (count < cap)
            out_regs[count++] = r;
        else
            more = 1;
    }
    if (more && overflow) *overflow = 1;
    return count;
}

const char* nvn_st_engine_reg_name(unsigned engine, unsigned reg) {
    if (engine >= NVN_ST_ENGINE_COUNT || reg >= nvn_st_engines[engine].count) return "reg.out_of_range";
    st_build_names();
    return st_name_bases[engine] + (size_t)reg * ST_REG_NAME_BYTES;
}

const char* nvn_st_reg_name(unsigned reg) { return nvn_st_engine_reg_name(NVN_ST_ENGINE_R3D, reg); }
