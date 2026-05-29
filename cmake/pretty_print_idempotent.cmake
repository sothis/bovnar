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

# Pass 1: canonicalise the original document.
execute_process(
    COMMAND           "${BOVNAR}" pretty-print "${BVNR_FILE}"
    OUTPUT_VARIABLE   pass1
    ERROR_VARIABLE    err1
    RESULT_VARIABLE   rc1)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "pretty-print pass 1 failed (rc=${rc1}) on ${BVNR_FILE}\n${err1}")
endif()

# pretty-print reads a seekable regular file, so feed pass 1 back via a temp file.
file(WRITE "${TMP_FILE}" "${pass1}")

# Pass 2: canonicalise the canonical output. This also proves pass 1 re-parses.
execute_process(
    COMMAND           "${BOVNAR}" pretty-print "${TMP_FILE}"
    OUTPUT_VARIABLE   pass2
    ERROR_VARIABLE    err2
    RESULT_VARIABLE   rc2)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR
        "pretty-print pass 2 failed (rc=${rc2}) — canonical output of "
        "${BVNR_FILE} did not re-parse:\n${err2}\n--- pass 1 output ---\n${pass1}")
endif()

if(NOT pass1 STREQUAL pass2)
    message(FATAL_ERROR
        "pretty-print is NOT idempotent on ${BVNR_FILE}:\n"
        "--- pass 1 ---\n${pass1}\n--- pass 2 ---\n${pass2}")
endif()
