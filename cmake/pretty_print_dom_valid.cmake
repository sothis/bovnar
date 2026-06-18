# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: assert that the canonical (pretty-printed) form of a document is
# accepted by the materialised-document (DOM) tier, not just the streaming
# reader. The streaming serialiser is more lenient than the DOM (which enforces
# array homogeneity); a canonical form that the DOM rejects breaks query, JSON
# conversion, and any tree consumer. Regression guard for the datetime-array
# defect where bare elements lost their datetime type and the re-read array
# became a datetime/uint mix the DOM rejected.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DBVNR_FILE=<input.bvnr> -DTMP_FILE=<scratch>
#         -DKEY=<a key present in the document> -P pretty_print_dom_valid.cmake

foreach(_var BOVNAR BVNR_FILE TMP_FILE KEY)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "pretty_print_dom_valid.cmake: ${_var} not set")
    endif()
endforeach()

# Canonicalise via the streaming serialiser.
execute_process(
    COMMAND           ${EMULATOR} "${BOVNAR}" pretty-print "${BVNR_FILE}"
    OUTPUT_VARIABLE   canonical
    ERROR_VARIABLE    err1
    RESULT_VARIABLE   rc1)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "pretty-print failed (rc=${rc1}) on ${BVNR_FILE}\n${err1}")
endif()
file(WRITE "${TMP_FILE}" "${canonical}")

# query uses bvn_dom_parse: it materialises (and homogeneity-checks) the whole
# document before the lookup, so a successful query proves DOM acceptance.
execute_process(
    COMMAND           ${EMULATOR} "${BOVNAR}" query "${KEY}" "${TMP_FILE}"
    OUTPUT_VARIABLE   qout
    ERROR_VARIABLE    qerr
    RESULT_VARIABLE   rcq)
if(NOT rcq EQUAL 0)
    message(FATAL_ERROR
        "DOM tier REJECTED the canonical form of ${BVNR_FILE} "
        "(query ${KEY} rc=${rcq}):\n${qerr}\n--- canonical ---\n${canonical}")
endif()
