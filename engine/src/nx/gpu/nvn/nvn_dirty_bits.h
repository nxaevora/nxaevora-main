/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The dirty bitmap of one bind slot group: a bind sets the bit of its slot, and the sender takes the set bits at the
   next draw or dispatch. No runtime header and no global state, so the host test links it alone. */
#ifndef SNX_NVN_DIRTY_BITS_H
#define SNX_NVN_DIRTY_BITS_H
#include <stdint.h>

/* The words of a bitmap with n slots: bit slot % 64 of word slot / 64 is the slot. */
#define NVN_DIRTY_WORDS(n) (((n) + 63u) / 64u)

/* Sets the bit of slot. A slot at or above nwords * 64 is ignored. */
static inline void nvn_dirty_set(uint64_t* words, uint32_t nwords, uint32_t slot) {
    if (!words || slot >= nwords * 64u) return;
    words[slot / 64u] |= 1ull << (slot % 64u);
}

/* Takes the lowest set bit, writes its slot to slot and clears the bit. 0 when no bit is set. */
static inline int nvn_dirty_take(uint64_t* words, uint32_t nwords, uint32_t* slot) {
    if (!words || !slot) return 0;
    for (uint32_t i = 0; i < nwords; i++) {
        if (!words[i]) continue;
        *slot = i * 64u + (uint32_t)__builtin_ctzll(words[i]);
        words[i] &= words[i] - 1u;
        return 1;
    }
    return 0;
}

/* Clears every bit of the bitmap. */
static inline void nvn_dirty_clear(uint64_t* words, uint32_t nwords) {
    if (!words) return;
    for (uint32_t i = 0; i < nwords; i++) words[i] = 0;
}

#endif
