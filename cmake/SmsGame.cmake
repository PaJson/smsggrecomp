include_guard(GLOBAL)

# Windows: the resource compiler embeds an optional program icon (see sms_add_game). enable_language
# must run at file scope; this file is included at the top level of each game's CMakeLists.txt.
if(WIN32)
    include(CheckLanguage)
    check_language(RC)
    if(CMAKE_RC_COMPILER)
        enable_language(RC)
    endif()
endif()

set(SMSGG_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

option(SMSGG_REGENERATE "Regenerate native code from the verified ROM" ON)
option(SMSGG_ENABLE_SDL "Build the interactive SDL2 host" ON)
option(SMSGG_ENABLE_GPU "Enable faithful GPU internal resolution" ON)
option(SMSGG_NATIVE_EXECUTION "Use qualified native CPU and PSG jobs" ON)
option(SMSGG_NATIVE_SPANS "Fuse straight-line instruction spans in native builds" ON)
option(SMSGG_REFERENCE_VDP "Use the scalar reference VDP" OFF)
set(SMSGG_OPTIMIZATION "O2" CACHE STRING "Native compiler optimization")
set_property(CACHE SMSGG_OPTIMIZATION PROPERTY STRINGS O1 O2 O3)
if(NOT SMSGG_OPTIMIZATION MATCHES "^O[123]$")
    message(FATAL_ERROR "SMSGG_OPTIMIZATION must be O1, O2 or O3")
endif()

# The runner = every source of a game exe except the generated ones. One list for sms_add_game and for
# sms_add_runner_library (a front end's release kit: the runner compiled once, linked with a game's
# generated C on the player's PC).
function(sms_runner_sources OUT)
    set(_engine "${SMSGG_ENGINE_DIR}")
    set(_s "${_engine}/runner/main.c" "${_engine}/runner/glue.c"
        "${_engine}/runner/coverage_route.c" "${_engine}/runner/hud.c" "${_engine}/runner/art.c" "${_engine}/runner/safe_file.c"
        "${_engine}/runner/video/sms_vdp.c" "${_engine}/runner/audio/sn76489.c" "${_engine}/runner/audio/ym2413.c" "${_engine}/runner/audio/emu2413.c"
        "${_engine}/runner/external/superzazu/z80.c")
    if(SMSGG_ENABLE_SDL)
        list(APPEND _s "${_engine}/runner/host_sdl.c")
        if(SMSGG_ENABLE_GPU AND NOT SMSGG_REFERENCE_VDP)
            list(APPEND _s "${_engine}/runner/host_gl.c")
        endif()
    endif()
    set(${OUT} ${_s} PARENT_SCOPE)
endfunction()

# Compile definitions / options / include dirs / SDL link of runner code (a game exe, the runner library,
# or code linked with it, e.g. a front end's per-game hooks). Must be the same for all of them.
function(sms_runner_settings TARGET)
    set(_engine "${SMSGG_ENGINE_DIR}")
    target_compile_features(${TARGET} PRIVATE c_std_11)
    target_compile_definitions(${TARGET} PRIVATE SMSGG_HAVE_GAME_LAYOUT)
    target_compile_options(${TARGET} PRIVATE "$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-${SMSGG_OPTIMIZATION}>")
    if(SMSGG_NATIVE_EXECUTION)
        target_compile_definitions(${TARGET} PRIVATE SMS_NATIVE_HOOKS SMS_NATIVE_DISPATCH SMS_NATIVE_EVENTS)
        if(SMSGG_NATIVE_SPANS)
            target_compile_definitions(${TARGET} PRIVATE SMS_NATIVE_SPANS)
        endif()
    else()
        target_compile_definitions(${TARGET} PRIVATE SMS_PSG_REFERENCE)
    endif()
    if(SMSGG_REFERENCE_VDP)
        target_compile_definitions(${TARGET} PRIVATE SMS_VDP_REFERENCE)
    endif()
    target_include_directories(${TARGET} PRIVATE "${_engine}/runner" "${_engine}/runner/include")
    if(SMSGG_ENABLE_SDL)
        find_package(SDL2 CONFIG REQUIRED)
        target_compile_definitions(${TARGET} PRIVATE SMS_HAVE_SDL)
        if(SMSGG_ENABLE_GPU AND NOT SMSGG_REFERENCE_VDP)
            target_compile_definitions(${TARGET} PRIVATE SMS_GPU)
        endif()
        target_link_libraries(${TARGET} PRIVATE SDL2::SDL2)
    endif()
endfunction()

# The runner as a static library (no generated code): a game exe = <prefix>_full/_dispatch/_layout.c
# compiled with the same definitions + this library (+ SDL2). Used by release kits.
function(sms_add_runner_library TARGET)
    sms_runner_sources(_runner)
    add_library(${TARGET} STATIC ${_runner})
    sms_runner_settings(${TARGET})
endfunction()

# A generic runner without any game: every ROM runs in the interpreter (runner/interp_only.c; main.c
# forces --interp). Nothing ROM-derived is compiled in, so it may be shipped; for --title-shot before a
# game is built. Headless only (no SDL: no DLL needed).
function(sms_add_interp TARGET)
    set(_engine "${SMSGG_ENGINE_DIR}")
    add_executable(${TARGET} "${_engine}/runner/main.c" "${_engine}/runner/glue.c"
        "${_engine}/runner/coverage_route.c" "${_engine}/runner/hud.c" "${_engine}/runner/art.c" "${_engine}/runner/safe_file.c"
        "${_engine}/runner/video/sms_vdp.c" "${_engine}/runner/audio/sn76489.c" "${_engine}/runner/audio/ym2413.c" "${_engine}/runner/audio/emu2413.c"
        "${_engine}/runner/external/superzazu/z80.c" "${_engine}/runner/interp_only.c")
    target_compile_features(${TARGET} PRIVATE c_std_11)
    target_compile_definitions(${TARGET} PRIVATE SMS_PSG_REFERENCE)
    target_compile_options(${TARGET} PRIVATE "$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-O2>")
    target_include_directories(${TARGET} PRIVATE "${_engine}/runner" "${_engine}/runner/include")
endfunction()

# The game owns game.toml and generated artifacts; this framework owns every
# build input and generation step shared by SMS/GG hosts.
function(sms_add_game TARGET)
    set(_engine "${SMSGG_ENGINE_DIR}")
    set(_config "${CMAKE_CURRENT_SOURCE_DIR}/game.toml")
    file(READ "${_config}" _text)
    string(REGEX MATCH "output_prefix[ \t]*=[ \t]*\"([A-Za-z_][A-Za-z_0-9]*)\"" _match "${_text}")
    set(_prefix "${CMAKE_MATCH_1}")
    if(NOT _prefix)
        message(FATAL_ERROR "game.toml requires a C-identifier output_prefix")
    endif()
    string(REGEX MATCH "rom[ \t]*=[ \t]*\"([^\"]+)\"" _match "${_text}")
    set(SMSGG_ROM "${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_MATCH_1}" CACHE FILEPATH "Owned cartridge ROM")
    set(_gen "${CMAKE_CURRENT_SOURCE_DIR}/generated")
    set(_sources "${_gen}/${_prefix}_full.c" "${_gen}/${_prefix}_dispatch.c" "${_gen}/${_prefix}_layout.c")
    if(SMSGG_REGENERATE)
        if(NOT EXISTS "${SMSGG_ROM}")
            message(FATAL_ERROR "Set SMSGG_ROM to your supported extracted cartridge")
        endif()
        add_subdirectory("${_engine}/recompiler" "${CMAKE_CURRENT_BINARY_DIR}/sms-recompiler")
        set(_deps "${SMSGG_ROM}" "${_config}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dispatch_manifest.txt")
            list(APPEND _deps "${CMAKE_CURRENT_SOURCE_DIR}/dispatch_manifest.txt")
        endif()
        add_custom_command(OUTPUT ${_sources}
            BYPRODUCTS "${_gen}/${_prefix}_funcs.h"
            COMMAND "$<TARGET_FILE:SmsRecomp>" "${SMSGG_ROM}" --game "${_config}"
            DEPENDS SmsRecomp ${_deps}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
    else()
        foreach(_source IN LISTS _sources)
            if(NOT EXISTS "${_source}")
                message(FATAL_ERROR "Missing generated source ${_source}; enable SMSGG_REGENERATE")
            endif()
        endforeach()
    endif()
    sms_runner_sources(_runner)
    add_executable(${TARGET} ${_sources} ${_runner})
    sms_runner_settings(${TARGET})
    target_include_directories(${TARGET} PRIVATE "${_gen}")
    # Optional program icon (Windows): SMSGG_ICON, else Images/Icon.ico in the game folder or up to
    # 4 parent folders (the same Images folder the runner's art lookup uses, runner/art.h).
    if(WIN32 AND CMAKE_RC_COMPILER)
        set(_icon "${SMSGG_ICON}")
        if(NOT _icon)
            set(_dir "${CMAKE_CURRENT_SOURCE_DIR}")
            foreach(_up RANGE 4)
                if(EXISTS "${_dir}/Images/Icon.ico")
                    set(_icon "${_dir}/Images/Icon.ico")
                    break()
                endif()
                get_filename_component(_dir "${_dir}" DIRECTORY)
            endforeach()
        endif()
        if(_icon AND EXISTS "${_icon}")
            set(_rc "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
            file(CONFIGURE OUTPUT "${_rc}" CONTENT "1 ICON \"${_icon}\"\n")
            set_source_files_properties("${_rc}" PROPERTIES OBJECT_DEPENDS "${_icon}")
            target_sources(${TARGET} PRIVATE "${_rc}")
        endif()
    endif()
    if(SMSGG_ENABLE_SDL)
        if(WIN32)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:${TARGET}>"
                VERBATIM)
        endif()
    endif()
endfunction()
