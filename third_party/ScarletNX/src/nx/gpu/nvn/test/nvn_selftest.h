/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_H
#define SNX_NVN_SELFTEST_H

/* nvn_selftest=1: the same scripted call goes to the game's NVN library and to the host handler, and the results are
   compared (nvn-unobserved.md, "The selftest"). It needs nvn_trace=1 (the trace holds the library's entries) and
   nvn_native=0 (the library must run). The objects and memory of the selftest are its own. */

#include "common/cpu/aarch64/cpu.h"

/* nvnDeviceInitialize(device, builder) was called: x0 is the device object that the library made. */
void nvn_selftest_note_device(uint64_t device);
/* Runs the cases once, on the guest thread that is about to call nvnQueueInitialize: the device exists, the game has
   resolved its functions, and no frame has run. The CPU context is restored when it returns. */
void nvn_selftest_run(CPU* c);

#endif
