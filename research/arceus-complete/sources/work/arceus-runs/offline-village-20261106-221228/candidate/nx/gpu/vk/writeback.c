/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Reconcile a shader's mapped output with guest memory. The host may have written the guest range while the GPU was
   running: shader-changed bytes land only where the guest still holds the value the shader originally read. */
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"

/* Nested because a result can flush an overlapping render target while landing. */
static _Thread_local unsigned t_result_depth;
void vk_result_begin(void) { t_result_depth++; }
void vk_result_end(void) { t_result_depth--; }
int vk_result_landing(void) { return t_result_depth != 0; }

/* whether [p, p + n) overlaps the guest's address space, where pages the GPU holds fault when read */
static int in_guest(const u8* p, u32 n) { return p < g_mem + AS_SIZE && p + n > g_mem; }

void vk_write_changed(u64 addr, const u8* res, const u8* org, u32 size) {
    /* res and org are host copies: the shader's output read back into host-cached memory, and the bytes it started
       from, snapshotted when it was bound (buffer.c, compute.c, surface.c). Comparing them reads no guest page, so it
       can't fault on one the GPU holds. Only cur below reads guest memory, after vk_rt_before_write */
    if (!size) return;
    if (in_guest(res, size) || in_guest(org, size)) fatal("vulkan: a storage result compared in guest memory");
    if (!memcmp(res, org, size)) return; /* nothing written: no guest read, allocation or flush */
    vk_result_begin();
    vk_rt_before_write(addr, size);
    u8* dst = gmem_ptr(addr, size);
    u8* tmp = NULL;
    const u8* cur = dst;
    int changed = 0;
    if (!cur) {
        tmp = malloc(size);
        if (!tmp) fatal("Out of memory landing a shader storage buffer");
        gmem_read(addr, tmp, size);
        cur = tmp;
    }
    for (u32 i = 0; i < size;) {
        u32 n = size - i < 1024 ? size - i : 1024, end = i + n;
        /* Large storage bindings often change only a few bytes: the host's vectorized comparison of the two host
           copies skips their unchanged ranges, and guest memory is read only where the shader changed a byte */
        if (!memcmp(res + i, org + i, n)) {
            i = end;
            continue;
        }
#if defined(__SSE2__)
        /* The mapped path can commit individual bytes directly. Compare the two
           host snapshots first so an unchanged shader lane never touches guest memory. */
        if (dst) {
            while (end - i >= 16) {
                __m128i old = _mm_loadu_si128((const __m128i *)(org + i));
                __m128i result = _mm_loadu_si128((const __m128i *)(res + i));
                unsigned pending = 0xffffu ^ (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(old, result));
                if (pending == 0xffffu && !memcmp(dst + i, org + i, 16)) {
                    memcpy(dst + i, res + i, 16);
                    changed = 1;
                } else {
                    while (pending) {
                        unsigned lane = (unsigned)__builtin_ctz(pending);
                        if (dst[i + lane] == org[i + lane]) {
                            dst[i + lane] = res[i + lane];
                            changed = 1;
                        }
                        pending &= pending - 1;
                    }
                }
                i += 16;
            }
            if (i == end) continue;
        }
#endif
        do {
            if (end - i >= 8 && !memcmp(res + i, org + i, 8)) {
                i += 8;
                continue;
            }
            if (res[i] == org[i] || cur[i] != org[i]) {
                i++;
                continue;
            }
            u32 start = i;
            while (i < size && res[i] != org[i] && cur[i] == org[i]) i++;
            if (dst)
                memcpy(dst + start, res + start, i - start);
            else
                gmem_write(addr + start, res + start, i - start);
            changed = 1;
        } while (i < end);
    }
    free(tmp);
    if (changed) gpu_mem_written_shader(addr, size); /* other GPU virtual addresses can alias these guest pages */
    vk_result_end();
}
