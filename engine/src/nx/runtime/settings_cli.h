/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_SETTINGS_CLI_H
#define SNX_SETTINGS_CLI_H
/* The settings commands of command.c's dispatch (settings_cli.c): schema prints the settings definition as JSON, get
   reads one value of the game's .ini, set writes one, and check tries one against the definition. argv[1] is
   "settings" and argv[2] the command's word; each returns an exit code of exit_codes.h. They need no window, no game
   start and no poll of the file */
int snx_settings_schema(int argc, char** argv);
int snx_settings_get(int argc, char** argv);
int snx_settings_set(int argc, char** argv);
int snx_settings_check(int argc, char** argv);

#endif
