/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_NVN_ENUM_TRACE_H
#define SNX_NVN_ENUM_TRACE_H
#include <stdint.h>

/* The enum and flag arguments of the observed NVN functions, and the set of values that the trace logged
   (nvn-unobserved.md, "Rules", item 3). This part has no guest memory, no log and no lock,
   so a host test can link it alone. */

#define NVN_ENUM_SEEN_MAX 64
#define NVN_ENUM_REGS_BITS 8 /* regs is a uint8_t: bit N is the argument in wN */

typedef struct {
    const char* name; /* the NVN function name */
    uint8_t regs;     /* bit N: the argument in wN is an enum or a flag mask */
} NvnEnumArgs;

typedef struct {
    uint64_t keys[NVN_ENUM_SEEN_MAX]; /* reg << 32 | value */
    uint32_t n;
} NvnEnumSeen;

/* The row of name, or NULL when the function has no enum or flag argument that the trace reads. */
const NvnEnumArgs* nvn_enum_args(const char* name);
/* 1: the key is new and added; 0: the key is known; -1: the set is full and the key is new, so it is not added. */
int nvn_enum_seen_add(NvnEnumSeen* s, uint32_t reg, uint32_t value);

#endif
