# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# Regenerate the single-file amalgamation and pack the release archives.
#
# Invoked on every build by the `bvnr_package` target (add_custom_target ... ALL
# in CMakeLists.txt). Driven entirely by -D arguments so it can run via
# `cmake -P` after the library/CLI targets are built. Produces, under BIN_DIR:
#
#   amalgamate/{bovnar.h,bovnar.c}              (the regenerated amalgamation)
#   bovnar-<version>-<count>-amalgamate.tar.xz  (the amalgamation, packed)
#   bovnar-linux-<version>-<count>.tar.xz       (native Linux artifacts)
#   bovnar-macos-<version>-<count>.tar.xz       (native macOS artifacts)
#   bovnar-windows-msvc-<version>-<count>.zip   (native MSVC artifacts)
#   bovnar-windows-mingw-<version>-<count>.zip  (native or cross MinGW artifacts)
#
# <count> is the total git commit count (a monotonic build number), or 0 outside
# a checkout. The platform archive matches the current build; on a Linux host
# with the MinGW cross-build present (CROSS_DIR), the Windows .zip is packed too.
#
# Required: SRC_DIR BIN_DIR VERSION
# Optional: PYTHON  NATIVE_KIND(linux|macos|msvc|mingw)  CROSS_DIR
#           ARTIFACT_DIR (where the native libraries and CLI were built;
#                         defaults to BIN_DIR, which is right only for a
#                         single-config generator)

cmake_minimum_required(VERSION 3.21)

if(NOT SRC_DIR OR NOT BIN_DIR OR NOT VERSION)
    message(FATAL_ERROR "pack_artifacts: SRC_DIR, BIN_DIR and VERSION are required")
endif()

# --- build count: total git commits, "0" outside a working tree -------------
set(COUNT "0")
find_program(_git git)
if(_git)
    execute_process(
        COMMAND "${_git}" -C "${SRC_DIR}" rev-list --count HEAD
        OUTPUT_VARIABLE _c OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc ERROR_QUIET)
    if(_rc EQUAL 0 AND _c)
        set(COUNT "${_c}")
    endif()
endif()
set(TAG "${VERSION}-${COUNT}")
if(NOT ARTIFACT_DIR)
    set(ARTIFACT_DIR "${BIN_DIR}")
endif()

file(MAKE_DIRECTORY "${BIN_DIR}")
set(STAGE "${BIN_DIR}/.pkgstage")
file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}")

# Pack a staged top-level directory (relative to STAGE) into BIN_DIR. The
# archive is named after the directory it unpacks to and takes no name of its
# own, so the two cannot come apart.
function(_pack topdir fmt)
    if(fmt STREQUAL "zip")
        set(outfile "${topdir}.zip")
    else()
        set(outfile "${topdir}.tar.xz")
    endif()
    file(REMOVE "${BIN_DIR}/${outfile}")
    if(fmt STREQUAL "zip")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar cf "${BIN_DIR}/${outfile}" --format=zip "${topdir}"
            WORKING_DIRECTORY "${STAGE}" RESULT_VARIABLE _r)
    else()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar cJf "${BIN_DIR}/${outfile}" "${topdir}"
            WORKING_DIRECTORY "${STAGE}" RESULT_VARIABLE _r)
    endif()
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "pack_artifacts: failed to create ${outfile}")
    endif()
    message(STATUS "pack_artifacts: wrote ${outfile}")
endfunction()

# Headers + LICENSE + THIRD_PARTY_NOTICES + README, plus the documentation,
# examples and editor highlighters — common to every binary archive.
#
# THIRD_PARTY_NOTICES.md is not optional packaging. The profile tables compiled
# into these binaries carry identifiers from six external vocabularies, and two
# of those licences (UDUNITS-2's BSD-3 clause 2, CC BY 4.0's attribution) require
# the notice to travel with a BINARY distribution specifically. An archive
# shipped without it is out of compliance the moment it is downloaded.
function(_stage_common top)
    file(MAKE_DIRECTORY "${STAGE}/${top}/include")
    file(GLOB _hdrs "${SRC_DIR}/include/*.h")
    file(COPY ${_hdrs} DESTINATION "${STAGE}/${top}/include")
    file(COPY "${SRC_DIR}/LICENSE" "${SRC_DIR}/THIRD_PARTY_NOTICES.md"
              "${SRC_DIR}/README.md" DESTINATION "${STAGE}/${top}")
    # Ship the docs, runnable examples and syntax highlighters alongside the
    # binaries. Each is a committed source directory; guard with EXISTS so a
    # trimmed checkout still packs a valid (if leaner) archive.
    foreach(_extra doc examples highlighter)
        if(EXISTS "${SRC_DIR}/${_extra}")
            file(COPY "${SRC_DIR}/${_extra}" DESTINATION "${STAGE}/${top}")
        endif()
    endforeach()
endfunction()

# Stage one platform's built artifacts (libs under lib/, runtime under bin/).
#
# Every artifact is required. file(COPY) of an empty glob is not an error, so a
# packer looking in the wrong directory used to produce an archive of headers
# and docs with no library and no CLI in it, and say nothing.
function(_bvnr_require what files)
    if(NOT files)
        message(FATAL_ERROR "pack_artifacts: no ${what} found for ${top} -- "
                            "is ARTIFACT_DIR right?")
    endif()
endfunction()

function(_stage_bin top kind dir)
    _stage_common("${top}")
    file(MAKE_DIRECTORY "${STAGE}/${top}/lib" "${STAGE}/${top}/bin")
    if(kind STREQUAL "linux")
        file(GLOB _libs "${dir}/libbvnr.a" "${dir}/libbvnr.so*")
        set(_exe "${dir}/bovnar")
    elseif(kind STREQUAL "macos")
        # libbvnr.2.0.0.dylib and its two symlinks, as the .so chain on Linux.
        file(GLOB _libs "${dir}/libbvnr.a" "${dir}/libbvnr*.dylib")
        set(_exe "${dir}/bovnar")
    elseif(kind STREQUAL "msvc")
        # Runtime (.dll, .exe) under bin/; import + static libs under lib/.
        file(GLOB _dll "${dir}/bvnr.dll")
        file(GLOB _libs "${dir}/bvnr.lib" "${dir}/bvnr_static.lib")
        set(_exe "${dir}/bovnar.exe")
    elseif(kind STREQUAL "mingw")
        # Runtime (.dll, .exe) under bin/; import + static libs under lib/.
        file(GLOB _dll "${dir}/libbvnr.dll")
        file(GLOB _libs "${dir}/libbvnr.dll.a" "${dir}/libbvnr.a")
        set(_exe "${dir}/bovnar.exe")
    else()
        message(FATAL_ERROR "pack_artifacts: unknown kind '${kind}'")
    endif()
    _bvnr_require("library in ${dir}" "${_libs}")
    file(COPY ${_libs} DESTINATION "${STAGE}/${top}/lib")
    if(kind STREQUAL "msvc" OR kind STREQUAL "mingw")
        _bvnr_require("DLL in ${dir}" "${_dll}")
        file(COPY ${_dll} DESTINATION "${STAGE}/${top}/bin")
    endif()
    if(NOT EXISTS "${_exe}")
        message(FATAL_ERROR "pack_artifacts: ${_exe} not found for ${top}")
    endif()
    file(COPY "${_exe}" DESTINATION "${STAGE}/${top}/bin")
endfunction()

# --- 1) amalgamation: regenerate into BIN_DIR/amalgamate, then pack ---------
# Best-effort: a missing Python or a generator hiccup must not fail the build —
# the amalgamation is a convenience artifact and the platform archives below do
# not depend on it.
if(PYTHON)
    file(MAKE_DIRECTORY "${BIN_DIR}/amalgamate")
    # The generated *.gen.inc snippets live in the build dir; point amalgamate at
    # them so it can inline them into the single-file output.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "BVNR_GENERATED_DIR=${BIN_DIR}/generated"
                "${PYTHON}" "${SRC_DIR}/amalgamate.py" "${BIN_DIR}/amalgamate"
        RESULT_VARIABLE _r OUTPUT_QUIET)
    if(NOT _r EQUAL 0)
        message(WARNING "pack_artifacts: amalgamate.py failed; skipping the amalgamation archive")
    else()
        set(_atop "bovnar-${TAG}-amalgamate")
        file(MAKE_DIRECTORY "${STAGE}/${_atop}")
        # Repo-root README (not dist/README.md) so the amalgamation ships the
        # project's main readme, matching the platform archives.
        file(COPY "${BIN_DIR}/amalgamate/bovnar.h" "${BIN_DIR}/amalgamate/bovnar.c"
             "${SRC_DIR}/LICENSE" "${SRC_DIR}/THIRD_PARTY_NOTICES.md"
             "${SRC_DIR}/README.md" DESTINATION "${STAGE}/${_atop}")
        # Bundle the docs and runnable examples with the single-file drop.
        foreach(_extra doc examples)
            if(EXISTS "${SRC_DIR}/${_extra}")
                file(COPY "${SRC_DIR}/${_extra}" DESTINATION "${STAGE}/${_atop}")
            endif()
        endforeach()
        _pack("${_atop}" xz)
    endif()
else()
    message(WARNING "pack_artifacts: Python not found; skipping the amalgamation archive")
endif()

# --- 2) native platform archive ---------------------------------------------
# The two Windows toolchains are named apart, in the file AND in the directory
# it unpacks to. They used to share both: only the release job told them apart,
# by renaming the file, so the msvc and mingw assets of one release unpacked
# onto each other -- import libraries of one ABI beside the DLL of the other.
if(NATIVE_KIND STREQUAL "linux")
    _stage_bin("bovnar-linux-${TAG}" linux "${ARTIFACT_DIR}")
    _pack("bovnar-linux-${TAG}" xz)
elseif(NATIVE_KIND STREQUAL "macos")
    _stage_bin("bovnar-macos-${TAG}" macos "${ARTIFACT_DIR}")
    _pack("bovnar-macos-${TAG}" xz)
elseif(NATIVE_KIND STREQUAL "msvc")
    _stage_bin("bovnar-windows-msvc-${TAG}" msvc "${ARTIFACT_DIR}")
    _pack("bovnar-windows-msvc-${TAG}" zip)
elseif(NATIVE_KIND STREQUAL "mingw")
    _stage_bin("bovnar-windows-mingw-${TAG}" mingw "${ARTIFACT_DIR}")
    _pack("bovnar-windows-mingw-${TAG}" zip)
endif()

# --- 3) cross MinGW Windows archive (Linux host with build/mingw present) ----
if(CROSS_DIR AND EXISTS "${CROSS_DIR}/bovnar.exe")
    _stage_bin("bovnar-windows-mingw-${TAG}" mingw "${CROSS_DIR}")
    _pack("bovnar-windows-mingw-${TAG}" zip)
elseif(CROSS_DIR)
    message(STATUS
        "pack_artifacts: ${CROSS_DIR}/bovnar.exe not present; skipping windows.zip "
        "(enable BVNR_CROSS_MINGW or build the MinGW target)")
endif()

file(REMOVE_RECURSE "${STAGE}")
