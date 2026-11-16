/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The nvn_measure setting (docs/graphics/nvn-design.md, "Stage 5 measure plan"): CPU time counters for the guest and
   GPU sides of both NVN paths, and one log line each 600 presents with the frame times and the draw counts. */
#ifndef SNX_GPU_NVN_MEASURE_H
#define SNX_GPU_NVN_MEASURE_H
#include <stdint.h>

/* Each takes the nanoseconds of one timed scope. The caller reads the clock only when nvn_measure is on. */
void nvn_measure_old_guest(uint64_t ns);
void nvn_measure_old_gpu(uint64_t ns);
void nvn_measure_native_guest(uint64_t ns);
void nvn_measure_native_gpu(uint64_t ns);
void nvn_measure_native_lock(uint64_t ns);

/* The parts of one draw, in the order of the nvn-measure log line. DRAW is the whole render_draw call, BIND is the
   native path's replay_bind_send_all outside it, and each other part is one span inside render_draw. */
typedef enum {
    NVN_MEASURE_PART_DRAW = 0,
    NVN_MEASURE_PART_SHADER,
    NVN_MEASURE_PART_FSKEY,
    NVN_MEASURE_PART_TARGETS,
    NVN_MEASURE_PART_BUFFERS,
    NVN_MEASURE_PART_SETS,
    NVN_MEASURE_PART_BIND,
    NVN_MEASURE_PART_COUNT
} NvnMeasurePart;

/* Adds the nanoseconds of one part of a draw to its window counter. An out-of-range part is ignored. */
void nvn_measure_part(NvnMeasurePart part, uint64_t ns);

/* One present at now_ns (plat_ns). Runs under vk_lock, so its window state needs no lock. It returns at once when
   nvn_measure is off. */
void nvn_measure_present(uint64_t now_ns);

#endif
