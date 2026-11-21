/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The game's settings file, <Game>.ini (options.c): its sections, the command line's overrides, reloading it while the
   game runs and writing back what the game's menu changes. Every setting's value is in g_settings (settings.h, from
   settings.txt); what the runtime derives from some of them is here */
#ifndef SNX_OPTIONS_H
#define SNX_OPTIONS_H
#include <stddef.h>

#include "nx/runtime/startup.h"
#include "snx_settings.h"

/* --key=value: k's value for this run, over the file's, which keeps its own. Before ini_open; v is kept, not copied */
void ini_override(const char* k, const char* v);
/* a value the game's menu set, already applied: written to the file at the next poll, even over an edit made meanwhile */
void ini_set(const char* k, const char* v);
/* a setting of the file's sections set by a window: checked, applied as a reload applies it (a startup setting is only
   saved), and saved with ini_set. A setting that a thread other than the main one reads is changed by the window as a
   whole int, as a reload does; the key bindings go through the main thread (backend.h). OPTIONS_OVERRIDDEN: the command
   line holds it for this run */
enum { OPTIONS_OK, OPTIONS_UNKNOWN, OPTIONS_OVERRIDDEN, OPTIONS_REJECTED };
int options_change(const char* key, const char* value);
int options_overridden(const char* key); /* 1 when --key=value is on the command line */
/* a startup setting that options_change saved in this run (the running game keeps its value): 1 with the saved text in
   out, else 0. Any thread */
int options_restart_value(const char* key, char* out, size_t n);
/* every setting of the file's sections but the game's own (a control's place), a line each on stdout, nine fields
   between tabs: the section (with /page for a page of its own), the key, its default, int | float | text, the lowest
   and highest number, the help on one line, the menus' name, and the values as value=word between '|' (empty for a
   typed one; ini_list_text). scarletnx --settings prints the game's sections' (g_ini_sections) for the launcher's
   Settings, --settings=recompile the launcher's own and the recompiler's (g_launch_sections) */
void ini_list(const IniSection* sections, int count);
/* 1 for a key of the file or an unattended-run option (platform/unattended.h): the driver passes only those on to a
   game (driver.h) */
int snx_setting_known(const char* key);
/* reads path, applies it with the overrides, writes it back with its help, and watches it: ini_poll reloads it when it
   changes and saves what the game's menu set */
void ini_open(const char* path);
/* the Debug section's settings not at their default (ini_changed's flags), then gdbstub when the GDB stub runs, between
   ", ": how many. ini_open and a reload show the list at the top of the picture (hud.h), a reload that changes it in the
   log too */
int options_debug(int flags, char* out, size_t n);
/* the path ini_open read, "" before it */
const char* ini_file(void);

/* ---- the CLI's settings commands (settings_cli.c), with no window, no game start and no poll ---- */
/* key's row in the game's .ini's sections (g_ini_sections), NULL for one they do not hold */
const IniOpt* snx_setting_row(const char* key);
/* path read with the runtime's reader into the store (ini_open's read: no overrides, no write, no poll): 1, 0 when
   there is no file, -1 when it cannot be read (the system's reason in why) */
int snx_settings_load(const char* path, char* why, size_t n);
/* key's value as the store last read it, else the row's default; NULL for a row with no default the file lacks */
const char* snx_settings_value(const IniOpt* o);
/* key=value put in the store and the file ini_open read written back as ini_open writes it ("write": beside it,
   "replace": over it): 0, 1 for a value the store cannot keep (why in out), or -1 with the stage, the path and the
   system's reason in out */
int snx_settings_save(const char* key, const char* value, char* out, size_t n);
/* about once a second, from the main loop. final: at exit, where an edit still being saved is left for the next start
   unless the menu's changes need writing, which is then done over the file as it is */
void ini_poll(int final);
/* fps_limit: 0 = no limit, SNX_FPS_GAME = the game's own pace (its swap interval), else frames a second (10 to 1000) */
#define SNX_FPS_GAME (-1)
extern int g_opt_fps;                              /* atomic: the game's threads read it */
void fps_limit_text(int fps, char* out, size_t n); /* g_opt_fps as fps_limit's value: "game", "0", "30"... */
/* ldn, read: local wireless over the LAN (ldn_mitm's protocol), through an Eden room, or off
   (hle/services/network/ldn.c) */
enum { SNX_OPT_LDN_LAN, SNX_OPT_LDN_ROOM, SNX_OPT_LDN_OFF };
extern int g_opt_ldn;

#endif
