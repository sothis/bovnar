# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CMake toolchain file for cross-compiling the 64-bit MinGW-w64 Windows build
# from a Linux host (mirrors the MinGW leg of .github/workflows/build-and-package.yml,
# which builds natively under MSYS2 UCRT64). A distro mingw-w64 (Debian, Ubuntu)
# targets msvcrt.dll by default. This file selects the Universal CRT instead, so
# the local archive links the same CRT the CI archive does (api-ms-win-crt-*.dll,
# not msvcrt.dll). Use it for a local cross-build:
#
#   cmake -B build/mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake \
#         -DCMAKE_BUILD_TYPE=Release -DBVNR_BUILD_TESTS=OFF .
#   cmake --build build/mingw
#
# Produces libbvnr.a, libbvnr.dll (+ libbvnr.dll.a import lib) and bovnar.exe.
# Tests stay OFF by that command: the harness is POSIX (fork/exec,
# socketpair/pthread, sigaction) and needs Wine to execute the .exe locally.
# The CRT selection below still applies when the tests are built.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

# Search the cross sysroot for libraries/headers, but use host programs (cmake,
# make) as-is.
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Universal CRT, matching MSYS2 UCRT64. Distro GCC 13 (what Debian and Ubuntu
# ship for mingw-w64) has no -mcrtdll=, and its *libgcc spec hard-wires
# -lmsvcrt, so a -lucrt on the link line would import both CRTs. When the
# compiler accepts -mcrtdll=ucrt, that flag replaces the CRT on its own. When
# it does not, rewrite a copy of *libgcc so -lmsvcrt becomes -lucrt and pass
# it with -specs=. CMakeLists.txt applies the result only if this file set
# BVNR_MINGW_UCRT, which the native UCRT64 CI build never does.
set(_bvnr_ucrt_lib "${CMAKE_FIND_ROOT_PATH}/lib/libucrt.a")
if(NOT EXISTS "${_bvnr_ucrt_lib}")
    message(FATAL_ERROR
        "MinGW cross toolchain has no ${_bvnr_ucrt_lib}. "
        "The CI MinGW archive links the Universal CRT; this sysroot cannot.")
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles")
set(_bvnr_crtdll_src "${CMAKE_BINARY_DIR}/CMakeFiles/bvnr-crtdll-probe.c")
set(_bvnr_crtdll_obj "${CMAKE_BINARY_DIR}/CMakeFiles/bvnr-crtdll-probe.o")
file(WRITE "${_bvnr_crtdll_src}" "int main(void){return 0;}\n")
execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -mcrtdll=ucrt -c "${_bvnr_crtdll_src}" -o "${_bvnr_crtdll_obj}"
    RESULT_VARIABLE _bvnr_crtdll_rc
    OUTPUT_QUIET ERROR_QUIET)
file(REMOVE "${_bvnr_crtdll_src}" "${_bvnr_crtdll_obj}")

set(_bvnr_ucrt_specs "")
if(NOT _bvnr_crtdll_rc EQUAL 0)
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -dumpspecs
        OUTPUT_VARIABLE _bvnr_dumpspecs
        RESULT_VARIABLE _bvnr_dumpspecs_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _bvnr_dumpspecs_rc EQUAL 0 OR NOT _bvnr_dumpspecs)
        message(FATAL_ERROR
            "MinGW cross toolchain: ${CMAKE_C_COMPILER} -dumpspecs failed, "
            "and -mcrtdll=ucrt is not supported")
    endif()
    # *libgcc runs until the next *spec. file(STRINGS) keeps the spec text
    # intact; the body has no semicolons for cmake to split on.
    string(REGEX REPLACE "\r\n" "\n" _bvnr_dumpspecs "${_bvnr_dumpspecs}")
    string(REGEX REPLACE "\r" "\n" _bvnr_dumpspecs "${_bvnr_dumpspecs}")
    set(_bvnr_dumpspecs_file "${CMAKE_BINARY_DIR}/CMakeFiles/bvnr-dumpspecs.txt")
    file(WRITE "${_bvnr_dumpspecs_file}" "${_bvnr_dumpspecs}\n")
    file(STRINGS "${_bvnr_dumpspecs_file}" _bvnr_spec_lines)
    file(REMOVE "${_bvnr_dumpspecs_file}")
    set(_bvnr_capturing OFF)
    set(_bvnr_libgcc "")
    foreach(_bvnr_line IN LISTS _bvnr_spec_lines)
        if(_bvnr_line MATCHES "^\\*[A-Za-z0-9_+.-]+:$")
            if(_bvnr_line STREQUAL "*libgcc:")
                set(_bvnr_capturing ON)
                continue()
            elseif(_bvnr_capturing)
                break()
            endif()
        elseif(_bvnr_capturing)
            string(APPEND _bvnr_libgcc "${_bvnr_line}\n")
        endif()
    endforeach()
    if(NOT _bvnr_libgcc)
        message(FATAL_ERROR
            "MinGW cross toolchain: ${CMAKE_C_COMPILER} -dumpspecs has no *libgcc spec")
    endif()
    if(_bvnr_libgcc MATCHES "(^| )-lmsvcrt( |$)")
        string(REGEX REPLACE "(^| )-lmsvcrt( |$)" "\\1-lucrt\\2" _bvnr_libgcc "${_bvnr_libgcc}")
        set(_bvnr_ucrt_specs "${CMAKE_BINARY_DIR}/bvnr-ucrt.specs")
        file(WRITE "${_bvnr_ucrt_specs}" "*libgcc:\n${_bvnr_libgcc}")
    elseif(NOT _bvnr_libgcc MATCHES "(^| )-lucrt( |$)")
        message(FATAL_ERROR
            "MinGW cross toolchain: *libgcc links neither -lmsvcrt nor -lucrt; "
            "refusing to guess the CRT")
    endif()
endif()

set(BVNR_MINGW_UCRT ON CACHE INTERNAL
    "This MinGW build selects the Universal CRT (set by the cross toolchain)")
if(_bvnr_crtdll_rc EQUAL 0)
    set(BVNR_MINGW_UCRT_CRTDL ON CACHE INTERNAL
        "Pass -mcrtdll=ucrt (this GCC supports it)" FORCE)
    set(BVNR_MINGW_UCRT_SPECS "" CACHE INTERNAL
        "No specs override; -mcrtdll=ucrt selects the CRT" FORCE)
else()
    set(BVNR_MINGW_UCRT_CRTDL OFF CACHE INTERNAL
        "This GCC has no -mcrtdll=; *libgcc is rewritten instead" FORCE)
    set(BVNR_MINGW_UCRT_SPECS "${_bvnr_ucrt_specs}" CACHE INTERNAL
        "-specs= file that links -lucrt instead of -lmsvcrt" FORCE)
endif()

# Run cross-built executables (the CLI in the smoke tests, the cross-built unit
# test binaries) through Wine when it is installed. CTest honours
# CMAKE_CROSSCOMPILING_EMULATOR for add_test() targets automatically, and the
# shared cmake/*.cmake CLI test helpers pick it up via -DEMULATOR=. When Wine is
# absent the variable stays empty: the library/CLI still cross-compile, but the
# test executables cannot be run here (CTest will report them as failures, so
# build with -DBVNR_BUILD_TESTS=OFF in that case).
find_program(BVNR_WINE NAMES wine64 wine)
if(BVNR_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${BVNR_WINE}" CACHE FILEPATH
        "Emulator used to run cross-compiled Windows executables under CTest")
endif()
