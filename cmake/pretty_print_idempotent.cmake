# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: assert that `bovnar pretty-print` is idempotent — i.e. that the
# canonical serialiser is a fixed point. Pretty-printing a document and then
# pretty-printing that output again must yield byte-identical text, and the
# first pass's output must itself re-parse cleanly. A canonical form that is not
# a fixed point breaks content hashing, signing, and diffing.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DBVNR_FILE=<input.bvnr> -DTMP_FILE=<scratch>
#         -P pretty_print_idempotent.cmake

foreach(_var BOVNAR BVNR_FILE TMP_FILE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "pretty_print_idempotent.cmake: ${_var} not set")
    endif()
endforeach()

# Both passes go straight to FILES, never through a CMake variable. A bovnar
# document may embed octet streams full of NUL bytes, and a CMake variable
# cannot hold those — routing the output through one truncated the document and
# made pass 2 fail to re-parse, reporting a library defect that was not there.
set(_p1 "${TMP_FILE}.pass1")
set(_p2 "${TMP_FILE}.pass2")

# Pass 1: canonicalise the original document.
execute_process(
    COMMAND           ${EMULATOR} "${BOVNAR}" pretty-print "${BVNR_FILE}"
    OUTPUT_FILE       "${_p1}"
    ERROR_VARIABLE    err1
    RESULT_VARIABLE   rc1)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "pretty-print pass 1 failed (rc=${rc1}) on ${BVNR_FILE}\n${err1}")
endif()

# Pass 2: canonicalise the canonical output. This also proves pass 1 re-parses.
execute_process(
    COMMAND           ${EMULATOR} "${BOVNAR}" pretty-print "${_p1}"
    OUTPUT_FILE       "${_p2}"
    ERROR_VARIABLE    err2
    RESULT_VARIABLE   rc2)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR
        "pretty-print pass 2 failed (rc=${rc2}) — canonical output of "
        "${BVNR_FILE} did not re-parse:\n${err2}")
endif()

execute_process(
    COMMAND           ${CMAKE_COMMAND} -E compare_files "${_p1}" "${_p2}"
    RESULT_VARIABLE   rc_cmp
    OUTPUT_QUIET ERROR_QUIET)
if(NOT rc_cmp EQUAL 0)
    message(FATAL_ERROR
        "pretty-print is NOT idempotent on ${BVNR_FILE}: ${_p1} and ${_p2} differ")
endif()
