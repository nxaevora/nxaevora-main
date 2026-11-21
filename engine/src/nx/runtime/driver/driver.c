/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/runtime/driver/driver.h"
#include "common/utilities/common.h"
#include "common/utilities/demangle.h"
#include "common/utilities/exit_codes.h"
#include "common/utilities/logscan.h"
#include "nx/runtime/driver/game_name.h"
#include "nx/runtime/driver/recompile.h"
#include "nx/runtime/driver/setup.h"
#include "common/utilities/mods.h"
#include "common/utilities/setup.h"
#include "nx/runtime/cli.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifndef SNX_SOURCE_DIR
#define SNX_SOURCE_DIR "."
#endif
#ifndef SNX_BUILD_DIR
#define SNX_BUILD_DIR "build"
#endif
/* a game's program file: <name>.exe on Windows, <name> elsewhere */
#ifdef _WIN32
#define PROGRAM_SUFFIX ".exe"
#else
#define PROGRAM_SUFFIX ""
#endif

static void usage(void) {
    puts("Usage: nxaevora-engine <command> [OPTIONS]\n"
         "  recompile INPUT        Recompile a decrypted game folder or an NRO, without playing\n"
         "  run PROGRAM            Run a recompiled game (its arguments go after --)\n"
         "  settings list          List the settings, tab-separated (--scope=game|recompile)\n"
         "  settings schema        Print the settings definition as one JSON document\n"
         "  settings get KEY --game=PROGRAM\n"
         "                         Print one value of the game's .ini, or its default\n"
         "  settings set KEY VALUE --game=PROGRAM\n"
         "                         Write one value of the game's .ini\n"
         "  settings check KEY VALUE\n"
         "                         Check one value against the settings definition, with no file");
    puts("The old options are aliases:\n"
         "  INPUT                  A game made already, or an NRO; a decrypted game folder with -r\n"
         "  -r, --recompile        Recompile a decrypted game folder or an NRO, without playing\n"
         "  -H, --headless         Run without a window or swapchain");
    puts("  --tracy                Start the Tracy client for a GUI connection (SNX_TRACY builds)\n"
         "  --tracy-capture        Record the run with tracy-capture, from Tracy 0.14.1 on PATH (SNX_TRACY builds)");
    puts(
        "  -f, --fullscreen       Start fullscreen for this run (--fullscreen=0: windowed); else the game's .ini decides\n"
        "  --touch=0|1|2          The game's touch screen: off, fingers, or fingers and the mouse\n"
        "  --controls=0|1         The on-screen controls: hidden or shown\n"
        "  --KEY=VALUE            Any setting of the game's .ini, for this run\n"
        "  --settings[=recompile] List the settings, tab-separated");
    puts("  -r -h                  Recompilation options\n"
         "  --game=DIR             Play the recompiled game in DIR (recompiled/<title ID>)");
    puts("  -O LEVEL               The recompile's optimization: 0, 1, 2, 3, s or z (default: 2); a game made\n"
         "                         already keeps its code until -r");
    puts("  --outdir DIR           Where the game's folder goes (default: recompiled/)\n"
         "  --data DIR             The data folder: recompiled/ and work/ (default: a\n"
         "                         .nxaevora folder beside the AppImage or the program,\n"
         "                         else the user's data folder,\n"
#ifdef _WIN32
         "                         %APPDATA%\\nxaevora)");
#else
         "                         ~/.local/share/nxaevora)");
#endif
    puts("  --setup                Go through the setup in the terminal: the language, the install, the\n"
         "                         folders and the first user (asked by itself the first time a terminal\n"
         "                         runs a game, or after an update says what changed)\n"
         "  --no-setup             Leave the setup out of this run, writing nothing");
    puts("  --build-dir DIR        Override this build's build directory\n"
         "  --dump-modules PROGRAM DIR\n"
         "                         Write an extracted program's (or an NRO's) modules as laid out\n"
         "                         in guest memory into DIR, for tools/symbols.py and tools/disasm.py");
    puts("  --scan-log LOG... [--format json|markdown] [--name NAME]\n"
         "                         Summarize a game's log or the launcher's Share Log file: the build,\n"
         "                         the game, how the run ended and where, unimplemented services");
#ifndef _WIN32
    puts("  -S                     Enable the native GDB stub on localhost:1234\n"
         "  -s                     Enable the stub and stop at startup\n"
         "Short options may be combined, for example -rH or -Hs.");
#endif
}
static int is_nro_path(const char* path) {
    const char* ext = strrchr(path, '.');
    return ext && strlen(ext) == 4 && tolower((unsigned char)ext[1]) == 'n' && tolower((unsigned char)ext[2]) == 'r' &&
           tolower((unsigned char)ext[3]) == 'o';
}
/* 1 when the files at a and b hold the same bytes */
static int same_bytes(const char* a, const char* b) {
    FILE *x = snx_fopen(a, "rb"), *y = snx_fopen(b, "rb");
    unsigned char p[16384], q[16384];
    size_t n;
    int same = x && y;
    while (same && (n = fread(p, 1, sizeof p, x)) > 0) same = fread(q, 1, n, y) == n && !memcmp(p, q, n);
    same = same && !ferror(x) && fread(q, 1, 1, y) == 0 && !ferror(y);
    if (x) fclose(x);
    if (y) fclose(y);
    return same;
}
/* A game made by another build of this ABI runs the code it was recompiled into (stamped), with its folder's copy of
   that build's runtime: replaced by this build's when the two differ, so its fixes reach the games made before it. 0,
   or nonzero with a line saying why */
static int refresh_runtime(const SnxPaths* paths, const char* game) {
    char* run = snx_own_runtime(paths);
    int rc = 0, same;
    /* a build without the runtime deploys one it builds, not itself: there is nothing here to hold the copy to */
    if (!run) return 0;
    same = same_bytes(run, game);
    /* the library beside the runtime is deployed with it (snx_deploy_runtime) */
    if (!same) {
        rc = snx_deploy_runtime(run, game);
        if (!rc && !same) printf("%s ran another build's runtime: it runs %s now\n", game, run);
    }
    free(run);
    return rc;
}
/* whether this build made the game at base (<folder>/<name>, a game's or its update's), with the mods its game plays
   now (mods.h's snx_game_stamp_is) */
static int stamped(const char* base) {
    char *made = snx_game_mods_stamp(base), *expected = snx_game_stamp(made);
    int same = snx_game_stamp_is(base, expected);
    free(expected);
    free(made);
    return same;
}
/* a game the recompile made, named by its folder (recompiled/<id>) or its program there (recompiled/<id>/<id>, .exe on
   Windows): 1 with its folder and name, owned when asked for. A game an older version laid out beside the others is not
   one */
static int prepared_game(const char* input, char** folder, char** name) {
    char *path = snx_absolute(input), *dir = NULL, *own = NULL, program[SNX_PATH_SIZE], data[SNX_PATH_SIZE];
    const char* leaf;
    size_t n;
    int found = 0;
    if (!path) return 0;
    n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) path[--n] = 0;
    dir = snx_is_dir(path) ? snx_strdup(path) : snx_is_file(path) ? snx_dirname(path) : NULL;
    for (leaf = path + n; leaf > path && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
    own = snx_strdup(leaf);
#ifdef _WIN32
    n = strlen(own);
    if (n > 4 && own[n - 4] == '.' && tolower((unsigned char)own[n - 3]) == 'e' &&
        tolower((unsigned char)own[n - 2]) == 'x' && tolower((unsigned char)own[n - 1]) == 'e' && !snx_is_dir(path))
        own[n - 4] = 0;
#endif
    if (dir) {
        for (leaf = dir + strlen(dir); leaf > dir && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
        found = *own && !strcmp(leaf, own) &&
                !snx_path_format(program, sizeof program, "%s/%s" PROGRAM_SUFFIX, dir, own) && snx_is_file(program) &&
                !snx_path_format(data, sizeof data, "%s/%s_data", dir, own) && snx_is_dir(data);
    }
    if (found && folder) {
        *folder = dir;
        dir = NULL;
    }
    if (found && name) {
        *name = own;
        own = NULL;
    }
    free(path);
    free(dir);
    free(own);
    return found;
}
/* an add-on the recompile made, named by its program in its game's folder (recompiled/<id>/<id>_<add-on's id>, .exe on
   Windows): 1 with that folder, its name and its title ID, owned */
static int prepared_addon(const char* input, char** folder, char** name, SnxU64* addon) {
    char *path = snx_absolute(input), *dir, *own;
    const char* leaf;
    size_t n;
    int found = 0;
    if (!path || !snx_is_file(path)) {
        free(path);
        return 0;
    }
    dir = snx_dirname(path);
    for (leaf = path + strlen(path); leaf > path && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
    own = snx_strdup(leaf);
    n = strlen(own);
#ifdef _WIN32
    if (n > 4 && own[n - 4] == '.' && tolower((unsigned char)own[n - 3]) == 'e' &&
        tolower((unsigned char)own[n - 2]) == 'x' && tolower((unsigned char)own[n - 1]) == 'e')
        own[n - 4] = 0;
#endif
    (void)n;
    for (leaf = dir + strlen(dir); leaf > dir && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
    found = snx_addon_name(leaf, own, addon);
    if (found) {
        *folder = dir;
        *name = own;
    } else {
        free(dir);
        free(own);
    }
    free(path);
    return found;
}
/* a game an older version laid out beside the others (games/<name>, games/<name>_data/), which runs where it is but is
   neither played nor recompiled by the driver until it has a folder of its own */
static int left_beside(const char* input) {
    size_t n = strlen(input);
    char* data;
    int found;
#ifdef _WIN32
    if (n > 4 && input[n - 4] == '.' && tolower((unsigned char)input[n - 3]) == 'e' &&
        tolower((unsigned char)input[n - 2]) == 'x' && tolower((unsigned char)input[n - 1]) == 'e')
        n -= 4;
#endif
    data = snx_format("%.*s_data", (int)n, input);
    found = snx_is_file(input) && snx_is_dir(data) && !prepared_game(input, NULL, NULL);
    free(data);
    if (found) { /* an add-on beside its game, in the game's folder */
        char *folder, *name;
        SnxU64 addon;
        if (prepared_addon(input, &folder, &name, &addon)) {
            free(folder);
            free(name);
            found = 0;
        }
    }
    return found;
}
/* game name's folder: in the folder outdir names, else the data folder's recompiled/ (games/ of an older version while
   the game is there, snx_recompiled_game) */
static char* game_folder(const char* outdir, const char* repo, const char* name) {
    char *games, *folder;
    if (!outdir) return snx_recompiled_game(repo, name);
    games = snx_absolute(outdir);
    folder = games ? snx_path_join(games, name) : NULL;
    free(games);
    return folder;
}
/* whether the recompile of the game from source is given name, its folder's: only when it would name it otherwise by
   itself (snx_game_name), or cannot read its NPDM to, as a game named by its title ID is then shown by the name in
   its control.nacp, not by the ID */
static int named_otherwise(const char* source, const char* name) {
    SnxNpdm npdm;
    char error[256], *own;
    int otherwise;
    if (snx_load_npdm(source, &npdm, error, sizeof error)) return 1;
    own = snx_game_name(NULL, &npdm, source);
    otherwise = !own || strcmp(own, name) != 0;
    free(own);
    return otherwise;
}
/* The game the driver plays: a game made already (recompiled/<id>/<id>) or an NRO, which is recompiled first. A
   decrypted game folder is recompiled with -r. An update's program made from a dump is refused. 0, or nonzero with the
   line that says why */
static int play_dump(const char* input, const SnxPaths* paths, unsigned flags, const char* outdir, const char* opt,
                     int recompiling, char** recompile, int settings, char** setting, int game_argc, char** game_argv) {
    char *name = NULL, *games = NULL, *game = NULL, *folder = NULL, *base = NULL, *code = NULL, *source = NULL;
    char error[256];
    SnxU64 addon = 0;
    const char** command =
        (const char**)snx_alloc((size_t)(game_argc + settings + recompiling + 24) * sizeof(*command));
    int rc = SNX_EXIT_INTERNAL, nro = is_nro_path(input), prepared = prepared_game(input, &folder, &name), named = 0;
    int n, j;
    SnxNpdm npdm;
    command[0] = paths->executable;
    command[1] = "--data";
    command[2] = paths->repo;
    command[3] = "--build-dir";
    command[4] = paths->build;
    if (prepared) {
        if (outdir) {
            snx_error("--outdir cannot go with %s: it is a game made already", input);
            rc = SNX_EXIT_USAGE;
            goto done;
        }
        games = snx_dirname(folder);
        base = snx_path_join(folder, name);
        /* made from what its _data keeps: the ExeFS or the NRO, the RomFS, control.nacp */
        source = snx_format("%s_data/exefs/main.nro", base);
        if (!snx_is_file(source)) {
            char* exefs = snx_format("%s_data/exefs", base);
            free(source);
            source = snx_is_dir(exefs) ? NULL : snx_game_source(folder, name, NULL);
            if (source && !snx_is_file(source)) {
                free(source);
                source = NULL;
            }
            if (!source) source = snx_format("%s_data", base);
            free(exefs);
        }
        named = named_otherwise(source, name); /* an NRO's, one named when it was made */
        goto made;
    }
    /* an update's program made from a dump, which this build cannot read */
    if (prepared_addon(input, &folder, &name, &addon)) {
        snx_error("%s is an update's program made from a dump: recompile the game from its decrypted folder with -r",
                  input);
        rc = SNX_EXIT_GAME_FILES;
        goto done;
    }
    if (snx_refuse_encrypted_dump(input)) {
        rc = SNX_EXIT_GAME_FILES;
        goto done;
    }
    if (!nro || !snx_is_file(input)) {
        snx_error("%s is not a file", input);
        rc = SNX_EXIT_GAME_FILES;
        goto done;
    }
    games = outdir ? snx_absolute(outdir) : snx_recompiled_dir(paths->repo);
    if (snx_load_npdm(input, &npdm, error, sizeof error)) {
        snx_error("%s", error);
        rc = SNX_EXIT_GAME_FILES;
        goto done;
    }
    name = snx_game_name(NULL, &npdm, input);
    folder = game_folder(outdir, paths->repo, name); /* the game's own, as the recompile makes it */
    free(games);
    games = folder ? snx_dirname(folder) : NULL;
    base = snx_path_join(folder, name);
    source = snx_strdup(input);
made:
    if (!games || !folder || !base || !source) {
        rc = snx_error("cannot resolve the game's folder for %s", input);
        goto done;
    }
#ifdef _WIN32
    code = snx_format("%s_code.dll", base);
    game = snx_format("%s.exe", base);
#else
    code = snx_format("%s_code.so", base);
    game = snx_strdup(base);
#endif
    {
        /* Always restage an NRO: a same-named cached game may belong to another input or an older build. */
        if (nro || (flags & SNX_RECOMP) || !snx_is_file(game) || !snx_is_file(code) || !stamped(base)) {
            command[5] = "-r";
            command[6] = "--outdir";
            command[7] = games;
            n = 8;
            if (opt) {
                command[n++] = "-O";
                command[n++] = opt;
            }
            for (j = 0; j < recompiling; ++j) command[n++] = recompile[j];
            command[n++] = "--";
            command[n++] = source;
            if (named) command[n++] = name;
            command[n] = NULL;
            rc = snx_run_stage(command, NULL, "Recompilation");
            if (rc) goto done;
            if (!snx_is_file(game)) {
                snx_error("recompilation finished without creating %s", game);
                rc = SNX_EXIT_RECOMPILE;
                goto done;
            }
        }
    }
    /* the game is made: the recompile ran, or its stamp is this build's */
    rc = SNX_EXIT_OK;
    if (flags & SNX_RECOMP) goto done;
    /* the game may start with another environment than this run's (its copy outside an AppImage, a shortcut) */
    if (snx_record_folders(folder, name)) {
        rc = SNX_EXIT_INTERNAL;
        goto done;
    }
    rc = refresh_runtime(paths, game);
    if (rc) goto done;
    n = 0;
    command[n++] = game;
    if (flags & SNX_HEADLESS) command[n++] = "-H";
    if (flags & SNX_FULLSCREEN) command[n++] = "-f";
    if (flags & SNX_WINDOWED) command[n++] = "--fullscreen=0";
    for (j = 0; j < settings; ++j) command[n++] = setting[j];
    if (flags & SNX_STOP)
        command[n++] = "-s";
    else if (flags & SNX_GDB)
        command[n++] = "-S";
    if (flags & SNX_TRACY_CLIENT) command[n++] = "--tracy";
    if (flags & SNX_TRACY_CAPTURE) command[n++] = "--tracy-capture";
    /* Keep the boundary so the deployed launcher cannot interpret game arguments as another request. */
    if (game_argc) command[n++] = "--";
    for (j = 0; j < game_argc; ++j) command[n++] = game_argv[j];
    command[n] = NULL;
    rc = (flags & SNX_GDB) ? snx_run_debug_stage(command, folder, "Game") : snx_run_stage(command, folder, "Game");
done:
    free(name);
    free(games);
    free(game);
    free(folder);
    free(base);
    free(code);
    free(source);
    free(command);
    return rc;
}
/* 1 for an update's or a DLC's program in its game's folder (prepared_addon) */
static int addon_program(const char* input) {
    char *folder, *name;
    SnxU64 addon;
    if (!prepared_addon(input, &folder, &name, &addon)) return 0;
    free(folder);
    free(name);
    return 1;
}
/* Options whose following token belongs to a tool, not to the launcher. */
static int takes_value(const char* arg) {
    static const char* const names[] = {"--name", "--outname", "--outdir", "--runtime", "--target",
                                        "-o",     "-O",        "--jobs",   "--chunk"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(arg, names[i])) return 1;
    return 0;
}
/* key=value's key, a setting the runtime knows */
static int setting_known(const char* option) {
    char key[64];
    size_t n = (size_t)(strchr(option, '=') - option);
    if (!snx_setting_known || n >= sizeof key) return 0;
    memcpy(key, option, n);
    key[n] = 0;
    return snx_setting_known(key);
}
/* The exit code of a failed dump: a program that cannot be loaded is the game's files, a failed write or a failure of
   the host is inside the runtime */
int dump_exit_code(SnxDumpResult result) {
    return result == SNX_DUMP_INPUT ? SNX_EXIT_GAME_FILES : SNX_EXIT_INTERNAL;
}
/* --dump-modules PROGRAM DIR: each module's image and symbols, at the bases the runtime gives them (loader.h's
   snx_dump_program has the format). Tools read game code through it */
static int dump_modules(int argc, char** argv) {
    char error[1024];
    SnxDumpResult result;
    if (argc != 4) {
        snx_error("--dump-modules takes a program (an extracted game or an NRO) and a folder");
        return SNX_EXIT_USAGE;
    }
    result = snx_dump_program(argv[2], argv[3], error, sizeof error);
    if (result != SNX_DUMP_OK) {
        snx_error("%s", error);
        return dump_exit_code(result);
    }
    return SNX_EXIT_OK;
}
/* the setup in the terminal for the program at argv0: all of it, or what setup.ini has left. 0 once done */
static int setup_run(const char* argv0, int all) {
    char *self = snx_executable(argv0), *dir = self ? snx_dirname(self) : NULL;
    int rc;
    snx_tool_console();
    rc = snx_setup_terminal(stdin, stdout, dir, all);
    free(dir);
    free(self);
    return rc;
}
int snx_driver(int argc, char** argv) {
    SnxPaths paths;
    const char *repo = NULL, *build = SNX_BUILD_DIR, *input = NULL, *outdir = NULL, *opt = NULL;
    char *r = NULL, *b = NULL, *exe = NULL, **rest = NULL, **setting, **recompile, *self, *data = NULL, *source = NULL;
    unsigned flags = 0;
    int i, n = 0, separator = argc, result = -1, tool_paths = 0, help = 0, parsed, automatic;
    int repo_given = 0, settings = 0, recompiling = 0, taken = 0;
    if (argc <= 1 && snx_setup_again) {
        result = setup_run(argv[0], 1); /* scarletnx --setup */
        goto done;
    }
    if (argc <= 1) {
        result = -1;
        goto done;
    }
    if (!strcmp(argv[1], "--dump-modules")) {
        result = dump_modules(argc, argv);
        goto done;
    }
    if (!strcmp(argv[1], "--scan-log")) {
        result = snx_logscan_main(argc - 1, argv + 1, snx_demangle);
        goto done;
    }
    rest = (char**)snx_alloc((size_t)(3 * argc + 1) * sizeof(*rest));
    setting = rest + argc + 1;
    recompile = setting + argc;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) {
            separator = i;
            break;
        }
        if (!strcmp(argv[i], "--repo")) snx_option_renamed("--repo", "--data");
        if (!strcmp(argv[i], "--data") || !strcmp(argv[i], "--repo") || !strcmp(argv[i], "--build-dir")) {
            int is_repo = strcmp(argv[i], "--build-dir") != 0;
            tool_paths = 1;
            if (++i == argc) {
                snx_error("%s requires a folder", argv[i - 1]);
                result = SNX_EXIT_USAGE;
                goto done;
            }
            if (is_repo) {
                repo = argv[i];
                repo_given = 1;
            } else
                build = argv[i];
            continue;
        }
        parsed = snx_cli_flag(argv[i], &flags);
        if (parsed < 0) {
            snx_error("unknown option: %s", argv[i]);
            result = SNX_EXIT_USAGE;
            goto done;
        }
#ifdef _WIN32
        if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "-S")) {
            snx_error("the GDB stub is UNIX only");
            result = SNX_EXIT_USAGE;
            goto done;
        }
#endif
        if (parsed) continue;
        if (!strcmp(argv[i], "--backend") || !strncmp(argv[i], "--backend=", 10)) {
            /* LLVM is the only code generator: older scripts name it */
            const char* backend = argv[i][9] ? argv[i] + 10 : i + 1 < argc ? argv[++i] : "";
            if (strcmp(backend, "llvm")) {
                snx_error("--backend %s: LLVM is the only backend", backend);
                result = SNX_EXIT_USAGE;
                goto done;
            }
            continue;
        }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) help = 1;
        if (!input && argv[i][0] != '-') input = argv[i];
        rest[n++] = argv[i];
        if (takes_value(argv[i]) && i + 1 < argc) rest[n++] = argv[++i];
    }
    if (help && !(flags & SNX_RECOMP)) {
        snx_tool_console();
        usage();
        result = SNX_EXIT_OK;
        goto done;
    }
    /* a game made already is played or, with -r, recompiled where it is */
    automatic = input && (!(flags & SNX_RECOMP) ||
                          ((flags & SNX_RECOMP) && (prepared_game(input, NULL, NULL) || addon_program(input))));
    if (input && (automatic || (flags & SNX_RECOMP)) && left_beside(input)) {
        snx_error("%s was laid out by an older version: move its files as docs/DEPRECATIONS.md says, or run it "
                  "as it is with --game=%s",
                  input, input);
        result = SNX_EXIT_GAME_FILES;
        goto done;
    }
    /* played, a --key=value that is not the driver's own is a setting of the recompile, passed on to it when the game is
       made, or of the game's .ini, passed on to the game when the runtime knows the key; any other is refused */
    for (i = 0; automatic && i < n; ++i)
        if (!strncmp(rest[i], "--", 2) && strchr(rest[i], '=')) {
            char key[64];
            size_t length = (size_t)(strchr(rest[i], '=') - rest[i] - 2);
            if (length < sizeof key) {
                memcpy(key, rest[i] + 2, length);
                key[length] = 0;
            }
            const IniOpt* recompiled = length < sizeof key ? snx_recompile_setting(key) : NULL;
            int plays = setting_known(rest[i] + 2);
            if (!recompiled && !plays) {
                snx_error("unknown option: %s", rest[i]);
                result = SNX_EXIT_USAGE;
                goto done;
            }
            if (recompiled) {
                /* the recompile path checks each value with ini_check (recompile.c): check it here too, before the
                   stamp decides that the game is made and the recompile is skipped */
                char why[256];
                if (ini_check(recompiled, strchr(rest[i], '=') + 1, why, sizeof why)) {
                    snx_error("%s", why);
                    result = SNX_EXIT_USAGE;
                    goto done;
                }
                recompile[recompiling++] = rest[i];
            }
            if (plays) setting[settings++] = rest[i]; /* both for one both take (slp_vectorizer) */
            ++taken;
        } else
            rest[i - taken] = rest[i];
    n -= taken;
    /* played, --outdir names the games' folder, as -r takes it */
    for (i = 0; automatic && i < n; ++i)
        if (!strcmp(rest[i], "--outdir")) {
            if (i + 1 == n) {
                snx_error("%s requires a directory", rest[i]);
                result = SNX_EXIT_USAGE;
                goto done;
            }
            outdir = rest[i + 1];
            memmove(rest + i, rest + i + 2, (size_t)(n - i - 2) * sizeof(*rest));
            n -= 2;
            --i;
        }
    /* played or prepared, -O LEVEL (or -OLEVEL) is the recompile's, for when the game is made */
    for (i = 0; automatic && i < n; ++i)
        if (!strncmp(rest[i], "-O", 2)) {
            int own = rest[i][2] == 0; /* its value the next token */
            if (own && i + 1 == n) {
                snx_error("-O requires a level");
                result = SNX_EXIT_USAGE;
                goto done;
            }
            opt = own ? rest[i + 1] : rest[i] + 2;
            if (strlen(opt) != 1 || !strchr("0123sz", opt[0])) {
                snx_error("-O %s: not 0, 1, 2, 3, s or z", opt);
                result = SNX_EXIT_USAGE;
                goto done;
            }
            memmove(rest + i, rest + i + 1 + own, (size_t)(n - i - 1 - own) * sizeof(*rest));
            n -= 1 + own;
            --i;
        }
    /* -r's AOT build names the profile cache and hooks direct calls; both are the recompiler's, not the game's.
       Left in rest they count as extra dump inputs */
    for (i = 0; automatic && (flags & SNX_RECOMP) && i < n; ++i) {
        if (!strcmp(rest[i], "--jit-cache")) {
            if (i + 1 == n) {
                snx_error("--jit-cache requires a file");
                result = SNX_EXIT_USAGE;
                goto done;
            }
            recompile[recompiling++] = rest[i];
            recompile[recompiling++] = rest[i + 1];
            memmove(rest + i, rest + i + 2, (size_t)(n - i - 2) * sizeof(*rest));
            n -= 2;
            --i;
        } else if (!strcmp(rest[i], "--hook-direct-calls")) {
            recompile[recompiling++] = rest[i];
            memmove(rest + i, rest + i + 1, (size_t)(n - i - 1) * sizeof(*rest));
            --n;
            --i;
        }
    }
    /* played (a game, or nothing), an option nothing here took is refused by its name; -r refuses its own */
    if (!(flags & SNX_RECOMP) || automatic)
        for (i = 0; i < n; ++i) {
            if (takes_value(rest[i])) {
                ++i; /* its value */
                continue;
            }
            /* the runtime's own, which it reads after: the settings' list and the game to play; as a game's copy is
               started, a setting it knows and -v too */
            if (!strcmp(rest[i], "--settings") || !strncmp(rest[i], "--settings=", 11) ||
                !strncmp(rest[i], "--game=", 7))
                continue;
            if (!input && (!strcmp(rest[i], "-v") ||
                           (!strncmp(rest[i], "--", 2) && strchr(rest[i], '=') && setting_known(rest[i] + 2))))
                continue;
            if (rest[i][0] == '-' && rest[i][1] && strcmp(rest[i], "-h") && strcmp(rest[i], "--help") &&
                strcmp(rest[i], "--hook-direct-calls")) {
                snx_error("unknown option: %s (nxaevora-engine --help lists them)", rest[i]);
                result = SNX_EXIT_USAGE;
                goto done;
            }
        }
    if (automatic && (n != 1 || help)) {
        if (help)
            snx_error("-h goes with -r, or alone (nxaevora-engine --help)");
        else
            snx_error("one input at a time, and %s is a second: the game's own arguments go after --", rest[n - 1]);
        result = SNX_EXIT_USAGE;
        goto done;
    }
    if ((flags & SNX_RECOMP) &&
        (flags & (SNX_HEADLESS | SNX_GDB | SNX_TRACY_CLIENT | SNX_TRACY_CAPTURE | SNX_FULLSCREEN | SNX_WINDOWED))) {
        snx_error("%s plays a game, and -r only prepares one", flags & SNX_HEADLESS        ? "--headless"
                                                              : flags & SNX_GDB           ? "-S"
                                                              : flags & SNX_TRACY_CLIENT  ? "--tracy"
                                                              : flags & SNX_TRACY_CAPTURE ? "--tracy-capture"
                                                              : flags & SNX_FULLSCREEN ? "--fullscreen"
                                                                                       : "--fullscreen=0");
        result = SNX_EXIT_USAGE;
        goto done;
    }
    if (!automatic && !(flags & SNX_RECOMP)) {
        if (tool_paths) {
            snx_error("expected a game or -r");
            result = SNX_EXIT_USAGE;
        }
        goto done;
    }
    if (automatic && (flags & SNX_RECOMP) && separator < argc) {
        snx_error("game arguments require game execution");
        result = SNX_EXIT_USAGE;
        goto done;
    }
    /* Explicit tool modes retain their positional -- escape (including names
       beginning with '-'). Automatic play uses it for game arguments instead. */
    if (!automatic)
        for (i = separator; i < argc; ++i) rest[n++] = argv[i];
    rest[n] = NULL;
    /* work/ and recompiled/ are in the data folder (snx_data_dir), unless --repo names one (the menu does), for a
       build tree's driver as for an installed one. Not the current folder: a desktop entry or Explorer start the
       driver wherever they happen to be, often somewhere read-only (/, the AppImage's mount, Program Files) */
    self = snx_executable(argv[0]);
    exe = self ? snx_dirname(self) : NULL;
    free(self);
    source = snx_checkout(SNX_SOURCE_DIR, build, exe);
    if (!repo_given) {
        data = snx_data_dir(exe);
        if (data && snx_mkdirs(data)) {
            snx_error("cannot create the data folder %s", data);
            result = SNX_EXIT_INTERNAL;
            goto done;
        }
        repo = data ? data : ".";
    }
    free(exe);
    exe = NULL;
    snx_tool_console();
    if (data && source) snx_checkout_left(source, data);
    if (data) snx_recompiled_left(data);
    /* what the setup has left, before the first run a terminal makes and the first after an update, or all of it */
    if (!snx_setup_skip && (snx_setup_again || snx_terminal()) && setup_run(argv[0], snx_setup_again)) {
        result = SNX_EXIT_INTERNAL;
        goto done;
    }
    r = snx_absolute(repo);
    b = snx_absolute(build);
    exe = snx_executable(argv[0]);
    if (!r || !b || !exe) {
        snx_error("cannot resolve %s", !r ? repo : !b ? build : argv[0]);
        result = SNX_EXIT_INTERNAL;
        goto done;
    }
    paths.repo = r;
    paths.build = b;
    paths.executable = exe;
    paths.source = source;
    if (automatic)
        result = play_dump(input, &paths, flags, outdir, opt, recompiling, recompile, settings, setting,
                           separator < argc ? argc - separator - 1 : 0,
                           argv + (separator < argc ? separator + 1 : argc));
    else
        result = snx_recompile_game(n, rest, &paths);
done:
    free(rest);
    free(r);
    free(b);
    free(exe);
    free(data);
    free(source);
    return result;
}
