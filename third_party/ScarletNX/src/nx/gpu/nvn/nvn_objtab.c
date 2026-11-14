/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/nvn_objtab.h"
#include <stdlib.h>

#define OBJTAB_MIN_CAPACITY 256u
#define OBJTAB_MAX_LOAD_NUM 3u /* grow when used slots pass 3/4 of the capacity */
#define OBJTAB_MAX_LOAD_DEN 4u

static uint32_t hash_addr(uint64_t addr, uint32_t mask) {
    return (uint32_t)(((addr >> 2) * 0x9E3779B97F4A7C15ull) >> 32) & mask;
}

/* the slot that holds addr (live or removed), or NULL */
static NvnObj* probe(const NvnObjTab* t, uint64_t addr) {
    if (!t->capacity) return NULL;
    uint32_t mask = t->capacity - 1;
    for (uint32_t n = 0, i = hash_addr(addr, mask); n < t->capacity; n++, i = (i + 1) & mask) {
        if (t->slot[i].addr == addr) return &t->slot[i];
        if (!t->slot[i].addr) return NULL;
    }
    return NULL;
}

/* rebuilds the table with room for more records; removed ones are dropped. 0 when out of memory */
static int grow(NvnObjTab* t) {
    uint32_t live = 0, cap = t->capacity ? t->capacity : OBJTAB_MIN_CAPACITY;
    for (uint32_t i = 0; i < t->capacity; i++) live += t->slot[i].addr && t->slot[i].kind != NVN_OBJ_NONE;
    while (live + 1 > cap / 2) cap *= 2; /* at most half full after the rebuild */
    NvnObj* fresh = calloc(cap, sizeof *fresh);
    if (!fresh) return 0;
    for (uint32_t i = 0; i < t->capacity; i++) {
        NvnObj* o = &t->slot[i];
        if (!o->addr) continue;
        if (o->kind == NVN_OBJ_NONE) continue; /* its data was freed at the removal */
        uint32_t mask = cap - 1, j = hash_addr(o->addr, mask);
        while (fresh[j].addr) j = (j + 1) & mask;
        fresh[j] = *o;
    }
    free(t->slot);
    t->slot = fresh;
    t->capacity = cap;
    t->used = live;
    return 1;
}

void nvn_objtab_free(NvnObjTab* t) {
    for (uint32_t i = 0; i < t->capacity; i++) free(t->slot[i].data);
    free(t->slot);
    t->slot = NULL;
    t->capacity = t->used = 0;
}

NvnObj* nvn_objtab_find(const NvnObjTab* t, uint64_t addr) {
    NvnObj* o = addr ? probe(t, addr) : NULL;
    return o && o->kind != NVN_OBJ_NONE ? o : NULL;
}

NvnObj* nvn_objtab_put(NvnObjTab* t, uint64_t addr, NvnObjKind kind, size_t size) {
    if (!addr || kind == NVN_OBJ_NONE) return NULL;
    void* data = calloc(1, size ? size : 1);
    if (!data) return NULL;
    NvnObj* o = probe(t, addr);
    if (!o) {
        if (!t->capacity || (t->used + 1) * OBJTAB_MAX_LOAD_DEN > t->capacity * OBJTAB_MAX_LOAD_NUM) {
            if (!grow(t)) {
                free(data);
                return NULL;
            }
        }
        uint32_t mask = t->capacity - 1, i = hash_addr(addr, mask);
        while (t->slot[i].addr) i = (i + 1) & mask;
        o = &t->slot[i];
        o->addr = addr;
        t->used++;
    }
    free(o->data);
    o->data = data;
    o->kind = kind;
    o->generation = ++t->next_generation;
    return o;
}

int nvn_objtab_remove(NvnObjTab* t, uint64_t addr) {
    NvnObj* o = nvn_objtab_find(t, addr);
    if (!o) return 0;
    free(o->data);
    o->data = NULL;
    o->kind = NVN_OBJ_NONE;
    o->generation = ++t->next_generation; /* a recorded handle to the removed record no longer matches */
    return 1;
}

int nvn_objtab_valid(const NvnObjTab* t, uint64_t addr, uint32_t generation) {
    NvnObj* o = nvn_objtab_find(t, addr);
    return o && o->generation == generation;
}

uint32_t nvn_objtab_count(const NvnObjTab* t, NvnObjKind kind) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->capacity; i++) n += t->slot[i].addr && t->slot[i].kind == (uint32_t)kind;
    return n;
}
