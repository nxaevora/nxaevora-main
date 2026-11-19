/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The SNX_ARCEUS_PASS_GUEST test's stand-in for the log: every line kept in one buffer, and counted */
#pragma once
#include <stdio.h>
#include <string.h>
extern char g_test_log[65536];
extern int g_test_lines;
#define LOG(...)                                                                                                       \
    do {                                                                                                               \
        size_t at_ = strlen(g_test_log);                                                                               \
        if (at_ < sizeof g_test_log) snprintf(g_test_log + at_, sizeof g_test_log - at_, __VA_ARGS__);                 \
        g_test_lines++;                                                                                                \
    } while (0)
#define WARN(...) LOG(__VA_ARGS__)
