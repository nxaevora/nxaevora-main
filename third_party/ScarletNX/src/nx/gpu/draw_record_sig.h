/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the draw record (gpu/draw_record.c): the signature of one recorded call, the frame's two fold
   hashes, the text of a draw, the signature table and the shader hash cache. No global state, no I/O, no guest
   memory. */
#ifndef SNX_GPU_DRAW_RECORD_SIG_H
#define SNX_GPU_DRAW_RECORD_SIG_H
#include "common/cpu/aarch64/cpu.h"
#include "nx/gpu/gpu.h"

/* the bytes of one entry's text: a draw's line with six shader hashes and a few targets */
#define DRAWREC_TEXT_MAX 256u
/* entries of the signature table (a power of two, open addressing) */
#define DRAWREC_TABLE_SIZE 65536u

typedef enum {
    DRAWREC_KIND_DRAW = 0,
    DRAWREC_KIND_CLEAR,
    DRAWREC_KIND_DISPATCH,
    DRAWREC_KIND_BLIT,
    DRAWREC_KIND_DMA_COPY,
    DRAWREC_KIND_BLIT_MEM,
    DRAWREC_KIND_DRAW_TEXTURE,
    DRAWREC_KIND_PRESENT,
    DRAWREC_KIND_COUNT
} DrawRecKind;

/* Each returns the 64-bit signature of one call. The register words come from the engine's file; a shader hash is 0
   for a program that is not enabled. The address words of targets and programs are not part of a signature. */
u64 drawrec_sig_draw(const u32* r3d, int indexed, u32 topology, u32 count, u32 instances, const u64 shader[6]);
u64 drawrec_sig_clear(const u32* r3d, u32 v);
u64 drawrec_sig_dispatch(const u32* q, u64 shader);
u64 drawrec_sig_blit(const Surf2D* s, const Surf2D* d, int dx, int dy, int dw, int dh);
u64 drawrec_sig_dma_copy(const GpuDmaOp* o);
u64 drawrec_sig_blit_mem(const GpuBlit* b);
u64 drawrec_sig_draw_texture(const u32* r3d);
u64 drawrec_sig_present(u32 w, u32 h, u32 pitch, u32 layout, u32 fmt);

/* The GPU addresses of MAXWELL3D_PIPELINES' 6 programs, 0 for each one that the engine does not run. */
void drawrec_program_addrs(const u32* r3d, u64 out[6]);
/* The address of the compute program that the launch description q names, from code_loc. */
u64 drawrec_dispatch_addr(const u32* q, u64 code);

/* The frame's two hashes: one that depends on the order of the signatures, one that does not (a sum). */
u64 drawrec_fold_ordered(u64 prev, u64 sig);
u64 drawrec_fold_sum(u64 sum, u64 sig);

/* The short text of a draw, NUL terminated, cut to cap bytes (0: nothing). */
void drawrec_draw_text(const u32* r3d, int indexed, u32 topology, u32 count, u32 instances, const u64 shader[6],
                       char* out, u32 cap);

typedef struct DrawRecEntry {
    u64 sig;
    u32 count;
    u8 used;
    char text[DRAWREC_TEXT_MAX];
} DrawRecEntry;

typedef struct DrawRecTable {
    DrawRecEntry* entries;
    u32 size; /* a power of two */
    u32 used;
    u64 dropped; /* signatures that came when every slot was taken */
} DrawRecTable;

/* Makes the caller's array the table and clears it. size must be a power of two. */
void drawrec_table_init(DrawRecTable* t, DrawRecEntry* entries, u32 size);
/* Stores sig with text, or counts one more of it. 1 when it is in the table, 0 when the table is full (dropped). */
int drawrec_table_insert(DrawRecTable* t, u64 sig, const char* text);
const DrawRecEntry* drawrec_table_find(const DrawRecTable* t, u64 sig);

/* One program's hash, kept by its GPU address and kind. checked is the present count of its last memory check. */
typedef struct DrawRecShaderEntry {
    u64 gva;
    u64 hash;
    u64 gw;      /* gpu_mem_generation() before the code was read */
    u64 checked; /* rec_frame.frames of the last check */
    u32 len;     /* guest bytes of the code that the hash covered */
    u32 wseen;   /* vk_ww_now() before the code was read */
    u8 used;
    u8 compute; /* a compute program's key space is separate from a graphics program's */
} DrawRecShaderEntry;

typedef struct DrawRecShaderCache {
    DrawRecShaderEntry* entries;
    u32 size; /* a power of two */
} DrawRecShaderCache;

/* Makes the caller's array the cache and clears it. size must be a power of two. */
void drawrec_shader_cache_init(DrawRecShaderCache* c, DrawRecShaderEntry* entries, u32 size);
/* The entry of gva and kind, NULL when the cache has none. */
DrawRecShaderEntry* drawrec_shader_cache_find(DrawRecShaderCache* c, u64 gva, int compute);
/* Stores a program's hash and its baselines. NULL when every slot is taken. */
DrawRecShaderEntry* drawrec_shader_cache_insert(DrawRecShaderCache* c, u64 gva, int compute, u64 hash, u32 len,
                                                u64 gw, u32 wseen, u64 frame);
/* Forgets every program: another one may be at its address now. */
void drawrec_shader_cache_clear(DrawRecShaderCache* c);
/* 1 when the entry needs a memory check for this present: it was not checked yet for it. */
int drawrec_shader_entry_due(const DrawRecShaderEntry* e, u64 frames);

#endif
