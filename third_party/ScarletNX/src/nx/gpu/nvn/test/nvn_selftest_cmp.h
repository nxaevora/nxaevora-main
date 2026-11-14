/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_CMP_H
#define SNX_NVN_SELFTEST_CMP_H

/* The comparer of the NVN selftest (nvn-unobserved.md, "The selftest"). It is a pure part: it compares two recorded
   results of the same scripted call, one from the library and one from the host handler. It has no game state, so host
   tests can run it. */

#include <stddef.h>
#include <stdint.h>

/* The kinds of comparison. A difference in one of these four is a fail. */
enum {
    NVN_ST_R = 1u << 0, /* return values */
    NVN_ST_M = 1u << 1, /* memory writes */
    NVN_ST_S = 1u << 2, /* state at the point of work */
    NVN_ST_Y = 1u << 3, /* order of the sync operations */
};
#define NVN_ST_KINDS 4

#define NVN_ST_MAX_FIELDS 64
#define NVN_ST_MAX_BLOBS 16
#define NVN_ST_MAX_SYNC 64
#define NVN_ST_TEXT 192

typedef struct {
    const char* name; /* must outlive the record */
    uint64_t value;
} NvnStField;

/* Bytes that a call wrote. Bytes whose mask byte is 0 are on the ignore list and are not compared. A NULL mask compares
   all bytes. The caller owns data and mask. */
typedef struct {
    const char* name;
    const uint8_t* data;
    const uint8_t* mask;
    size_t size;
} NvnStBlob;

typedef struct {
    const char* op; /* fence, semaphore, report, barrier, wait, signal */
    uint64_t arg;
} NvnStSync;

typedef struct {
    NvnStField ret[NVN_ST_MAX_FIELDS];
    NvnStBlob blobs[NVN_ST_MAX_BLOBS];
    NvnStField state[NVN_ST_MAX_FIELDS];
    NvnStSync sync[NVN_ST_MAX_SYNC];
    unsigned n_ret, n_blobs, n_state, n_sync;
    unsigned overflow; /* mask of the kinds that lost an entry: such a record never compares equal */
} NvnStRec;

/* Each add function returns 1, or 0 when the table is full (the kind is then marked in rec->overflow). */
int nvn_st_add_ret(NvnStRec* rec, const char* name, uint64_t value);
int nvn_st_add_blob(NvnStRec* rec, const char* name, const void* data, const void* mask, size_t size);
int nvn_st_add_state(NvnStRec* rec, const char* name, uint64_t value);
int nvn_st_add_sync(NvnStRec* rec, const char* op, uint64_t arg);

typedef struct {
    unsigned failed;                     /* mask of NVN_ST_* kinds that differ */
    char first[NVN_ST_KINDS][NVN_ST_TEXT]; /* the first difference of each kind, with both values (index = bit number) */
} NvnStDiff;

/* Compares library (ref) and host (got). Returns the failed mask, also in diff->failed. */
unsigned nvn_st_compare(const NvnStRec* ref, const NvnStRec* got, NvnStDiff* diff);

/* The report line "<name> <args> R:ok M:ok S:ok Y:ok". A kind that the record does not use shows "-". */
int nvn_st_format_line(char* out, size_t cap, const char* name, const char* args, const NvnStRec* ref,
                       const NvnStDiff* diff);

#endif
