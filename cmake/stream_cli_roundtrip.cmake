# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: exercise the streaming CLI subcommands end-to-end.
#   * frames pack <files> | frames list   — multi-document record framing
#   * mux pack <chan:file> | mux list      — octet multiplexing
# Pack output is binary (octet streams contain NUL), so it is captured with
# OUTPUT_FILE rather than OUTPUT_VARIABLE; list output is text and is matched.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DEX_DIR=<examples dir> -DTMP_DIR=<scratch dir>
#         -P stream_cli_roundtrip.cmake

foreach(_var BOVNAR EX_DIR TMP_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "stream_cli_roundtrip.cmake: ${_var} not set")
    endif()
endforeach()

# ── multi-document framing ───────────────────────────────────────────────────
set(_frame_file "${TMP_DIR}/cli_docs.bvf")
execute_process(
    COMMAND ${EMULATOR} "${BOVNAR}" frames pack
            "${EX_DIR}/integers.bvnr" "${EX_DIR}/floats.bvnr" "${EX_DIR}/strings.bvnr"
    OUTPUT_FILE     "${_frame_file}"
    RESULT_VARIABLE _rc
    ERROR_VARIABLE  _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "frames pack failed (rc=${_rc})\n${_err}")
endif()

execute_process(
    COMMAND         ${EMULATOR} "${BOVNAR}" frames list "${_frame_file}"
    OUTPUT_VARIABLE _list
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "frames list failed (rc=${_rc})\n${_list}")
endif()
foreach(_needle "frame 0: OK" "frame 1: OK" "frame 2: OK" "3 document(s)")
    string(FIND "${_list}" "${_needle}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "frames list missing '${_needle}'\n${_list}")
    endif()
endforeach()

# ── octet multiplexing ───────────────────────────────────────────────────────
set(_mux_file "${TMP_DIR}/cli_mux.bvnr")
execute_process(
    COMMAND ${EMULATOR} "${BOVNAR}" mux pack
            "1:${EX_DIR}/integers.bvnr" "42:${EX_DIR}/strings.bvnr"
    OUTPUT_FILE     "${_mux_file}"
    RESULT_VARIABLE _rc
    ERROR_VARIABLE  _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "mux pack failed (rc=${_rc})\n${_err}")
endif()

execute_process(
    COMMAND         ${EMULATOR} "${BOVNAR}" mux list "${_mux_file}"
    OUTPUT_VARIABLE _mlist
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "mux list failed (rc=${_rc})\n${_mlist}")
endif()

# Reported message sizes must equal the source file sizes (faithful payloads).
file(SIZE "${EX_DIR}/integers.bvnr" _sz1)
file(SIZE "${EX_DIR}/strings.bvnr"  _sz42)
foreach(_needle "channel 1: ${_sz1} bytes" "channel 42: ${_sz42} bytes")
    string(FIND "${_mlist}" "${_needle}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "mux list missing '${_needle}'\n${_mlist}")
    endif()
endforeach()
