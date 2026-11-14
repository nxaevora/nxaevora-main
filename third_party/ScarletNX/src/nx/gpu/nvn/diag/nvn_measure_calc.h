/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the nvn measure (gpu/nvn/diag/nvn_measure.c): the frame time statistics of one window, the mean
   milliseconds of a counter, the indexes of the largest keys and the holder codes of an objs_lock take. No runtime
   header, no global state, so the host test links it alone. */
#ifndef SNX_GPU_NVN_MEASURE_CALC_H
#define SNX_GPU_NVN_MEASURE_CALC_H
#include <stdint.h>

/* the presents of one log window */
#define NVN_MEASURE_WINDOW 600u

typedef struct {
    uint32_t median_us;
    uint32_t p99_us;
    uint32_t max_us;
} NvnMeasureFrameStats;

/* Writes the median, the 99th percentile and the maximum of count frame times in microseconds to out. The
   percentile is nearest rank: index = ceil(p * count / 100) - 1 on a sorted copy. count 0 gives 0 for all three.
   Only the first NVN_MEASURE_WINDOW entries are read, and the input array is not changed. */
void nvn_measure_frame_stats(const uint32_t* frame_us, uint32_t count, NvnMeasureFrameStats* out);

/* The mean milliseconds of ns over presents, 0 when presents is 0. */
double nvn_measure_ms(uint64_t ns, uint64_t presents);

/* Writes the indexes of the at most k largest keys with a key above 0 to out_index, largest first, and returns the
   number written. Equal keys keep index order. It allocates nothing and does not change keys. count 0 or k 0 gives
   0. */
uint32_t nvn_measure_top(const uint64_t* keys, uint32_t count, uint32_t* out_index, uint32_t k);

/* The classes of the holder of objs_lock, in the order of the report. */
enum {
    NVN_MEASURE_HOLDER_UNKNOWN = 0,
    NVN_MEASURE_HOLDER_HANDLER,
    NVN_MEASURE_HOLDER_GPU,
    NVN_MEASURE_HOLDER_OTHER,
    NVN_MEASURE_HOLDER_CLASSES
};

typedef struct {
    uint32_t holder_class; /* NVN_MEASURE_HOLDER_* */
    uint32_t slot;         /* the slot index of a handler, 0 for the other classes */
} NvnMeasureHolder;

/* The holder code: slot1 (a handler's slot index + 1, 0 when not in a handler) when it is 1..slots, else the GPU code
   when on_gpu_thread, else the other-thread code. The codes depend on the slot count: 1..slots handlers, slots + 1 the
   GPU thread, slots + 2 another thread. */
uint32_t nvn_measure_holder_code(uint32_t slot1, int on_gpu_thread, uint32_t slots);

/* The class and the slot index of a holder code. A code above the known codes is unknown. */
NvnMeasureHolder nvn_measure_holder_from_code(uint32_t code, uint32_t slots);

#endif
