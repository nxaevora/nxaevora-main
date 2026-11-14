/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_REGDIFF_H
#define SNX_NVN_SELFTEST_REGDIFF_H

/* The register diff of the command buffer selftest (nvn-unobserved.md, "The selftest"), a pure part: it names the
   registers that the two sides left different from the channel's base state, so the snapshot adds one field for each
   of them. It has no game state, so host tests can run it. */

#include "common/cpu/aarch64/cpu.h"

/* The register arrays of a channel that the command buffer cases compare, in the order of the ignore lists. */
#define NVN_ST_ENGINE_R3D 0u
#define NVN_ST_ENGINE_R2D 1u
#define NVN_ST_ENGINE_RDMA 2u
#define NVN_ST_ENGINE_RCOMP 3u
#define NVN_ST_ENGINE_COUNT 4u

/* One engine's register array: its name, the byte offset of the array in GpuChannel, and the number of registers. */
typedef struct {
    const char* name;
    unsigned offset;
    unsigned count;
} NvnStEngine;

extern const NvnStEngine nvn_st_engines[NVN_ST_ENGINE_COUNT];

/* Writes the indexes, in register order, of the registers r below n where a[r] or b[r] differs from base[r]. Returns
   the number written, which is at most cap; *overflow is 1 when more registers differ than fit. It sets *overflow to
   0 at the start, and to 1 when a pointer is NULL. */
unsigned nvn_st_regdiff(const u32* base, const u32* a, const u32* b, unsigned n, u32* out_regs, unsigned cap,
                        int* overflow);

/* The field name "<engine>.0x<reg>" of one register, for example "r3d.0x47f" or "r2d.0x080". An engine or a register
   out of range gives "reg.out_of_range". The tables are built once on first use. The selftest runs on one guest thread,
   so no lock is needed; a record keeps the pointer, so the tables outlive every record. */
const char* nvn_st_engine_reg_name(unsigned engine, unsigned reg);

/* The r3d name, for the callers that compare the 3D engine only. */
const char* nvn_st_reg_name(unsigned reg);

#endif
