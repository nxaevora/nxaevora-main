/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The subcommand dispatch: argv[1] recompile, run or settings, translated to the old option form, and the settings
   commands that read, write and check one value (settings_cli.c). The old flags stay as aliases
   (docs/ui/engine-ui-plan.md, unit 3) */
#include "nx/runtime/command.h"

#include "common/utilities/common.h"
#include "common/utilities/exit_codes.h"
#include "nx/runtime/settings_cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNX_COMMAND_PREFIX "nxaevora-engine: "

/* a branch that ends the program prints after the console is attached: on Windows this is a GUI program, and its
   output reaches a terminal only then (snx_tool_console). The paths that return -1 print nothing here */
void snx_command_error(const char* fmt, ...) {
    va_list ap;
    snx_tool_console();
    fputs(SNX_COMMAND_PREFIX, stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void snx_command_print(const char* text) {
    snx_tool_console();
    fputs(text, stdout);
    fputc('\n', stdout);
}

int snx_command_help(int argc, char** argv, int first, const char* name, const char* usage) {
    int i, found = 0;
    for (i = first; i < argc; ++i)
        if (!strcmp(argv[i], "--help")) found = 1;
    if (!found) return -1;
    if (argc - first != 1) {
        snx_command_error("%s: --help takes no other argument", name);
        return SNX_EXIT_USAGE;
    }
    snx_command_print(usage);
    return SNX_EXIT_OK;
}

/* the old form's argv: one allocation the caller keeps until exit, never freed. NULL on failure */
static char** new_argv(int slots) {
    return (char**)malloc((size_t)slots * sizeof(char*));
}

static const char* settings_usage(void) {
    return "Usage: nxaevora-engine settings <command>\n"
           "  list [--scope=game|recompile]   Print the settings text, nine fields a line\n"
           "  schema                          Print the settings definition as one JSON document\n"
           "  get KEY --game=PATH             Print one value of the game's .ini\n"
           "  set KEY VALUE --game=PATH       Write one value of the game's .ini\n"
           "  check KEY VALUE                 Check one value against the settings definition\n"
           "Each command takes --help. logs and cache come later";
}

static const char* list_usage(void) {
    return "Usage: nxaevora-engine settings list [--scope=game|recompile]\n"
           "  --scope=game           The game's settings (the default)\n"
           "  --scope=recompile      The launcher's and the recompiler's settings";
}

static int translate_recompile(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out = new_argv(argc + 1);
    int i;
    if (!out) {
        snx_command_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[0] = argv[0];
    out[1] = "-r";
    for (i = 2; i < argc; ++i) out[i] = argv[i];
    out[argc] = NULL;
    *out_argc = argc;
    *out_argv = out;
    return -1;
}

static int translate_run(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out;
    int i, n = 0;
    if (argc < 3) {
        snx_command_error("run: name a game program");
        return SNX_EXIT_USAGE;
    }
    if (!strcmp(argv[2], "--help")) {
        snx_command_print("Usage: nxaevora-engine run PROGRAM [--KEY=VALUE] [-- GAME_ARGUMENTS...]\n"
                          "  PROGRAM                A game made already, or an NRO");
        return SNX_EXIT_OK;
    }
    if (argv[2][0] == '-') {
        snx_command_error("run: %s is an option, not a program", argv[2]);
        return SNX_EXIT_USAGE;
    }
    out = new_argv(argc);
    if (!out) {
        snx_command_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[n++] = argv[0];
    for (i = 2; i < argc; ++i) out[n++] = argv[i];
    out[n] = NULL;
    *out_argc = n;
    *out_argv = out;
    return -1;
}

/* 1 for the words of the settings commands that settings_cli.c runs (the runtime's): schema, get, set and check */
static int is_setting_command(const char* job) {
    return !strcmp(job, "schema") || !strcmp(job, "get") || !strcmp(job, "set") || !strcmp(job, "check");
}

static int translate_settings(int argc, char** argv, int* out_argc, char*** out_argv) {
    char** out;
    int status;
    const char* job = argc > 2 ? argv[2] : NULL;
    if (!job) {
        snx_command_error("settings: name a command: list, schema, get, set or check");
        return SNX_EXIT_USAGE;
    }
    if (!strcmp(job, "--help")) return snx_command_help(argc, argv, 2, "settings", settings_usage());
    if (!strcmp(job, "list")) {
        status = snx_command_help(argc, argv, 3, "settings list", list_usage());
        if (status != -1) return status;
    } else if (is_setting_command(job)) {
        /* each command finishes here, with its own use of the arguments (settings_cli.c) */
#if defined(SNX_WITH_RUNTIME)
        if (!strcmp(job, "schema")) return snx_settings_schema(argc, argv);
        if (!strcmp(job, "get")) return snx_settings_get(argc, argv);
        if (!strcmp(job, "set")) return snx_settings_set(argc, argv);
        return snx_settings_check(argc, argv);
#else
        snx_command_error("settings: %s: the settings commands are not in a build of the host tools", job);
        return SNX_EXIT_USAGE;
#endif
    } else {
        snx_command_error("settings: %s: not a command (list, schema, get, set or check)", job);
        return SNX_EXIT_USAGE;
    }
    if (argc > 4) {
        snx_command_error("settings list: %s: unexpected argument", argv[4]);
        return SNX_EXIT_USAGE;
    }
    if (argc == 4 && strncmp(argv[3], "--scope=", 8)) {
        snx_command_error("settings list: %s: unexpected argument", argv[3]);
        return SNX_EXIT_USAGE;
    }
    if (argc == 4 && strcmp(argv[3] + 8, "game") && strcmp(argv[3] + 8, "recompile")) {
        snx_command_error("settings list: --scope %s: not game or recompile", argv[3] + 8);
        return SNX_EXIT_USAGE;
    }
    out = new_argv(3);
    if (!out) {
        snx_command_error("out of memory");
        return SNX_EXIT_INTERNAL;
    }
    out[0] = argv[0];
    out[1] = argc == 4 && !strcmp(argv[3] + 8, "recompile") ? "--settings=recompile" : "--settings";
    out[2] = NULL;
    *out_argc = 2;
    *out_argv = out;
    return -1;
}

int snx_command_translate(int argc, char** argv, int* out_argc, char*** out_argv) {
    if (!out_argc || !out_argv) return SNX_EXIT_INTERNAL;
    *out_argc = argc;
    *out_argv = argv;
    if (argc < 2 || !argv) return -1;
    if (!strcmp(argv[1], "recompile")) return translate_recompile(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "run")) return translate_run(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "settings")) return translate_settings(argc, argv, out_argc, out_argv);
    if (!strcmp(argv[1], "logs") || !strcmp(argv[1], "cache")) {
        snx_command_error("%s: command not available in this build", argv[1]);
        return SNX_EXIT_USAGE;
    }
    return -1;
}
