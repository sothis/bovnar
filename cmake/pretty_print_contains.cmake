# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: pretty-print BVNR_FILE and assert that every substring in the
# REQUIRE list ('|'-separated — '|' never occurs in BVNR, and avoids clashing
# with CMake's ';' list separator) appears in the output. Guards against the
# canonical serialiser silently dropping information — in particular an inline
# unit on an un-annotated value ("9.81 m/s") must be folded into the annotation
# ("<float:64,_10,m/s>"), not replaced by "no_unit".
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DBVNR_FILE=<input.bvnr> "-DREQUIRE=a|b|c"
#         -P pretty_print_contains.cmake

foreach(_var BOVNAR BVNR_FILE REQUIRE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "pretty_print_contains.cmake: ${_var} not set")
    endif()
endforeach()

execute_process(
    COMMAND           "${BOVNAR}" pretty-print "${BVNR_FILE}"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    err
    RESULT_VARIABLE   rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "pretty-print failed (rc=${rc}) on ${BVNR_FILE}\n${err}")
endif()

string(REPLACE "|" ";" _needles "${REQUIRE}")
foreach(needle IN LISTS _needles)
    string(FIND "${out}" "${needle}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "pretty-print output of ${BVNR_FILE} is missing '${needle}' "
            "(inline unit dropped?):\n--- output ---\n${out}")
    endif()
endforeach()
