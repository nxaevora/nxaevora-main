# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# The subcommand dispatch (runtime/command.c): recompile, run and settings list, the old flags as aliases, a bad
# argument as exit 2 (SNX_EXIT_USAGE, exit_codes.h) with the bad word on stderr. Run by ctest as runtime_cli_commands
set(dir "${CMAKE_CURRENT_BINARY_DIR}/cli-commands")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")
set(env "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY SDL_VIDEO_DRIVER=dummy)
function(engine prefix)
    execute_process(COMMAND ${env} "${RUNTIME}" ${ARGN}
                    WORKING_DIRECTORY "${dir}" TIMEOUT 20 RESULT_VARIABLE code
                    OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set("${prefix}_code" "${code}" PARENT_SCOPE)
    set("${prefix}_out" "${out}" PARENT_SCOPE)
    set("${prefix}_err" "${err}" PARENT_SCOPE)
endfunction()
# settings list gives the stdout and the exit of --settings, and its recompile scope those of --settings=recompile
engine(old --settings)
engine(new settings list)
if(NOT new_code EQUAL 0 OR NOT "${new_err}" STREQUAL "" OR NOT "${new_out}" STREQUAL "${old_out}")
    message(FATAL_ERROR "settings list is not --settings: exit ${new_code}, stderr \"${new_err}\"")
endif()
engine(game_scope settings list --scope=game)
if(NOT game_scope_code EQUAL 0 OR NOT "${game_scope_out}" STREQUAL "${old_out}")
    message(FATAL_ERROR "settings list --scope=game is not --settings: exit ${game_scope_code}, stderr \"${game_scope_err}\"")
endif()
engine(old_scope --settings=recompile)
engine(new_scope settings list --scope=recompile)
if(NOT new_scope_code EQUAL 0 OR NOT "${new_scope_err}" STREQUAL ""
   OR NOT "${new_scope_out}" STREQUAL "${old_scope_out}")
    message(FATAL_ERROR "settings list --scope=recompile is not --settings=recompile: exit ${new_scope_code}, stderr \"${new_scope_err}\"")
endif()
# a bad word is exit 2, and stderr has a part of that refusal: the bad word, or, where the word is the message's start,
# the line's own words (a wrong line then fails here)
foreach(case "settings list extra|extra" "settings list --scope=bogusscope|bogusscope" "settings|name a command"
             "settings bogus|bogus" "logs|not available in this build" "cache|not available in this build"
             "run|name a game program" "run -x|-x")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 args)
    list(GET fields 1 part)
    separate_arguments(args_list UNIX_COMMAND "${args}")
    engine(bad ${args_list})
    string(FIND "${bad_err}" "${part}" at)
    if(NOT bad_code EQUAL 2 OR at LESS 0)
        message(FATAL_ERROR "nxaevora-engine ${args} ended with ${bad_code}, not 2, or stderr has no \"${part}\":\n${bad_err}")
    endif()
endforeach()
# a missing input is the game's files (exit_codes.h: SNX_EXIT_GAME_FILES 3), recompile and its alias -r alike
engine(missing_new recompile missing.nro)
engine(missing_old -r missing.nro)
if(NOT missing_new_code EQUAL 3 OR NOT missing_old_code EQUAL 3)
    message(FATAL_ERROR "recompile missing.nro ended with ${missing_new_code} and -r with ${missing_old_code}, not 3")
endif()
# run PROGRAM is the play mode: a missing program is the game's files (exit_codes.h: SNX_EXIT_GAME_FILES 3), as the
# old form of the play mode
engine(run_missing run missing.nro)
engine(play_missing missing.nro)
if(NOT run_missing_code EQUAL 3 OR NOT play_missing_code EQUAL 3)
    message(FATAL_ERROR "run missing.nro ended with ${run_missing_code} and missing.nro with ${play_missing_code}, not 3")
endif()
# a bad --KEY=VALUE is refused as the recompile path refuses it: exit 2 and the same line, before anything runs
engine(value_new recompile missing.nro --aot_threads=abc)
engine(value_old -r missing.nro --aot_threads=abc)
if(NOT value_new_code EQUAL 2 OR NOT value_old_code EQUAL 2
   OR NOT "${value_new_err}" STREQUAL "${value_old_err}")
    message(FATAL_ERROR "recompile --aot_threads=abc ended with ${value_new_code} (${value_new_err}) and -r with ${value_old_code} (${value_old_err})")
endif()
# the help of a command is exit 0 with its usage on stdout; --help names the three commands
engine(run_help run --help)
string(FIND "${run_help_out}" "run" at_run)
if(NOT run_help_code EQUAL 0 OR at_run LESS 0)
    message(FATAL_ERROR "run --help ended with ${run_help_code}:\n${run_help_out}")
endif()
engine(settings_help settings list --help)
string(FIND "${settings_help_out}" "settings" at_settings)
if(NOT settings_help_code EQUAL 0 OR at_settings LESS 0)
    message(FATAL_ERROR "settings list --help ended with ${settings_help_code}:\n${settings_help_out}")
endif()
engine(global_help --help)
foreach(word recompile run settings)
    string(FIND "${global_help_out}" "${word}" at_global)
    if(NOT global_help_code EQUAL 0 OR at_global LESS 0)
        message(FATAL_ERROR "--help ended with ${global_help_code} or does not name ${word}:\n${global_help_out}")
    endif()
endforeach()
file(REMOVE_RECURSE "${dir}")
