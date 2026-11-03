# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# cli_expect.cmake -- run the CLI once and require BOTH its exit status and its
# output.
#
# A test with PASS_REGULAR_EXPRESSION is judged by the match alone: CTest drops
# the exit status. A CLI that printed the expected line and then failed (a
# sanitizer report at teardown, a leak, an error on close) was reported as a
# pass. This script checks the status and the match together.
#
# Variables:
#   BOVNAR     the CLI binary (required)
#   EMULATOR   cross-compiling emulator, may be empty
#   ARGS       the arguments, separated by "@@"
#   EXPECT_RC  0 (default), or the exact status a refusal must exit with
#   PASS_RX    regex the combined stdout+stderr must match (optional)
#   FAIL_RX    regex it must NOT match (optional)

if(NOT DEFINED BOVNAR)
    message(FATAL_ERROR "cli_expect.cmake: BOVNAR not set")
endif()
if(NOT DEFINED EXPECT_RC)
    set(EXPECT_RC 0)
endif()

string(REPLACE "@@" ";" _args "${ARGS}")
execute_process(
    COMMAND         ${EMULATOR} "${BOVNAR}" ${_args}
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc)
set(_all "${_out}${_err}")

if(NOT "${_rc}" STREQUAL "${EXPECT_RC}")
    message(FATAL_ERROR
        "bovnar ${_args}: exit status ${_rc}, expected ${EXPECT_RC}\n${_all}")
endif()
if(DEFINED PASS_RX AND NOT "${PASS_RX}" STREQUAL "")
    if(NOT _all MATCHES "${PASS_RX}")
        message(FATAL_ERROR
            "bovnar ${_args}: output does not match '${PASS_RX}'\n${_all}")
    endif()
endif()
if(DEFINED FAIL_RX AND NOT "${FAIL_RX}" STREQUAL "")
    if(_all MATCHES "${FAIL_RX}")
        message(FATAL_ERROR
            "bovnar ${_args}: output matches '${FAIL_RX}'\n${_all}")
    endif()
endif()
