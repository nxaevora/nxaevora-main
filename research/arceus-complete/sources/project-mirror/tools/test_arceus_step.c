/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/arceus_step.h"
#include <assert.h>
#include <stdio.h>
static void batch(ArceusStep* state, uint64_t now, uint64_t expected) {
    uint64_t result;
    for (unsigned channel = 0; channel < 4; channel++) {
        assert(arceus_step(state, now + channel * 100, 123, channel, &result));
        assert(result == expected);
    }
}
int main(void) {
    ArceusStep state = {0};
    uint64_t result = 0, now = 1000000000, total = 0;
    assert(!arceus_step(&state, now, 123, 1, &result));
    batch(&state, now, ARCEUS_INITIAL_STEP_NS);
    for (unsigned i = 0; i < 120; i++) {
        uint64_t elapsed = i % 2 ? 25000000 : 16666667;
        now += elapsed;
        batch(&state, now, elapsed);
        total += state.step;
    }
    assert(total == now - 1000000000);
    batch(&state, now + 1000000000, ARCEUS_MAX_STEP_NS);
    assert(!arceus_step(&state, now, 999, 1, &result));
    assert(!arceus_step(&state, now, 123, 4, &result));
    assert(arceus_step(&state, now, 999, 0, &result));
    assert(result == ARCEUS_INITIAL_STEP_NS);
    state = (ArceusStep){.initial_ns = 8333333};
    now = 1000000000;
    total = 0;
    batch(&state, now, 8333333);
    for (unsigned i = 0; i < 240; i++) {
        uint64_t elapsed = i % 3 == 0 ? 16666667 : (i % 3 == 1 ? 8333333 : 8333334);
        now += elapsed;
        batch(&state, now, elapsed);
        total += state.step;
    }
    assert(total == now - 1000000000);
    assert(arceus_step(&state, now, 999, 0, &result));
    assert(result == 8333333);
    puts("PASS: four-channel coherence, 60/40 and 120/60 FPS elapsed time, stall cap, object reset, order guards");
    return 0;
}
