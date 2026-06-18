# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: assert that "bovnar query QPATH FILE" succeeds (exit 0) and that
# its stdout contains NEEDLE. Used to pin CLI query output — e.g. that a datetime
# carrying sub-second digits is printed as the faithful ISO literal rather than
# the fraction-dropping integer carrier.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DFILE=<input.bvnr> -DQPATH=<path> -DNEEDLE=<substr>
#         -P query_expect.cmake

foreach(_var BOVNAR FILE QPATH NEEDLE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "query_expect.cmake: ${_var} not set")
    endif()
endforeach()

execute_process(
    COMMAND         ${EMULATOR} "${BOVNAR}" query "${QPATH}" "${FILE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE  err
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "expected `bovnar query ${QPATH} ${FILE}` to succeed, got rc=${rc}:\n${err}")
endif()

string(FIND "${out}" "${NEEDLE}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "`bovnar query ${QPATH} ${FILE}` stdout did not contain '${NEEDLE}':\n${out}")
endif()
