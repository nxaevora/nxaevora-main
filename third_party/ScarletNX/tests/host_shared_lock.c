/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* common/filesystem/host.c's shared mode of PMutex: a shared take and an exclusive take exclude each other, and on
   Windows two shared holders run at the same time */
#include "common/filesystem/host.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned test_checks;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        test_checks++;                                                                                                 \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "host_shared_lock.c:%d: check failed: %s\n", __LINE__, #expr);                              \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static PMutex test_lock = PMUTEX_INIT; /* usable before any init runs */

int main(void) {
    /* the shared release frees the mutex for an exclusive take */
    pm_lock_shared(&test_lock);
    pm_unlock_shared(&test_lock);
    CHECK(pm_trylock(&test_lock));
    pm_unlock(&test_lock);
    /* an exclusive holder keeps the shared take out */
    pm_lock(&test_lock);
    CHECK(!pm_trylock_shared(&test_lock));
    pm_unlock(&test_lock);
    CHECK(pm_trylock_shared(&test_lock));
    pm_unlock_shared(&test_lock);
#if defined(_WIN32)
    /* two shared holders at the same time, and the exclusive take waits for both */
    pm_lock_shared(&test_lock);
    CHECK(pm_trylock_shared(&test_lock));
    CHECK(!pm_trylock(&test_lock));
    pm_unlock_shared(&test_lock);
    CHECK(!pm_trylock(&test_lock)); /* one shared holder is still in */
    pm_unlock_shared(&test_lock);
    CHECK(pm_trylock(&test_lock));
    pm_unlock(&test_lock);
#else
    /* the documented fallback: a shared take is a normal take, so a second one fails */
    pm_lock_shared(&test_lock);
    CHECK(!pm_trylock_shared(&test_lock));
    pm_unlock_shared(&test_lock);
#endif
    printf("host_shared_lock: %u checks passed\n", test_checks);
    return 0;
}
