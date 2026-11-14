/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* nvn_trace=1: the NVN functions a game resolves through nvnBootstrapLoader and nvnDeviceGetProcAddress, how
   often each is called, and how many GPFIFO submissions (by any thread) happen during each queue call. The wrappers
   replace the functions' entries in the recompiler's dispatch table, so they see calls through it (function pointers,
   which is how NVN is called) but not direct branches, unless the game module was recompiled with
   --hook-direct-calls: the counts are lower bounds otherwise. The original functions always run, so the GPU path is
   unchanged. */
#include "nx/gpu/nvn/diag/nvn_trace.h"
#include "nx/gpu/nvn/diag/nvn_enum_trace.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/nvn/test/nvn_exectest.h"
#include "nx/gpu/nvn/test/nvn_selftest.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/options.h"
#include "nx/runtime/rtapi.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

#define VALUE_SEEN_MAX 64 /* distinct queries logged for one function */
enum { SELFTEST_NONE = 0, SELFTEST_DEVICE_INIT, SELFTEST_QUEUE_INIT, EXECTEST_QUEUE_INIT };
enum { VALUE_NONE = 0, VALUE_GET_INTEGER, VALUE_ARGS, VALUE_RESULT };
typedef struct {
    u64 addr, calls;
    GuestFn original;
    int queue_op; /* a queue call: its GPFIFO submissions are counted */
    int size_op;  /* a texture builder storage query: the result and the builder words are logged */
    int selftest_hook; /* SELFTEST_*: the selftest needs this call (nvn_selftest=1) */
    int value_op; /* VALUE_*: a query whose distinct arguments and results are logged (the data of a host handler) */
    u64 seen[VALUE_SEEN_MAX]; /* the keys of the queries logged so far */
    u32 nseen;
    const NvnEnumArgs* enum_args; /* the enum and flag arguments of the function, or NULL */
    NvnEnumSeen enum_seen;        /* the register values logged so far */
    int enum_full_logged;         /* the seen set became full and the one line about it is written */
    char name[128];
} NvProc;
#define NVN_PROC_SLOTS 1024
static NvProc procs[NVN_PROC_SLOTS];
static u32 nprocs;
/* Guest threads can resolve functions at the same time: registration takes the lock, and publishes a slot (addr with
   release ordering) only once its original and name are filled in. Calls read the slots without it. */
static PMutex procs_lock = PMUTEX_INIT;
static GuestFn bootstrap_original, getproc_original;
static int tracing;
static void getproc_trace(CPU* c, u64 entry);

static int readable(u64 addr) {
    if (addr < AS_BASE || addr >= AS_SIZE) return 0;
    MemBlock block;
    mem_query(addr, &block);
    return block.state != MS_FREE && block.state != MS_INACCESSIBLE && (block.perm & PERM_R);
}

/* a NUL-terminated name in guest memory, checked a page at a time (memory permissions are per page) */
NvnNameRead nvn_guest_name_read(u64 ptr, char name[128]) {
    u64 readable_end = 0;
    size_t i;
    for (i = 0; i < 127 && ptr + i >= ptr; i++) {
        u64 at = ptr + i;
        if (at >= readable_end) {
            if (!readable(at)) break;
            readable_end = (at | 0xFFF) + 1;
        }
        name[i] = *(const char*)GP(at);
        if (!name[i]) return i == 0 ? NVN_NAME_EMPTY : NVN_NAME_OK;
    }
    name[i] = 0;
    return i == 127 ? NVN_NAME_LONG : NVN_NAME_UNREADABLE;
}

static int read_name(u64 ptr, char name[128]) { return nvn_guest_name_read(ptr, name) == NVN_NAME_OK; }

int nvn_guest_name(u64 ptr, char name[128]) { return read_name(ptr, name); }

static u32 slot_hash(u64 addr) { return (u32)((addr >> 2) * 2654435761u) & (NVN_PROC_SLOTS - 1); }
/* the slot of addr, or NULL; never loops on a full table */
static NvProc* proc_find(u64 addr) {
    for (u32 n = 0, i = slot_hash(addr); n < NVN_PROC_SLOTS; n++, i = (i + 1) & (NVN_PROC_SLOTS - 1)) {
        u64 a = __atomic_load_n(&procs[i].addr, __ATOMIC_ACQUIRE);
        if (a == addr) return &procs[i];
        if (!a) return NULL;
    }
    return NULL;
}

/* The texture builder is read as raw words: its layout is not known (docs/graphics/nvn-design.md, 2-1). The size is the
   public NVN header's 64 bytes, not verified, so a few words more are read when they are readable. */
#define SIZE_TRACE_COUNT 40       /* queries logged, over both functions */
#define SIZE_TRACE_BUILDER_WORDS 32 /* words of the builder shown */
static u32 size_traced;
static void size_query_trace(CPU* c, u64 entry, NvProc* p) {
    u64 builder = c->x[0];
    u32 words[SIZE_TRACE_BUILDER_WORDS];
    int have = builder < AS_SIZE - sizeof words && readable(builder) && readable(builder + sizeof words - 1);
    if (have) memcpy(words, GP(builder), sizeof words); /* before the call: the call may not change it, but this is safe */
    p->original(c, entry);
    if (__atomic_fetch_add(&size_traced, 1, __ATOMIC_RELAXED) >= SIZE_TRACE_COUNT) return;
    LOG("nvn-size: %s builder=%llx result=%llx\n", p->name, (unsigned long long)builder, (unsigned long long)c->x[0]);
    for (u32 w = 0; have && w < SIZE_TRACE_BUILDER_WORDS; w += 4)
        LOG("nvn-size:   word %2u: %08x %08x %08x %08x\n", w, words[w], words[w + 1], words[w + 2], words[w + 3]);
}

/* The queries (other than nvnDeviceGetInteger) whose arguments and results are logged. The host layer must give the
   same answers (docs/graphics/nvn-design.md, 2-6, G1 to G5). */
static int value_query_name(const char* name) {
    static const char* const names[] = {
        "nvnQueueBuilderGetQueueMemorySize", "nvnTexturePoolGetSize", "nvnSamplerPoolGetSize",
        "nvnMemoryPoolGetFlags",             "nvnBufferBuilderGetSize", "nvnDeviceGetTimestampInNanoseconds",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i])) return 1;
    return 0;
}

/* Queries whose answer the host must give by itself: logs each distinct query once. nvnDeviceGetInteger
   (device, what, int* out) is keyed by `what`, its value is read from out. The timestamp query is keyed by its three
   arguments after the object. The others take only an object, so they are keyed by their result. */
static u32 word_at(u64 addr) { return addr < AS_SIZE - 4 && readable(addr) && readable(addr + 3) ? gr32(addr) : 0xDEADBEEF; }

/* The words of the device and builder objects that the library reads for the two queries below. The host keeps no
   device object, so these values become constants of its handlers (docs/graphics/nvn-design.md, 3b-3). */
static void object_words_trace(const char* name, u64 object) {
    if (!strcmp(name, "nvnDeviceGetTimestampInNanoseconds")) {
        LOG("nvn-value: device %llx +58=%x +5c=%x +19d8=%x +19dc=%x\n", (unsigned long long)object,
            word_at(object + 0x58), word_at(object + 0x5c), word_at(object + 0x19d8), word_at(object + 0x19dc));
    } else if (!strcmp(name, "nvnQueueBuilderGetQueueMemorySize")) {
        static const u32 builder_off[] = {0x10, 0x14, 0x15, 0x16, 0x18, 0x1c, 0x20, 0x24, 0x28};
        static const u32 device_off[] = {0x8c, 0x90, 0x94, 0x98, 0xa4, 0xe0, 0xe4, 0xe8, 0x1a38, 0x1a3c, 0x1a40,
                                         0x1a48, 0x1a68, 0x1a70, 0x1a74, 0x1af4};
        u64 device = object + 8 < AS_SIZE - 8 && readable(object + 8) && readable(object + 15) ? gr64(object + 8) : 0;
        for (size_t i = 0; i < sizeof builder_off / sizeof builder_off[0]; i++)
            LOG("nvn-value: queue builder %llx +%x=%x\n", (unsigned long long)object, builder_off[i],
                word_at(object + builder_off[i]));
        for (size_t i = 0; i < sizeof device_off / sizeof device_off[0]; i++)
            LOG("nvn-value: queue builder device %llx +%x=%x\n", (unsigned long long)device, device_off[i],
                word_at(device + device_off[i]));
    }
}

static void value_query_trace(CPU* c, u64 entry, NvProc* p) {
    u64 a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3];
    u64 out = c->x[2];
    p->original(c, entry);
    u64 key = p->value_op == VALUE_GET_INTEGER ? (a1 & 0xFFFFFFFFu)
              : p->value_op == VALUE_RESULT    ? c->x[0]
                                               : a1 * 0x9E3779B97F4A7C15ull ^ a2 * 31 ^ a3;
    int32_t value = 0;
    int have = p->value_op == VALUE_GET_INTEGER && out < AS_SIZE - 4 && readable(out) && readable(out + 3);
    if (have) memcpy(&value, GP(out), sizeof value);
    pm_lock(&procs_lock);
    u32 i = 0;
    while (i < p->nseen && p->seen[i] != key) i++;
    int fresh = i == p->nseen && p->nseen < VALUE_SEEN_MAX;
    if (fresh) p->seen[p->nseen++] = key;
    pm_unlock(&procs_lock);
    if (!fresh) return;
    if (p->value_op != VALUE_GET_INTEGER) object_words_trace(p->name, a0);
    if (p->value_op == VALUE_GET_INTEGER)
        LOG("nvn-value: %s what=%u value=%d (%s)\n", p->name, (u32)a1, value, have ? "read" : "out not readable");
    else if (p->value_op == VALUE_RESULT)
        LOG("nvn-value: %s result=%llx\n", p->name, (unsigned long long)c->x[0]);
    else
        LOG("nvn-value: %s x1=%llx x2=%llx x3=%llx result=%llx\n", p->name, (unsigned long long)a1,
            (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)c->x[0]);
}

/* The enum and flag arguments of the observed functions: one line for each new value (docs/graphics/nvn-unobserved.md,
   "Rules", item 3). It reads registers only, so the original call runs unchanged. */
static void enum_args_trace(CPU* c, NvProc* p) {
    int full = 0;
    for (u32 r = 0; r < NVN_ENUM_REGS_BITS; r++) {
        if (!(p->enum_args->regs & (1u << r))) continue;
        u32 value = (u32)c->x[r];
        pm_lock(&procs_lock);
        int result = nvn_enum_seen_add(&p->enum_seen, r, value);
        pm_unlock(&procs_lock);
        if (result > 0)
            LOG("nvn-enum: %s w%u=%x\n", p->name, r, value);
        else if (result < 0)
            full = 1;
    }
    if (full && !__atomic_exchange_n(&p->enum_full_logged, 1, __ATOMIC_RELAXED))
        LOG("nvn-enum: %s seen set full, more values not logged\n", p->name);
}

static void proc_trace(CPU* c, u64 entry) {
    NvProc* p = proc_find(entry);
    if (!p) { /* generated code reads g_disp without ordering: on a weakly ordered host the lock gives it */
        pm_lock(&procs_lock);
        p = proc_find(entry);
        pm_unlock(&procs_lock);
        if (!p) fatal("nvn: a trace wrapper runs for a function it never registered (%llx)", (unsigned long long)entry);
    }
    __atomic_fetch_add(&p->calls, 1, __ATOMIC_RELAXED);
    if (p->selftest_hook == SELFTEST_DEVICE_INIT) nvn_selftest_note_device(c->x[0]);
    if (p->selftest_hook == SELFTEST_QUEUE_INIT) nvn_selftest_run(c);
    if (p->selftest_hook == EXECTEST_QUEUE_INIT) {
        /* The test needs the host record of this queue, so it runs after the original call. x0 holds the queue. */
        const u64 queue = c->x[0];
        p->original(c, entry);
        nvn_exectest_run(c, queue);
        return;
    }
    if (p->enum_args) enum_args_trace(c, p);
    if (p->size_op) {
        size_query_trace(c, entry, p);
        return;
    }
    if (p->value_op) {
        value_query_trace(c, entry, p);
        return;
    }
    if (!p->queue_op) {
        p->original(c, entry);
        return;
    }
    if (!strcmp(p->name, "nvnQueueSubmitCommands")) {
        u64 list = 0, ptr = c->x[2]; /* nvnQueueSubmitCommands(queue, count, lists) */
        if (c->x[1] && ptr < AS_SIZE - 8 && readable(ptr) && readable(ptr + 7)) list = gr64(ptr);
        LOG("nvn-submit: queue=%llx count=%u lists=%llx first=%llx\n", (unsigned long long)c->x[0], (u32)c->x[1],
            (unsigned long long)ptr, (unsigned long long)list);
    }
    u64 before = __atomic_load_n(&g_gpu_submits, __ATOMIC_RELAXED);
    p->original(c, entry);
    LOG("nvn-%s: GPFIFO submissions during call=%llu\n", p->name,
        (unsigned long long)(__atomic_load_n(&g_gpu_submits, __ATOMIC_RELAXED) - before));
}

static void trace_proc(u64 addr, const char* name) {
    if (addr < g_code_lo || addr >= g_code_hi || (addr & 3)) return;
    pm_lock(&procs_lock);
    GuestFn* dispatch = &g_disp[(addr - g_code_lo) / 4];
    NvProc* p = proc_find(addr);
    if (p) {
        /* identical functions folded to one address: their calls count under the first name */
        if (strcmp(p->name, name)) LOG_LIMIT(8, "nvn: %s shares its address with %s\n", name, p->name);
    } else if (*dispatch != getproc_trace && nprocs < NVN_PROC_SLOTS) {
        u32 i = slot_hash(addr);
        while (procs[i].addr) i = (i + 1) & (NVN_PROC_SLOTS - 1);
        p = &procs[i];
        p->original = *dispatch ? *dispatch : g_rtapi.miss; /* what snx_lookup gives an empty slot */
        if (p->original == proc_trace) fatal("nvn: the slot of %s already holds a trace wrapper (%llx)", name, (unsigned long long)addr);
        memcpy(p->name, name, strlen(name) + 1);
        p->queue_op = !strcmp(name, "nvnQueueSubmitCommands") || !strcmp(name, "nvnQueueFlush") ||
                      !strcmp(name, "nvnQueueFenceSync") || !strcmp(name, "nvnQueuePresentTexture") ||
                      !strcmp(name, "nvnQueueWaitSync");
        p->size_op = !strcmp(name, "nvnTextureBuilderGetStorageSize") ||
                     !strcmp(name, "nvnTextureBuilderGetStorageAlignment");
        /* The selftest and the execution test need opposite nvn_native values; with both on, the selftest takes the
           queue hook, and the execution test does not run. */
        if (g_settings.nvn_selftest || g_settings.nvn_exectest)
            p->selftest_hook = !strcmp(name, "nvnDeviceInitialize")
                                   ? SELFTEST_DEVICE_INIT
                                   : !strcmp(name, "nvnQueueInitialize")
                                         ? (g_settings.nvn_selftest ? SELFTEST_QUEUE_INIT : EXECTEST_QUEUE_INIT)
                                         : SELFTEST_NONE;
        p->value_op = !strcmp(name, "nvnDeviceGetInteger")                  ? VALUE_GET_INTEGER
                      : !strcmp(name, "nvnDeviceGetTimestampInNanoseconds") ? VALUE_ARGS
                      : value_query_name(name)                              ? VALUE_RESULT
                                                                            : VALUE_NONE;
        p->enum_args = nvn_enum_args(name);
        __atomic_store_n(&p->addr, addr, __ATOMIC_RELEASE);
        nprocs++;
        __atomic_store_n(dispatch, proc_trace, __ATOMIC_RELEASE);
    }
    pm_unlock(&procs_lock);
}

static void getproc_trace(CPU* c, u64 entry) {
    char name[128];
    int valid = read_name(c->x[1], name); /* nvnDeviceGetProcAddress(device, name) */
    GuestFn original = __atomic_load_n(&getproc_original, __ATOMIC_ACQUIRE);
    if (!original) { /* as in proc_trace: the lock orders a host where g_disp's plain load doesn't */
        pm_lock(&procs_lock);
        original = getproc_original;
        pm_unlock(&procs_lock);
        if (!original) fatal("nvn: the nvnDeviceGetProcAddress wrapper runs before its original is known");
    }
    original(c, entry);
    if (!valid) return;
    LOG("nvn: %s -> %llx\n", name, (unsigned long long)c->x[0]);
    trace_proc(c->x[0], name);
}

static void bootstrap_trace(CPU* c, u64 entry) {
    char name[128];
    int valid = read_name(c->x[0], name); /* nvnBootstrapLoader(name) */
    bootstrap_original(c, entry);
    if (!valid) return;
    LOG("nvn: %s -> %llx\n", name, (unsigned long long)c->x[0]);
    if (strcmp(name, "nvnDeviceGetProcAddress")) {
        trace_proc(c->x[0], name);
        return;
    }
    u64 resolver = c->x[0];
    if (resolver < g_code_lo || resolver >= g_code_hi || (resolver & 3)) return;
    pm_lock(&procs_lock);
    GuestFn* slot = &g_disp[(resolver - g_code_lo) / 4];
    if (*slot && !getproc_original) { /* hook once: with nvn_native the slot may hold its hook, which calls back here */
        __atomic_store_n(&getproc_original, *slot, __ATOMIC_RELEASE);
        __atomic_store_n(slot, getproc_trace, __ATOMIC_RELEASE);
    }
    pm_unlock(&procs_lock);
}

void nvn_trace_init(void) {
    tracing = g_settings.nvn_trace;
    if (!tracing && g_settings.nvn_selftest) WARN("nvn-selftest: it needs nvn_trace=1, so it does not run\n");
    if (!tracing && g_settings.nvn_exectest) WARN("exectest: it needs nvn_trace=1, so it does not run\n");
    if (!tracing) return;
    u64 entry = kernel_find_symbol_exact("nvnBootstrapLoader");
    if (!entry || entry < g_code_lo || entry >= g_code_hi || (entry & 3)) {
        WARN("nvn: bootstrap loader symbol is unavailable\n");
        return;
    }
    GuestFn* slot = &g_disp[(entry - g_code_lo) / 4];
    if (!*slot) {
        WARN("nvn: bootstrap loader has no recompiled entry\n");
        return;
    }
    bootstrap_original = *slot;
    *slot = bootstrap_trace; /* before any guest thread runs */
    LOG("nvn: tracing bootstrap function requests (direct calls are only seen with --hook-direct-calls)\n");
}

int nvn_trace_active(void) { return tracing; }

int nvn_trace_library(const char* name, GuestFn* fn, uint64_t* entry) {
    int found = 0;
    pm_lock(&procs_lock);
    for (u32 i = 0; i < NVN_PROC_SLOTS && !found; i++) {
        if (!procs[i].addr || strcmp(procs[i].name, name)) continue;
        *fn = procs[i].original;
        *entry = procs[i].addr;
        found = 1;
    }
    pm_unlock(&procs_lock);
    return found;
}

void nvn_trace_report(void) {
    if (!tracing || !bootstrap_original) return;
    pm_lock(&procs_lock);
    u32 used = 0;
    for (u32 i = 0; i < NVN_PROC_SLOTS; i++) {
        if (!procs[i].addr) continue;
        u64 calls = __atomic_load_n(&procs[i].calls, __ATOMIC_RELAXED);
        if (!calls) continue;
        used++;
        LOG("nvn-used: %s %llu\n", procs[i].name, (unsigned long long)calls);
    }
    LOG("nvn: %u resolved addresses, %u called\n", nprocs, used);
    pm_unlock(&procs_lock);
}
