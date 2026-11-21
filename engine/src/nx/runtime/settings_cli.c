/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The settings commands (settings_cli.h): the definition from the generated tables as one JSON document, one value of
   the game's .ini read or written with the runtime's reader and writer, and one value checked against the definition
   only. No window, no game start, no poll (docs/ui/engine-ui-plan.md, unit 3) */
#include "nx/runtime/settings_cli.h"

#include "common/utilities/exit_codes.h"
#include "nx/runtime/command.h"
#include "nx/runtime/ini.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_CLI_PATH 1400 /* the game's .ini's path, as main.c's ini has */
#define SETTINGS_CLI_WORDS 2   /* the key and the value a command names at most */
#define SETTINGS_CLI_JSON 8192 /* the schema's first buffer, then doubled */

static const char* const USAGE_SCHEMA =
    "Usage: nxaevora-engine settings schema\n"
    "  Print the settings definition as one JSON document on stdout";
static const char* const USAGE_GET =
    "Usage: nxaevora-engine settings get KEY --game=PATH\n"
    "  Print KEY's value in the game's .ini, or its default; it writes nothing";
static const char* const USAGE_SET =
    "Usage: nxaevora-engine settings set KEY VALUE --game=PATH\n"
    "  Write one value in the game's .ini; a value the settings refuse is exit 7";
static const char* const USAGE_CHECK =
    "Usage: nxaevora-engine settings check KEY VALUE\n"
    "  Check one value against the settings definition, with no file";

/* the arguments after the command's word: its words and its --game, in any order. Only --game=PATH is an option, since
   a value may start with '-' (-1); word[] beyond the command's count is named in extra */
typedef struct {
    const char* word[SETTINGS_CLI_WORDS];
    int count;
    const char* game;
    const char* extra;
} Args;

static void parse_args(int argc, char** argv, int first, Args* a) {
    int i;
    a->count = 0;
    a->game = NULL;
    a->extra = NULL;
    for (i = first; i < argc; ++i) {
        if (!strncmp(argv[i], "--game=", 7))
            a->game = argv[i] + 7;
        else if (a->count < SETTINGS_CLI_WORDS)
            a->word[a->count++] = argv[i];
        else if (!a->extra)
            a->extra = argv[i];
    }
}

/* The game's settings file, <config>/<name>.ini, for the program path of --game: main.c's naming of the game's path and
   folders, then the .ini that snx_main opens. 0, or -1 for a path that names no game (the folders are not made) */
static int settings_file(const char* program, char* out, size_t n) {
    snx_game_locate(program);
    if (!g_exename[0]) return -1;
    snx_game_folders();
    snprintf(out, n, "%s%s.ini", g_confdir, g_exename);
    return 0;
}

/* the runtime's line about the value it clamped or left, with the command's prefix */
static void say_clamped(const char* line) { snx_command_error("%s", line); }

/* the row messages that more than one command says */
static int unknown_key(const char* name, const char* key) {
    snx_command_error("%s: %s: not a setting of the game's .ini", name, key);
    return SNX_EXIT_USAGE;
}

/* key's row in the settings definition: the game's .ini's (snx_setting_row), then the launcher's and the recompiler's,
   the rows that the schema prints of g_launch_sections (check takes the whole definition, get and set the .ini's) */
static const IniOpt* definition_row(const char* key) {
    const IniOpt* o = snx_setting_row(key);
    int s, t;
    if (o) return o;
    for (s = 0; s < g_launch_sections_count; s++)
        for (t = 0; t < 3 && g_launch_sections[s].table[t]; t++) {
            o = ini_find(g_launch_sections[s].table[t], key);
            if (o) return o;
        }
    return NULL;
}

/* ---- the JSON document of the settings definition ---- */

typedef struct {
    char* text;
    size_t at, size;
    int failed; /* an allocation failed: nothing is printed */
} Json;

static void json_reserve(Json* j, size_t n) {
    char* bigger;
    size_t cap;
    if (j->failed || j->at + n + 1 <= j->size) return;
    cap = j->size ? j->size : SETTINGS_CLI_JSON;
    while (cap < j->at + n + 1) cap *= 2;
    bigger = (char*)realloc(j->text, cap);
    if (!bigger) {
        j->failed = 1;
        return;
    }
    j->text = bigger;
    j->size = cap;
}

static void json_put(Json* j, const char* text) {
    size_t n = strlen(text);
    if (j->failed) return;
    json_reserve(j, n);
    if (j->failed) return;
    memcpy(j->text + j->at, text, n);
    j->at += n;
    j->text[j->at] = 0;
}

static void json_format(Json* j, const char* fmt, ...) {
    char part[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(part, sizeof part, fmt, ap);
    va_end(ap);
    json_put(j, part);
}

/* text as a JSON string: the quote and the backslash escaped, the control characters below 0x20 as \u00XX. NULL is
   JSON null */
static void json_string(Json* j, const char* text) {
    const char* p;
    char one[8];
    if (!text) {
        json_put(j, "null");
        return;
    }
    json_put(j, "\"");
    for (p = text; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            one[0] = '\\';
            one[1] = (char)c;
            one[2] = 0;
            json_put(j, one);
        } else if (c < 0x20) {
            snprintf(one, sizeof one, "\\u%04x", (unsigned)c);
            json_put(j, one);
        } else {
            one[0] = (char)c;
            one[1] = 0;
            json_put(j, one);
        }
    }
    json_put(j, "\"");
}

/* the default as the .ini writes it: a number for an int or a float, a string for a text, null for none */
static void json_default(Json* j, const IniOpt* o) {
    char* end;
    double d;
    if (!o->def) {
        json_put(j, "null");
        return;
    }
    if (o->type == INI_TEXT) {
        json_string(j, o->def);
        return;
    }
    d = strtod(o->def, &end);
    if (end == o->def || *end) {
        json_string(j, o->def);
        return;
    }
    json_format(j, "%.15g", d);
}

/* a row's scope: the launcher's own, the recompiler's (the LLVM Internals are its), or the game's, a startup setting
   or one that applies while it runs */
static const char* row_scope(const IniOpt* table, const IniOpt* o) {
    if (table == g_recompile_ini || table == g_llvm_ini) return "recompile";
    if (table == g_launch_ini) return "launcher";
    return (o->flags & INI_STARTUP) ? "startup" : "in-game";
}

/* the numbers in the digits the nine-field settings text writes them with, so both give the same value */
static void json_row(Json* j, const IniSection* section, int t, const IniOpt* o) {
    static const char* const types[] = {"int", "float", "text"};
    json_put(j, "{\"key\":");
    json_string(j, o->key);
    json_put(j, ",\"section\":");
    json_string(j, section->name);
    json_put(j, ",\"page\":");
    json_string(j, section->page[t]);
    json_put(j, ",\"type\":\"");
    json_put(j, types[o->type]);
    json_put(j, "\",\"min\":");
    if (o->type == INI_TEXT)
        json_put(j, "null");
    else
        json_format(j, "%.15g", o->lo);
    json_put(j, ",\"max\":");
    if (o->type == INI_TEXT)
        json_put(j, "null");
    else
        json_format(j, "%.15g", o->hi);
    json_put(j, ",\"default\":");
    json_default(j, o);
    json_put(j, ",\"words\":");
    json_string(j, o->words);
    json_put(j, ",\"label\":");
    json_string(j, o->name);
    json_put(j, ",\"help\":");
    json_string(j, ini_help(section->table[t], o));
    json_put(j, ",\"scope\":");
    json_string(j, row_scope(section->table[t], o));
    json_put(j, "}");
}

/* every row of a sections array, a comma before all but the document's first */
static void json_sections(Json* j, const IniSection* sections, int count, int* first) {
    int s, t;
    for (s = 0; s < count; s++)
        for (t = 0; t < 3 && sections[s].table[t]; t++) {
            const IniOpt* o;
            for (o = sections[s].table[t]; o->key; o++) {
                if (!*first) json_put(j, ",");
                *first = 0;
                json_row(j, &sections[s], t, o);
            }
        }
}

static int schema_print(void) {
    Json j;
    int first = 1;
    memset(&j, 0, sizeof j);
    json_put(&j, "{\"v\":1,\"settings\":[");
    json_sections(&j, g_ini_sections, g_ini_sections_count, &first);
    json_sections(&j, g_launch_sections, g_launch_sections_count, &first);
    json_put(&j, "]}");
    if (j.failed) {
        free(j.text);
        snx_command_error("settings schema: out of memory");
        return SNX_EXIT_INTERNAL;
    }
    snx_command_print(j.text);
    free(j.text);
    return SNX_EXIT_OK;
}

/* ---- the commands ---- */

int snx_settings_schema(int argc, char** argv) {
    int status = snx_command_help(argc, argv, 3, "settings schema", USAGE_SCHEMA);
    if (status != -1) return status;
    if (argc > 3) {
        snx_command_error("settings schema: %s: unexpected argument", argv[3]);
        return SNX_EXIT_USAGE;
    }
    return schema_print();
}

int snx_settings_get(int argc, char** argv) {
    char path[SETTINGS_CLI_PATH], value[SETTINGS_CLI_PATH], why[256];
    const IniOpt* o;
    const char* text;
    int status = snx_command_help(argc, argv, 3, "settings get", USAGE_GET);
    Args a;
    if (status != -1) return status;
    parse_args(argc, argv, 3, &a);
    if (a.extra) {
        snx_command_error("settings get: %s: unexpected argument", a.extra);
        return SNX_EXIT_USAGE;
    }
    if (a.count != 1) {
        snx_command_error("settings get: name one key, and --game=PATH");
        return SNX_EXIT_USAGE;
    }
    if (!a.game) {
        snx_command_error("settings get: --game=PATH names the game's program");
        return SNX_EXIT_USAGE;
    }
    o = snx_setting_row(a.word[0]);
    if (!o) return unknown_key("settings get", a.word[0]);
    if (settings_file(a.game, path, sizeof path)) {
        snx_command_error("settings get: %s: not a game's program: check --game", a.game);
        return SNX_EXIT_GAME_FILES;
    }
    status = snx_settings_load(path, why, sizeof why);
    if (status == 0) {
        snx_command_error("settings get: no %s: run recompile first, or check --game=PATH", path);
        return SNX_EXIT_GAME_FILES;
    }
    if (status < 0) {
        snx_command_error("settings get: read %s: %s", path, why);
        return SNX_EXIT_INTERNAL;
    }
    text = snx_settings_value(o);
    ini_set_say(say_clamped);
    if (text) {
        /* the row's value as the runtime reads it, its clamp included, but not its set function: that one acts on the
           running game and this command has none (ldn and room stop the run) */
        IniOpt row = *o;
        row.set = NULL;
        ini_read(&row, text);
        ini_format(&row, value, sizeof value);
    } else
        ini_format(o, value, sizeof value);
    snx_command_print(value);
    return SNX_EXIT_OK;
}

int snx_settings_set(int argc, char** argv) {
    char path[SETTINGS_CLI_PATH], why[256];
    const IniOpt* o;
    int status = snx_command_help(argc, argv, 3, "settings set", USAGE_SET);
    Args a;
    if (status != -1) return status;
    parse_args(argc, argv, 3, &a);
    if (a.extra) {
        snx_command_error("settings set: %s: unexpected argument", a.extra);
        return SNX_EXIT_USAGE;
    }
    if (a.count != 2) {
        snx_command_error("settings set: name one key, one value, and --game=PATH");
        return SNX_EXIT_USAGE;
    }
    if (!a.game) {
        snx_command_error("settings set: --game=PATH names the game's program");
        return SNX_EXIT_USAGE;
    }
    o = snx_setting_row(a.word[0]);
    if (!o) return unknown_key("settings set", a.word[0]);
    if (ini_check(o, a.word[1], why, sizeof why)) {
        snx_command_error("settings set: %s", why);
        return SNX_EXIT_SETTING;
    }
    if (settings_file(a.game, path, sizeof path)) {
        snx_command_error("settings set: %s: not a game's program: check --game", a.game);
        return SNX_EXIT_GAME_FILES;
    }
    status = snx_settings_load(path, why, sizeof why);
    if (status == 0) {
        snx_command_error("settings set: no %s: run recompile first, or check --game=PATH", path);
        return SNX_EXIT_GAME_FILES;
    }
    if (status < 0) {
        snx_command_error("settings set: read %s: %s", path, why);
        return SNX_EXIT_INTERNAL;
    }
    status = snx_settings_save(a.word[0], a.word[1], why, sizeof why);
    if (status) {
        snx_command_error("settings set: %s", why);
        return status < 0 ? SNX_EXIT_INTERNAL : SNX_EXIT_SETTING;
    }
    return SNX_EXIT_OK;
}

int snx_settings_check(int argc, char** argv) {
    char why[256];
    const IniOpt* o;
    int status = snx_command_help(argc, argv, 3, "settings check", USAGE_CHECK);
    Args a;
    if (status != -1) return status;
    parse_args(argc, argv, 3, &a);
    if (a.extra) {
        snx_command_error("settings check: %s: unexpected argument", a.extra);
        return SNX_EXIT_USAGE;
    }
    if (a.game) {
        snx_command_error("settings check: --game is not an argument of this command, which reads no file");
        return SNX_EXIT_USAGE;
    }
    if (a.count != 2) {
        snx_command_error("settings check: name one key and one value");
        return SNX_EXIT_USAGE;
    }
    o = definition_row(a.word[0]);
    if (!o) {
        snx_command_error("settings check: %s: not a setting of the settings definition", a.word[0]);
        return SNX_EXIT_USAGE;
    }
    if (ini_check(o, a.word[1], why, sizeof why)) {
        snx_command_error("settings check: %s", why);
        return SNX_EXIT_SETTING;
    }
    return SNX_EXIT_OK;
}
