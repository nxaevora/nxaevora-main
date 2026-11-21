/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_COMMAND_H
#define SNX_COMMAND_H
/* The subcommand dispatch (command.c): argv[1] as recompile, run or settings, translated to the old option form
   before the driver reads it, and the settings commands, which finish there (docs/ui/engine-ui-plan.md, unit 3). -1:
   the old form goes on, with *out_argc and *out_argv set (maybe argv); else an exit code of exit_codes.h and the
   command is finished */
int snx_command_translate(int argc, char** argv, int* out_argc, char*** out_argv);
/* A line of a command on stderr, after the nxaevora-engine: prefix, and text on stdout: the Windows console is
   attached first, since the program is a GUI one and its output reaches a terminal only then */
void snx_command_error(const char* fmt, ...);
void snx_command_print(const char* text);
/* --help as every command takes it, the five settings words too: argv[first...] holding --help alone prints usage to
   stdout (SNX_EXIT_OK); with another argument it says so and returns SNX_EXIT_USAGE; -1 when there is no --help */
int snx_command_help(int argc, char** argv, int first, const char* name, const char* usage);

#endif
