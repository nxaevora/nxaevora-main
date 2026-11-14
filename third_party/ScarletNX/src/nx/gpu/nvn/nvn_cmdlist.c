/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/nvn_cmdlist.h"
#include <stdlib.h>
#include <string.h>

#define OPLIST_MIN_CAPACITY 256u
#define STORE_MIN_CAPACITY  64u
#define HANDLE_INDEX_LIMIT  0x40000000u /* index + 1 fits in the 31 bits above bit 0 */

static uint32_t padded(uint32_t bytes) { return (bytes + NVN_OP_ALIGN - 1) & ~(NVN_OP_ALIGN - 1); }

int nvn_oplist_append(NvnOpList* list, NvnOpCode code, const void* payload, uint32_t bytes) {
    if (code <= NVN_OP_NONE || code >= NVN_OP_COUNT || bytes > NVN_OP_MAX_PAYLOAD || (bytes && !payload)) return 0;
    uint32_t body = padded(bytes);
    size_t need = list->used + sizeof(NvnOpHeader) + body;
    if (need > list->cap) {
        size_t cap = list->cap ? list->cap : OPLIST_MIN_CAPACITY;
        while (cap < need) cap *= 2;
        uint8_t* fresh = realloc(list->data, cap);
        if (!fresh) return 0;
        list->data = fresh;
        list->cap = cap;
    }
    NvnOpHeader header = {(uint32_t)code, body};
    memcpy(list->data + list->used, &header, sizeof header);
    memcpy(list->data + list->used + sizeof header, payload, bytes);
    memset(list->data + list->used + sizeof header + bytes, 0, body - bytes);
    list->used = need;
    list->ops++;
    return 1;
}

int nvn_oplist_next(const NvnOpList* list, size_t* pos, NvnOpHeader* header, const void** payload) {
    if (*pos + sizeof *header > list->used) return 0;
    memcpy(header, list->data + *pos, sizeof *header);
    *payload = list->data + *pos + sizeof *header;
    *pos += sizeof *header + header->bytes;
    return 1;
}

int nvn_oplist_copy(NvnOpList* dst, const NvnOpList* src, NvnListStore* store, uint64_t* failed) {
    if (!dst || !src || !store || !failed) return 0;
    *failed = 0;
    size_t pos = 0;
    NvnOpHeader header;
    const void* payload;
    while (nvn_oplist_next(src, &pos, &header, &payload)) {
        uint64_t call = 0;
        if (header.code == NVN_OP_CALL) {
            if (header.bytes < sizeof(NvnOpCall)) return 0;
            memcpy(&call, payload, sizeof call);
            if (!nvn_list_ref(store, call)) {
                *failed = call;
                return 0;
            }
        }
        if (!nvn_oplist_append(dst, (NvnOpCode)header.code, payload, header.bytes)) {
            if (header.code == NVN_OP_CALL) nvn_list_unref(store, call);
            return 0;
        }
    }
    return 1;
}

void nvn_oplist_free(NvnOpList* list) {
    free(list->data);
    *list = (NvnOpList){0};
}

static int store_reserve(NvnListStore* s) {
    if (s->free_count) return 1;
    if (s->count == s->cap) {
        uint32_t cap = s->cap ? s->cap * 2 : STORE_MIN_CAPACITY;
        if (cap > HANDLE_INDEX_LIMIT) return 0;
        NvnListSlot** fresh = realloc(s->slot, (size_t)cap * sizeof *fresh);
        if (!fresh) return 0;
        s->slot = fresh;
        s->cap = cap;
    }
    return 1;
}

static uint64_t make_handle(uint32_t index, uint32_t generation) {
    return (uint64_t)generation << 32 | (uint64_t)(index + 1) << 1;
}

static NvnListSlot* slot_of(const NvnListStore* s, uint64_t handle) {
    uint32_t low = (uint32_t)handle, generation = (uint32_t)(handle >> 32);
    if (!generation || (low & 1) || !low) return NULL;
    uint32_t index = (low >> 1) - 1;
    if (index >= s->count) return NULL;
    NvnListSlot* slot = s->slot[index];
    return slot->refs && slot->generation == generation ? slot : NULL;
}

uint64_t nvn_list_publish(NvnListStore* s, NvnOpList* list) {
    if (!store_reserve(s)) return 0;
    uint32_t index;
    if (s->free_count) {
        index = s->free_index[--s->free_count];
    } else {
        NvnListSlot* fresh = calloc(1, sizeof *fresh);
        if (!fresh) return 0;
        index = s->count++;
        s->slot[index] = fresh;
    }
    NvnListSlot* slot = s->slot[index];
    if (++s->next_generation == 0) s->next_generation = 1; /* generation 0 never names a list */
    slot->generation = s->next_generation;
    slot->refs = 1;
    slot->list = *list;
    *list = (NvnOpList){0};
    return make_handle(index, slot->generation);
}

const NvnOpList* nvn_list_find(const NvnListStore* s, uint64_t handle) {
    const NvnListSlot* slot = slot_of(s, handle);
    return slot ? &slot->list : NULL;
}

int nvn_list_ref(NvnListStore* s, uint64_t handle) {
    NvnListSlot* slot = slot_of(s, handle);
    if (!slot || slot->refs == UINT32_MAX) return 0;
    slot->refs++;
    return 1;
}

static int free_slot_remember(NvnListStore* s, uint32_t index) {
    if (s->free_count == s->free_cap) {
        uint32_t cap = s->free_cap ? s->free_cap * 2 : STORE_MIN_CAPACITY;
        uint32_t* fresh = realloc(s->free_index, (size_t)cap * sizeof *fresh);
        if (!fresh) return 0;
        s->free_index = fresh;
        s->free_cap = cap;
    }
    s->free_index[s->free_count++] = index;
    return 1;
}

/* one reference of the handle goes; the CALL targets of a freed list go on the stack, so a deep chain does not recurse */
void nvn_list_unref(NvnListStore* s, uint64_t handle) {
    uint64_t* stack = NULL;
    size_t depth = 0, cap = 0;
    for (;;) {
        NvnListSlot* slot = slot_of(s, handle);
        if (slot && --slot->refs == 0) {
            size_t pos = 0;
            NvnOpHeader header;
            const void* payload;
            while (nvn_oplist_next(&slot->list, &pos, &header, &payload)) {
                if (header.code != NVN_OP_CALL) continue;
                if (depth == cap) {
                    size_t grown = cap ? cap * 2 : 16;
                    uint64_t* fresh = realloc(stack, grown * sizeof *fresh);
                    if (!fresh) {
                        s->leaked++;
                        continue;
                    }
                    stack = fresh;
                    cap = grown;
                }
                memcpy(&stack[depth++], payload, sizeof(uint64_t));
            }
            nvn_oplist_free(&slot->list);
            if (!free_slot_remember(s, ((uint32_t)handle >> 1) - 1)) {
                slot->refs = 1; /* the slot cannot be reused: it stays out of use and counts as leaked */
                s->leaked++;
            }
        }
        if (!depth) break;
        handle = stack[--depth];
    }
    free(stack);
}

uint32_t nvn_list_live(const NvnListStore* s) {
    uint32_t live = 0;
    for (uint32_t i = 0; i < s->count; i++) live += s->slot[i]->refs != 0;
    return live;
}

void nvn_list_store_free(NvnListStore* s) {
    for (uint32_t i = 0; i < s->count; i++) {
        nvn_oplist_free(&s->slot[i]->list);
        free(s->slot[i]);
    }
    free(s->slot);
    free(s->free_index);
    *s = (NvnListStore){0};
}

int nvn_owner_claim(uint32_t* owner, uint32_t self) {
    if (!self) return 0;
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(owner, &expected, self, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}

void nvn_owner_release(uint32_t* owner) { __atomic_store_n(owner, 0, __ATOMIC_RELEASE); }
