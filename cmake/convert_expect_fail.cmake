# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: assert that "bovnar convert --from json --to bvnr JSON_FILE"
# fails with a non-zero exit code and that its diagnostic on stderr contains
# NEEDLE. Guards the converter against silently accepting (and lossily
# converting) JSON it cannot faithfully represent — arrays of objects, jagged /
# deeply-nested arrays, non-identifier keys, out-of-range integers, and trailing
# content. Before the fix several of these produced corrupt output with exit 0.
#
# Invoked as either (legacy json->bvnr):
#   cmake -DBOVNAR=<binary> -DJSON_FILE=<input.json> -DNEEDLE=<substr>
#         -P convert_expect_fail.cmake
# or with an explicit direction (e.g. bvnr->json):
#   cmake -DBOVNAR=<binary> -DINPUT_FILE=<input> -DFROM=bvnr -DTO=json
#         -DNEEDLE=<substr> -P convert_expect_fail.cmake

foreach(_var BOVNAR NEEDLE)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "convert_expect_fail.cmake: ${_var} not set")
    endif()
endforeach()

# JSON_FILE is the legacy spelling of the json->bvnr direction.
if(DEFINED JSON_FILE)
    set(INPUT_FILE "${JSON_FILE}")
    set(FROM json)
    set(TO   bvnr)
endif()
foreach(_var INPUT_FILE FROM TO)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "convert_expect_fail.cmake: ${_var} not set")
    endif()
endforeach()

execute_process(
    COMMAND         ${EMULATOR} "${BOVNAR}" convert --from "${FROM}" --to "${TO}" "${INPUT_FILE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE  err
    RESULT_VARIABLE rc)

if(rc EQUAL 0)
    message(FATAL_ERROR
        "expected conversion of ${INPUT_FILE} to fail, but it succeeded:\n${out}")
endif()

string(FIND "${err}" "${NEEDLE}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "conversion of ${INPUT_FILE} failed as expected, but the message did "
        "not contain '${NEEDLE}':\n${err}")
endif()
