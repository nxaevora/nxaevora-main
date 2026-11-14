/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int add_field(NvnStField* tab, unsigned* n, unsigned* overflow, unsigned kind, const char* name, uint64_t v) {
    if (*n >= NVN_ST_MAX_FIELDS) {
        *overflow |= kind;
        return 0;
    }
    tab[*n].name = name;
    tab[*n].value = v;
    (*n)++;
    return 1;
}

int nvn_st_add_ret(NvnStRec* rec, const char* name, uint64_t value) {
    return add_field(rec->ret, &rec->n_ret, &rec->overflow, NVN_ST_R, name, value);
}

int nvn_st_add_state(NvnStRec* rec, const char* name, uint64_t value) {
    return add_field(rec->state, &rec->n_state, &rec->overflow, NVN_ST_S, name, value);
}

int nvn_st_add_blob(NvnStRec* rec, const char* name, const void* data, const void* mask, size_t size) {
    if (rec->n_blobs >= NVN_ST_MAX_BLOBS) {
        rec->overflow |= NVN_ST_M;
        return 0;
    }
    NvnStBlob* b = &rec->blobs[rec->n_blobs++];
    b->name = name;
    b->data = (const uint8_t*)data;
    b->mask = (const uint8_t*)mask;
    b->size = size;
    return 1;
}

int nvn_st_add_sync(NvnStRec* rec, const char* op, uint64_t arg) {
    if (rec->n_sync >= NVN_ST_MAX_SYNC) {
        rec->overflow |= NVN_ST_Y;
        return 0;
    }
    rec->sync[rec->n_sync].op = op;
    rec->sync[rec->n_sync].arg = arg;
    rec->n_sync++;
    return 1;
}

static int kind_index(unsigned kind) {
    int i = 0;
    while (!(kind & 1u)) {
        kind >>= 1;
        i++;
    }
    return i;
}

/* Keeps only the first difference of a kind. */
static void fail(NvnStDiff* d, unsigned kind, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void fail(NvnStDiff* d, unsigned kind, const char* fmt, ...) {
    if (d->failed & kind) return;
    d->failed |= kind;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->first[kind_index(kind)], NVN_ST_TEXT, fmt, ap);
    va_end(ap);
}

static int same_name(const char* a, const char* b) {
    if (a == b) return 1;
    return a && b && strcmp(a, b) == 0;
}

static void compare_fields(const NvnStField* a, unsigned na, const NvnStField* b, unsigned nb, unsigned kind,
                           NvnStDiff* d) {
    if (na != nb) {
        fail(d, kind, "count: library %u, host %u", na, nb);
        return;
    }
    for (unsigned i = 0; i < na; i++) {
        if (!same_name(a[i].name, b[i].name)) {
            fail(d, kind, "entry %u name: library %s, host %s", i, a[i].name ? a[i].name : "?",
                 b[i].name ? b[i].name : "?");
            return;
        }
        if (a[i].value != b[i].value) {
            fail(d, kind, "%s: library 0x%llx, host 0x%llx", a[i].name ? a[i].name : "?",
                 (unsigned long long)a[i].value, (unsigned long long)b[i].value);
            return;
        }
    }
}

static void compare_blobs(const NvnStRec* ref, const NvnStRec* got, NvnStDiff* d) {
    if (ref->n_blobs != got->n_blobs) {
        fail(d, NVN_ST_M, "blob count: library %u, host %u", ref->n_blobs, got->n_blobs);
        return;
    }
    for (unsigned i = 0; i < ref->n_blobs; i++) {
        const NvnStBlob* a = &ref->blobs[i];
        const NvnStBlob* b = &got->blobs[i];
        if (!same_name(a->name, b->name)) {
            fail(d, NVN_ST_M, "blob %u name: library %s, host %s", i, a->name ? a->name : "?", b->name ? b->name : "?");
            return;
        }
        if (a->size != b->size) {
            fail(d, NVN_ST_M, "%s size: library %zu, host %zu", a->name ? a->name : "?", a->size, b->size);
            return;
        }
        for (size_t k = 0; k < a->size; k++) {
            /* the ignore list belongs to the library record: the library's private bytes */
            if (a->mask && !a->mask[k]) continue;
            if (a->data[k] != b->data[k]) {
                fail(d, NVN_ST_M, "%s byte %zu: library 0x%02x, host 0x%02x", a->name ? a->name : "?", k, a->data[k],
                     b->data[k]);
                return;
            }
        }
    }
}

static void compare_sync(const NvnStRec* ref, const NvnStRec* got, NvnStDiff* d) {
    unsigned n = ref->n_sync < got->n_sync ? ref->n_sync : got->n_sync;
    for (unsigned i = 0; i < n; i++) {
        const NvnStSync* a = &ref->sync[i];
        const NvnStSync* b = &got->sync[i];
        if (!same_name(a->op, b->op) || a->arg != b->arg) {
            fail(d, NVN_ST_Y, "op %u: library %s(0x%llx), host %s(0x%llx)", i, a->op ? a->op : "?",
                 (unsigned long long)a->arg, b->op ? b->op : "?", (unsigned long long)b->arg);
            return;
        }
    }
    if (ref->n_sync != got->n_sync) fail(d, NVN_ST_Y, "op count: library %u, host %u", ref->n_sync, got->n_sync);
}

unsigned nvn_st_compare(const NvnStRec* ref, const NvnStRec* got, NvnStDiff* diff) {
    memset(diff, 0, sizeof *diff);
    compare_fields(ref->ret, ref->n_ret, got->ret, got->n_ret, NVN_ST_R, diff);
    compare_blobs(ref, got, diff);
    compare_fields(ref->state, ref->n_state, got->state, got->n_state, NVN_ST_S, diff);
    compare_sync(ref, got, diff);
    /* a record that lost an entry is not a complete record: it cannot pass */
    for (unsigned k = 1; k <= NVN_ST_Y; k <<= 1)
        if ((ref->overflow | got->overflow) & k) fail(diff, k, "record table full");
    return diff->failed;
}

int nvn_st_format_line(char* out, size_t cap, const char* name, const char* args, const NvnStRec* ref,
                       const NvnStDiff* diff) {
    const unsigned used[NVN_ST_KINDS] = {ref->n_ret != 0, ref->n_blobs != 0, ref->n_state != 0, ref->n_sync != 0};
    static const char letter[NVN_ST_KINDS] = {'R', 'M', 'S', 'Y'};
    int n = snprintf(out, cap, "selftest: %s %s", name, args);
    for (int k = 0; k < NVN_ST_KINDS && n > 0 && (size_t)n < cap; k++) {
        const char* r = !used[k] && !(diff->failed & (1u << k)) ? "-" : (diff->failed & (1u << k)) ? "FAIL" : "ok";
        n += snprintf(out + n, cap - (size_t)n, " %c:%s", letter[k], r);
    }
    return n;
}
