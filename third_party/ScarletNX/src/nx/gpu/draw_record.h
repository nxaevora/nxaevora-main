/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The draw record of the stage 4 compare (nvn_drawrec, docs/graphics/nvn-design.md): a Renderer wrapper that records
   the draw, clear, dispatch, copy and present calls of either NVN path, writes one line per frame, and keeps a
   signature table. It changes nothing that the game or the renderer reads. */
#ifndef SNX_GPU_DRAW_RECORD_H
#define SNX_GPU_DRAW_RECORD_H
#include "common/cpu/aarch64/cpu.h"

/* nvn_drawrec=1: copies the renderer and wraps the recorded calls. Runs once in snx_main, before any thread uses
   g_renderer. A file that cannot be opened turns the record off and the game goes on. */
void draw_record_init(void);
/* At a clean exit: writes the signature table. Safe when the record is off. */
void draw_record_save(void);

#endif
