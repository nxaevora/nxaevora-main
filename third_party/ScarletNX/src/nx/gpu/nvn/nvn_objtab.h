/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_OBJTAB_H
#define SNX_NVN_OBJTAB_H
#include <stddef.h>
#include <stdint.h>

/* The host's records of the NVN objects, keyed by the guest address of the object (nvn-design.md, 2-1). The guest
   memory of an object is never written: the game allocates it, and the host keeps its own state here. The table does
   no locking; the owner serializes the calls. */

typedef enum {
    NVN_OBJ_NONE = 0, /* a removed record (kept so that a probe goes on past it) */
    NVN_OBJ_DEVICE,
    NVN_OBJ_DEVICE_BUILDER,
    NVN_OBJ_QUEUE,
    NVN_OBJ_QUEUE_BUILDER,
    NVN_OBJ_WINDOW,
    NVN_OBJ_WINDOW_BUILDER,
    NVN_OBJ_SYNC,
    NVN_OBJ_MEMORY_POOL,
    NVN_OBJ_MEMORY_POOL_BUILDER,
    NVN_OBJ_TEXTURE,
    NVN_OBJ_TEXTURE_BUILDER,
    NVN_OBJ_TEXTURE_VIEW,
    NVN_OBJ_TEXTURE_POOL,
    NVN_OBJ_SAMPLER_POOL,
    NVN_OBJ_SAMPLER_BUILDER,
    NVN_OBJ_SAMPLER,
    NVN_OBJ_BUFFER_BUILDER,
    NVN_OBJ_BUFFER,
    NVN_OBJ_COMMAND_BUFFER,
    NVN_OBJ_PROGRAM,
    NVN_OBJ_COLOR_STATE,
    NVN_OBJ_DEPTH_STENCIL_STATE,
    NVN_OBJ_BLEND_STATE,
    NVN_OBJ_POLYGON_STATE,
    NVN_OBJ_MULTISAMPLE_STATE,
    NVN_OBJ_CHANNEL_MASK_STATE,
    NVN_OBJ_VERTEX_ATTRIB_STATE,
    NVN_OBJ_VERTEX_STREAM_STATE,
    NVN_OBJ_EVENT_BUILDER,
    NVN_OBJ_EVENT,
    NVN_OBJ_KIND_COUNT
} NvnObjKind;

typedef struct {
    uint64_t addr;       /* the guest address; 0 marks an empty slot */
    uint32_t kind;       /* NvnObjKind */
    uint32_t generation; /* changes each time a record is made for the address */
    void* data;          /* the host state, zero filled at creation */
} NvnObj;

typedef struct {
    NvnObj* slot;
    uint32_t capacity; /* a power of two, or 0 before the first record */
    uint32_t used;     /* slots with an address, removed ones included */
    uint32_t next_generation;
} NvnObjTab;

void nvn_objtab_free(NvnObjTab* t); /* frees every record's data */
/* the live record at addr, or NULL (never for a removed one) */
NvnObj* nvn_objtab_find(const NvnObjTab* t, uint64_t addr);
/* a record for addr with size bytes of zeroed data, replacing a live or removed one. NULL when out of memory or when
   addr is 0. */
NvnObj* nvn_objtab_put(NvnObjTab* t, uint64_t addr, NvnObjKind kind, size_t size);
/* 1 if a live record was removed */
int nvn_objtab_remove(NvnObjTab* t, uint64_t addr);
/* 1 when addr still has the record that carried this generation (a recorded command names objects so) */
int nvn_objtab_valid(const NvnObjTab* t, uint64_t addr, uint32_t generation);
uint32_t nvn_objtab_count(const NvnObjTab* t, NvnObjKind kind);

#endif
