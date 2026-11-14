/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/test/nvn_selftest_dec.h"

/* The record ops of the library's interpreter (d839d20). */
#define REC_OP_RANGES 0x03u
#define REC_OP_JUMP 0x04u
#define REC_OP_TEXTURE_POOL 0x13u
#define REC_OP_SAMPLER_POOL 0x14u
#define REC_OP_END_MAX 0x02u
#define REC_HEADER_BYTES 8u
#define REC_JUMP_BYTES 0x10u
#define REC_ENTRY_BYTES 0x18u
#define REC_ENTRY_WORD_COUNT_OFFSET 16u
#define REC_JUMP_NEXT_OFFSET 8u
#define REC_POOL_BYTES 0x10u
#define REC_POOL_OBJECT_OFFSET 8u
#define REC_HANDLE_ADDRESS_MASK 0xFFFFFFFFFFFFull
#define REC_HANDLE_TAG_SHIFT 48u
#define REC_HANDLE_ENTRY_BIT 1ull

typedef struct {
    const void* (*read)(void* ctx, uint64_t addr, size_t size);
    void* ctx;
    NvnStRange* out;
    unsigned cap, n;
    NvnStDecodeFault* fault;
} Decoder;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static int read_at(Decoder* d, uint64_t addr, size_t size, const uint8_t** out) {
    const void* p = d->read(d->ctx, addr, size);
    if (!p) {
        d->fault->op = 0;
        d->fault->addr = addr;
        return 0;
    }
    *out = (const uint8_t*)p;
    return 1;
}

/* One entry of an entry table, or one record entry: {u64 gpu_start, u64 cpu_start, u32 word_count, u8 type}. The
   run log of 2026-11-11 shows +8 equal to the command memory storage for the first range, so it is the start. */
static int add_entry(Decoder* d, const uint8_t* entry, uint64_t addr) {
    const uint64_t cpu_start = rd64(entry + 8);
    const uint32_t words = rd32(entry + REC_ENTRY_WORD_COUNT_OFFSET);
    if (!words || cpu_start > UINT64_MAX - (uint64_t)words * 4u) {
        d->fault->op = 0;
        d->fault->addr = addr;
        return NVN_ST_DEC_BAD_ENTRY;
    }
    if (d->n >= d->cap) return NVN_ST_DEC_TOO_MANY_RANGES;
    d->out[d->n].cpu_start = cpu_start;
    d->out[d->n].words = words;
    d->out[d->n].pool_op = 0;
    d->out[d->n].pool = 0;
    d->n++;
    return NVN_ST_DEC_OK;
}

/* The handle names tag entries of 0x18 bytes: a 16 bit count of ranges, not a record list. */
static int decode_entries(Decoder* d, uint64_t base, unsigned tag) {
    if (tag > d->cap) {
        d->fault->op = 0;
        d->fault->addr = base;
        return NVN_ST_DEC_TOO_MANY_RANGES;
    }
    const uint8_t* entries;
    if (!read_at(d, base, (size_t)tag * REC_ENTRY_BYTES, &entries)) return NVN_ST_DEC_READ_FAILED;
    for (unsigned i = 0; i < tag; i++) {
        const uint64_t addr = base + (uint64_t)i * REC_ENTRY_BYTES;
        const int status = add_entry(d, entries + (size_t)i * REC_ENTRY_BYTES, addr);
        if (status != NVN_ST_DEC_OK) {
            if (status == NVN_ST_DEC_BAD_ENTRY) d->fault->op = REC_OP_RANGES;
            return status;
        }
    }
    return NVN_ST_DEC_OK;
}

/* The handle names a record list: 03 adds a range entry table, 04 jumps, 0, 1 and 2 end it. */
static int decode_records(Decoder* d, uint64_t addr) {
    unsigned jumps = 0;
    for (;;) {
        const uint8_t* header;
        if (!read_at(d, addr, REC_HEADER_BYTES, &header)) return NVN_ST_DEC_READ_FAILED;
        const uint32_t op = header[0];
        if (op <= REC_OP_END_MAX) return NVN_ST_DEC_OK;
        if (op == REC_OP_JUMP) {
            if (jumps >= NVN_ST_DEC_MAX_JUMPS) {
                d->fault->op = op;
                d->fault->addr = addr;
                return NVN_ST_DEC_TOO_MANY_JUMPS;
            }
            jumps++;
            const uint8_t* record;
            if (!read_at(d, addr, REC_JUMP_BYTES, &record)) return NVN_ST_DEC_READ_FAILED;
            addr = rd64(record + REC_JUMP_NEXT_OFFSET);
            continue;
        }
        if (op == REC_OP_RANGES) {
            const uint32_t count = rd32(header + 4);
            if (count > d->cap - d->n) {
                d->fault->op = op;
                d->fault->addr = addr;
                return NVN_ST_DEC_TOO_MANY_RANGES;
            }
            const uint8_t* entries;
            if (!read_at(d, addr + REC_HEADER_BYTES, (size_t)count * REC_ENTRY_BYTES, &entries))
                return NVN_ST_DEC_READ_FAILED;
            for (uint32_t i = 0; i < count; i++) {
                const uint64_t entry_addr = addr + REC_HEADER_BYTES + (uint64_t)i * REC_ENTRY_BYTES;
                const int status = add_entry(d, entries + (size_t)i * REC_ENTRY_BYTES, entry_addr);
                if (status != NVN_ST_DEC_OK) {
                    if (status == NVN_ST_DEC_BAD_ENTRY) d->fault->op = op;
                    return status;
                }
            }
            addr += REC_HEADER_BYTES + (uint64_t)count * REC_ENTRY_BYTES;
            continue;
        }
        if (op == REC_OP_TEXTURE_POOL || op == REC_OP_SAMPLER_POOL) {
            if (d->n >= d->cap) {
                d->fault->op = op;
                d->fault->addr = addr;
                return NVN_ST_DEC_TOO_MANY_RANGES;
            }
            const uint8_t* record;
            if (!read_at(d, addr, REC_POOL_BYTES, &record)) return NVN_ST_DEC_READ_FAILED;
            NvnStRange* r = &d->out[d->n];
            r->cpu_start = 0;
            r->words = 0;
            r->pool_op = op;
            r->pool = rd64(record + REC_POOL_OBJECT_OFFSET);
            d->n++;
            addr += REC_POOL_BYTES;
            continue;
        }
        d->fault->op = op;
        d->fault->addr = addr;
        return NVN_ST_DEC_REFUSED_OP;
    }
}

int nvn_st_decode_recording(uint64_t handle, const void* (*read)(void* ctx, uint64_t addr, size_t size), void* ctx,
                            NvnStRange* out, unsigned cap, unsigned* n, NvnStDecodeFault* fault) {
    NvnStDecodeFault local;
    Decoder d = {read, ctx, out, cap, 0, fault ? fault : &local};
    d.fault->op = 0;
    d.fault->addr = 0;
    if (n) *n = 0;
    if (!read || !n || (!out && cap)) return NVN_ST_DEC_READ_FAILED;
    const uint64_t base = (handle & REC_HANDLE_ADDRESS_MASK) & ~REC_HANDLE_ENTRY_BIT;
    const unsigned tag = (unsigned)(handle >> REC_HANDLE_TAG_SHIFT);
    int status;
    if ((handle & REC_HANDLE_ENTRY_BIT) && tag)
        status = decode_entries(&d, base, tag);
    else
        status = decode_records(&d, base);
    if (status == NVN_ST_DEC_OK) d.fault->addr = 0;
    *n = d.n;
    return status;
}

/* The words of one expanded pool record: the library interpreter does this at submission (d84c120 for op 0x13, d84c3b0
   for op 0x14, the constants at de141a0). The second run is the same header on subchannel 1. */
#define POOL_OP_TEXTURE_BASE 0x2003055Du /* mode 1, count 3, method 0x55D */
#define POOL_OP_SAMPLER_BASE 0x20030557u /* mode 1, count 3, method 0x557 */
#define POOL_OP_SUBCHANNEL1 0x2000u      /* bit 13 of the header: subchannel 1 */
#define POOL_TEXTURE_SELECT 0x80400E20u  /* mode 4, value 0x40, method 0xE20 */
#define POOL_TEXTURE_OFFSET 0x200208E4u  /* mode 1, count 2, method 0x8E4 */
#define POOL_TEXTURE_WORDS 12u
#define POOL_SAMPLER_WORDS 8u

uint32_t nvn_st_pool_words(uint32_t op, uint64_t base, uint32_t size, uint32_t* out, uint32_t cap) {
    if (!out) return 0;
    const uint32_t header = op == REC_OP_TEXTURE_POOL ? POOL_OP_TEXTURE_BASE
                            : op == REC_OP_SAMPLER_POOL ? POOL_OP_SAMPLER_BASE
                                                        : 0u;
    if (!header) return 0;
    const uint32_t count = op == REC_OP_TEXTURE_POOL ? POOL_TEXTURE_WORDS : POOL_SAMPLER_WORDS;
    if (cap < count) return 0;
    const uint32_t hi = (uint32_t)(base >> 32);
    const uint32_t lo = (uint32_t)base;
    const uint32_t limit = size - 1u; /* a 32-bit wrap: size 0 gives 0xFFFFFFFF */
    out[0] = header;
    out[1] = hi;
    out[2] = lo;
    out[3] = limit;
    out[4] = header | POOL_OP_SUBCHANNEL1;
    out[5] = hi;
    out[6] = lo;
    out[7] = limit;
    if (op == REC_OP_TEXTURE_POOL) {
        out[8] = POOL_TEXTURE_SELECT;
        out[9] = POOL_TEXTURE_OFFSET;
        out[10] = lo;
        out[11] = hi;
    }
    return count;
}
