# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: `bovnar pretty-print --canonical` is a CANONICAL form, which is a
# stronger claim than "it pretty-prints" and needs a different test.
#
# Two properties, and the mode is worthless without both:
#
#   CONVERGENCE  two documents that mean the same thing but spell their units
#                differently -- "5.0 k~m" and "5000.0 m", "2.0 m·m" and "2.0 m²"
#                -- must produce BYTE-IDENTICAL output. This is what makes a
#                signature, a content hash or a semantic diff possible, and it is
#                the whole reason for the mode.
#
#   IDEMPOTENCE  canonicalising the output again must change nothing. Without it
#                the "canonical" form is just one more spelling, and a consumer
#                that stores the hash of a canonicalised document would see it
#                move on the next pass.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DFILE_A=<a.bvnr> -DFILE_B=<b.bvnr> -DTMP_DIR=<dir>
#         -P canonical_converges.cmake

foreach(_var BOVNAR FILE_A FILE_B TMP_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "canonical_converges.cmake: ${_var} not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${TMP_DIR}")
set(_a "${TMP_DIR}/canon_a.out")
set(_b "${TMP_DIR}/canon_b.out")
set(_aa "${TMP_DIR}/canon_a.out.again")

macro(_canon in out)
    execute_process(
        COMMAND         ${EMULATOR} "${BOVNAR}" pretty-print --canonical "${in}"
        OUTPUT_FILE     "${out}"
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "pretty-print --canonical ${in} failed (rc=${_rc}):\n${_err}")
    endif()
endmacro()

_canon("${FILE_A}" "${_a}")
_canon("${FILE_B}" "${_b}")

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${_a}" "${_b}"
                RESULT_VARIABLE _diff)
if(NOT _diff EQUAL 0)
    file(READ "${_a}" _ta)
    file(READ "${_b}" _tb)
    message(FATAL_ERROR
        "canonical form does not CONVERGE: two documents meaning the same thing "
        "produced different bytes.\n--- ${FILE_A} ---\n${_ta}\n--- ${FILE_B} "
        "---\n${_tb}")
endif()

# Idempotence, on the already-canonical output.
_canon("${_a}" "${_aa}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${_a}" "${_aa}"
                RESULT_VARIABLE _diff2)
if(NOT _diff2 EQUAL 0)
    file(READ "${_a}" _t1)
    file(READ "${_aa}" _t2)
    message(FATAL_ERROR
        "canonical form is not IDEMPOTENT: a second pass changed it.\n"
        "--- pass 1 ---\n${_t1}\n--- pass 2 ---\n${_t2}")
endif()

message(STATUS "canonical form converges and is idempotent")
