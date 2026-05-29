# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: round-trip a JSON document through bovnar and back to JSON and
# assert the result is byte-identical to the input once insignificant
# whitespace is removed. This guards the json<->bvnr converter against
# regressions in value fidelity — integer range (incl. uint64 above INT64_MAX),
# 2-D array structure, nested objects, booleans and null.
#
# JSON_FILE must be written so that it already matches the converter's canonical
# output (key order preserved, "3.5" rather than "3.50", no redundant zeros), so
# that "strip(roundtrip(input)) == strip(input)" holds.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DJSON_FILE=<input.json> -DTMP_FILE=<scratch.bvnr>
#         -P convert_json_roundtrip.cmake

foreach(_var BOVNAR JSON_FILE TMP_FILE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "convert_json_roundtrip.cmake: ${_var} not set")
    endif()
endforeach()

# json -> bvnr
execute_process(
    COMMAND         "${BOVNAR}" convert --from json --to bvnr "${JSON_FILE}"
    OUTPUT_VARIABLE bvnr_out
    ERROR_VARIABLE  err1
    RESULT_VARIABLE rc1)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "json->bvnr failed (rc=${rc1}) on ${JSON_FILE}\n${err1}")
endif()
file(WRITE "${TMP_FILE}" "${bvnr_out}")

# bvnr -> json
execute_process(
    COMMAND         "${BOVNAR}" convert --from bvnr --to json "${TMP_FILE}"
    OUTPUT_VARIABLE json_out
    ERROR_VARIABLE  err2
    RESULT_VARIABLE rc2)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "bvnr->json failed (rc=${rc2}) on ${TMP_FILE}\n${err2}")
endif()

file(READ "${JSON_FILE}" json_in)

# Strip all whitespace from both sides before comparing.
string(REGEX REPLACE "[ \t\r\n]" "" _in  "${json_in}")
string(REGEX REPLACE "[ \t\r\n]" "" _out "${json_out}")

if(NOT _in STREQUAL _out)
    message(FATAL_ERROR
        "round-trip mismatch for ${JSON_FILE}\n"
        "--- input  ---\n${_in}\n"
        "--- output ---\n${_out}")
endif()
