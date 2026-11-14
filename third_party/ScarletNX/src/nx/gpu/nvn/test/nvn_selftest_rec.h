/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_REC_H
#define SNX_NVN_SELFTEST_REC_H

/* The recording Renderer of the NVN selftest (nvn-unobserved.md, "The selftest"): a test double that logs the engine's
   calls instead of drawing. The selftest makes it the g_renderer while it runs on its private channel, so the stream of
   the library and the replay of the host both end here and their records can be compared (kinds S and Y). */

#include "nx/gpu/nvn/test/nvn_selftest_cmp.h"
#include "nx/gpu/renderer.h"

extern const Renderer nvn_st_renderer;

/* The bytes of one merged front-end write run. A longer append or run sets rec->overflow and is not recorded. */
#define NVN_ST_WRITE_RUN_MAX 0x10000u

/* Called at a draw, a clear and a dispatch, before the call is logged: the owner adds the fields of the channel's state
   (targets, viewports, blend, depth, vertex state, bound objects) to rec->state. NULL logs the call arguments only. */
typedef void (*NvnStSnapshotFn)(GpuChannel* ch, NvnStRec* rec);

/* Starts the recording into rec (state calls go to rec->state, sync operations to rec->sync). One recording at a time.
   The recorder takes no lock: the selftest has one private channel, and reads rec after the GPU job of that channel is
   done. nvn_st_renderer_end returns the number of calls that the recorder logged. */
void nvn_st_renderer_begin(NvnStRec* rec, NvnStSnapshotFn snapshot);
unsigned nvn_st_renderer_end(void);

/* The GPU address of the texture pool of the side that records now, for the write records. 0 means none.
   nvn_st_renderer_begin sets it to 0. */
void nvn_st_renderer_set_texture_pool(u64 gva);

#endif
