/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_ARCEUS_STEP_H
#define SNX_ARCEUS_STEP_H
#include <stdint.h>
#define ARCEUS_INITIAL_STEP_NS UINT64_C(16666667)
#define ARCEUS_MAX_STEP_NS UINT64_C(100000000)
typedef struct {
    uint64_t last, object, step, initial_ns;
    unsigned next_channel;
} ArceusStep;
/* One wall-clock sample for the four consecutive main-loop updates. Zero-duration writes never enter here.
   A long stall drops excess time instead of moving the world several seconds in one update. */
static inline int arceus_step(ArceusStep* s, uint64_t now, uint64_t object, unsigned channel, uint64_t* out) {
    if (channel > 3) return 0;
    if (channel == 0) {
        uint64_t initial = s->initial_ns ? s->initial_ns : ARCEUS_INITIAL_STEP_NS;
        uint64_t elapsed = s->last && s->object == object && now > s->last ? now - s->last : initial;
        s->last = now;
        s->object = object;
        s->step = elapsed > ARCEUS_MAX_STEP_NS ? ARCEUS_MAX_STEP_NS : elapsed;
        s->next_channel = 1;
    } else {
        if (s->object != object || s->next_channel != channel) return 0;
        s->next_channel++;
    }
    *out = s->step;
    return 1;
}
#endif
