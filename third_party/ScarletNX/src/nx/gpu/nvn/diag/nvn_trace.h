/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_NVN_TRACE_H
#define SNX_NVN_TRACE_H
#include <stdint.h>
#include "common/cpu/aarch64/cpu.h"

/* nvn_trace=1: an inventory of the NVN functions the game resolves and calls (gpu/nvn/diag/nvn_trace.c). Init runs once the
   game's symbols and dispatch table are loaded, before any guest thread starts. */
void nvn_trace_init(void);
void nvn_trace_report(void);
/* 1 once nvn_trace_init found nvn_trace=1: wrappers replace dispatch slots */
int nvn_trace_active(void);
/* How a name read ended: OK has a NUL in its first 127 bytes after one other byte at least, EMPTY has a NUL first,
   LONG has 127 readable bytes and no NUL, UNREADABLE has a byte that cannot be read before the NUL (ptr 0 too). */
typedef enum { NVN_NAME_OK, NVN_NAME_EMPTY, NVN_NAME_LONG, NVN_NAME_UNREADABLE } NvnNameRead;
/* Reads the NUL-terminated name at a guest pointer into name (128 bytes, checked a page at a time). Shared with
   gpu/nvn/nvn_native.c. */
NvnNameRead nvn_guest_name_read(uint64_t ptr, char name[128]);
/* 1 when nvn_guest_name_read returned NVN_NAME_OK. */
int nvn_guest_name(uint64_t ptr, char name[128]);
/* The library's function that the game resolved as name: its entry and its address (what ran before any wrapper). 1
   when the game resolved it. With nvn_native=1 the entry is the host handler's, so the selftest does not use it. */
int nvn_trace_library(const char* name, GuestFn* fn, uint64_t* entry);

#endif
