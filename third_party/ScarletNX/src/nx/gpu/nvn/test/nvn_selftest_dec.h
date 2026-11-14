/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_DEC_H
#define SNX_NVN_SELFTEST_DEC_H

/* The decoder of a library command buffer recording (nvn-unobserved.md, "The selftest"). It is a pure part: it reads
   the control memory behind an nvnCommandBufferEndRecording handle and gives the ranges of push buffer words and the
   pool records. It does not read the words and has no game state, so host tests can run it. */

#include <stddef.h>
#include <stdint.h>

/* A cycle of jump records is a broken recording: the guard stops after this many jumps. */
#define NVN_ST_DEC_MAX_JUMPS 64u

/* The result of nvn_st_decode_recording. */
enum {
    NVN_ST_DEC_OK = 0,
    NVN_ST_DEC_REFUSED_OP,      /* a record op that the decoder does not read (0x05 to 0x12, 0x15 and 0x16) */
    NVN_ST_DEC_TOO_MANY_RANGES, /* more ranges than the caller's cap */
    NVN_ST_DEC_TOO_MANY_JUMPS,  /* more jumps than NVN_ST_DEC_MAX_JUMPS: a cycle */
    NVN_ST_DEC_BAD_ENTRY,       /* word_count is 0, or the range wraps the address space */
    NVN_ST_DEC_READ_FAILED,     /* the read callback gave NULL */
};

/* The push buffer words of one record entry live at [cpu_start, cpu_start + 4 * words). A pool record has pool_op set
   (0x13 texture pool, 0x14 sampler pool), its pool object in pool, and no words of its own. */
typedef struct {
    uint64_t cpu_start;
    uint32_t words;
    uint32_t pool_op;
    uint64_t pool;
} NvnStRange;

/* Why the decoder stopped: the record op and its address. Zero for a fault that has no record op. */
typedef struct {
    uint32_t op;
    uint64_t addr;
} NvnStDecodeFault;

/* Decodes the recording behind handle into out[0..*n). read returns the bytes at addr, or NULL when they are not
   readable. The decoder follows the two handle forms, the record ops 03 and 04, the pool records 0x13 and 0x14, and
   the end ops 0, 1 and 2. It reads no more than cap items. Returns one of the NVN_ST_DEC_* status values. */
int nvn_st_decode_recording(uint64_t handle, const void* (*read)(void* ctx, uint64_t addr, size_t size), void* ctx,
                            NvnStRange* out, unsigned cap, unsigned* n, NvnStDecodeFault* fault);

/* Writes the push buffer words that the library interpreter makes from one pool record, at the submission of the
   recording (d84c120 for op 0x13, d84c3b0 for op 0x14). The words use the constants at de141a0. Returns the count (12
   for 0x13, 8 for 0x14), or 0 for an unknown op, a NULL out or a cap that is too small. */
uint32_t nvn_st_pool_words(uint32_t op, uint64_t base, uint32_t size, uint32_t* out, uint32_t cap);

#endif
