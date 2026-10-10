# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# CTest helper: the command-line contract of `bovnar`, one case per line.
#
# Every case here was a defect a review of the 2.0.0 candidate found by running
# the tool, and each was seen to FAIL against the binary from before its fix.
# They are kept in one script because they share one shape -- run this command
# line, expect this exit status, expect this text -- and because the fixtures
# several of them need (a document nested 65 deep, a JSON file with a NUL after
# its closing brace) are cheaper to write here than to keep in the tree.
#
# The exit status is asserted EXACTLY, not as zero/non-zero. The contract is
#   0  it worked
#   1  the document, the input or the output failed
#   2  the command line was wrong
# and half of what this file pins is which of the last two a case belongs to.
#
# Invoked as:
#   cmake -DBOVNAR=<binary> -DTMP_DIR=<scratch dir> [-DEMULATOR=<wrapper>]
#         -P cli_regressions.cmake
#
# All failures are collected and reported together, so one run shows the whole
# picture rather than the first broken case.

foreach(_var BOVNAR TMP_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "cli_regressions.cmake: ${_var} not set")
    endif()
endforeach()

set(_dir "${TMP_DIR}/cli_regressions")
file(REMOVE_RECURSE "${_dir}")
file(MAKE_DIRECTORY "${_dir}")

set(_failures "")
set(_ncases 0)

# _case(<name> <rc> <out-regex> <err-regex> <forbidden-regex> <arg>...)
# An empty regex is not checked. <forbidden-regex> is matched against stdout and
# stderr together. STDIN / STDOUT redirections are taken from the variables
# _stdin and _stdout_file when the caller sets them just before the call.
function(_case name want_rc want_out want_err forbid)
    set(_extra "")
    if(DEFINED _stdin)
        list(APPEND _extra INPUT_FILE "${_stdin}")
    endif()
    if(DEFINED _stdout_file)
        list(APPEND _extra OUTPUT_FILE "${_stdout_file}")
    else()
        list(APPEND _extra OUTPUT_VARIABLE _out)
    endif()
    execute_process(
        COMMAND ${EMULATOR} "${BOVNAR}" ${ARGN}
        WORKING_DIRECTORY "${_dir}"
        ${_extra}
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc)
    if(DEFINED _stdout_file)
        set(_out "")
    endif()
    set(_why "")
    if(NOT "${_rc}" STREQUAL "${want_rc}")
        string(APPEND _why "    exit status ${_rc}, expected ${want_rc}\n")
    endif()
    if(NOT want_out STREQUAL "" AND NOT _out MATCHES "${want_out}")
        string(APPEND _why "    stdout does not match '${want_out}'\n")
    endif()
    if(NOT want_err STREQUAL "" AND NOT _err MATCHES "${want_err}")
        string(APPEND _why "    stderr does not match '${want_err}'\n")
    endif()
    if(NOT forbid STREQUAL "" AND "${_out}${_err}" MATCHES "${forbid}")
        string(APPEND _why "    output matches forbidden '${forbid}'\n")
    endif()
    math(EXPR _n "${_ncases} + 1")
    set(_ncases ${_n} PARENT_SCOPE)
    if(NOT _why STREQUAL "")
        string(REPLACE ";" " " _cmdline "${ARGN}")
        string(SUBSTRING "${_out}" 0 300 _o)
        string(SUBSTRING "${_err}" 0 400 _e)
        set(_failures
            "${_failures}  ${name}: bovnar ${_cmdline}\n${_why}    stdout: ${_o}\n    stderr: ${_e}\n"
            PARENT_SCOPE)
    endif()
endfunction()

# ── fixtures ────────────────────────────────────────────────────────────────
file(WRITE "${_dir}/a.bvnr" ".a = 1;\n.s = {.t = <float:64,m> 2.5;};\n")
file(WRITE "${_dir}/b.bvnr" ".b = 2;\n")
file(WRITE "${_dir}/plain.bvnr" ".a = 1;\n.name = \"x\";\n.arr = [1, 2, 3];\n")
file(WRITE "${_dir}/j.json" "{\"a\":1}")
file(WRITE "${_dir}/j.txt"  "{\"a\":1}")
file(WRITE "${_dir}/bad.bvnr" ".a = 1;\n.b = @@;\n.c = 3;\n")
file(WRITE "${_dir}/u.bvnr" ".len = <float:64,k~m> 2.5;\n.i = <uint:8,k~m> 2;\n.j = <sint:8,m> -3;\n")
file(WRITE "${_dir}/comment.bvnr" "# a comment\n.a = 1; # another\n")

foreach(_depth 65 255)
    string(REPEAT ".a={" ${_depth} _open)
    string(REPEAT "};"   ${_depth} _close)
    file(WRITE "${_dir}/struct${_depth}.bvnr" "${_open}.x=1;${_close}\n")
    string(REPEAT "[" ${_depth} _open)
    string(REPEAT "]" ${_depth} _close)
    file(WRITE "${_dir}/array${_depth}.bvnr" ".a=${_open}1${_close};\n")
    file(WRITE "${_dir}/array${_depth}.json" "{\"a\":${_open}1${_close}}")
    string(REPEAT "{\"a\":" ${_depth} _open)
    string(REPEAT "}"       ${_depth} _close)
    file(WRITE "${_dir}/struct${_depth}.json" "${_open}1${_close}")
endforeach()

# ── 1. the writer nests as deep as the reader ───────────────────────────────
# pretty-print opened its reader at 255 levels and left its writer at 64, so a
# valid document 65..255 deep was read and could not be written back; the JSON
# converter had the same writer.
foreach(_depth 65 255)
    foreach(_kind struct array)
        _case("validate_${_kind}${_depth}" 0 "OK" "" ""
              validate "${_kind}${_depth}.bvnr")
        _case("pretty_${_kind}${_depth}" 0 "x = |\\[<uint" "" "nesting_too_high"
              pretty-print "${_kind}${_depth}.bvnr")
        _case("canonical_${_kind}${_depth}" 0 "" "" "nesting_too_high"
              pretty-print --canonical "${_kind}${_depth}.bvnr")
    endforeach()
    _case("json_array${_depth}" 0 "\\[\\[" "" "nesting_too_high"
          convert "array${_depth}.json")
endforeach()
# One JSON object level is the document itself, so 255 braces is 254 structs.
_case(json_struct255 0 "\\.a = " "" "nesting_too_high" convert struct255.json)
# What pretty-print writes at the limit is itself a valid document.
set(_stdout_file "${_dir}/struct255.pp.bvnr")
_case(pretty_struct255_to_file 0 "" "" "" pretty-print struct255.bvnr)
unset(_stdout_file)
_case(pretty_struct255_revalidates 0 "OK" "" "" validate struct255.pp.bvnr)

# ── 2. a failed write to stdout is a failure ────────────────────────────────
# Nothing checked the stdio buffer at exit. /dev/full fails every write with
# ENOSPC; where a platform has no such device the cases are skipped.
if(EXISTS "/dev/full" AND NOT EMULATOR)
    set(_stdout_file "/dev/full")
    _case(devfull_query    1 "" "error writing to standard output" "" query .a a.bvnr)
    _case(devfull_validate 1 "" "error writing to standard output" "" validate a.bvnr)
    _case(devfull_events   1 "" "error writing to standard output" "" events a.bvnr)
    _case(devfull_version  1 "" "error writing to standard output" "" version)
    _case(devfull_help     1 "" "error writing to standard output" "" --help)
    _case(devfull_convert  1 "" "error writing to standard output" "" convert j.json)
    _case(devfull_tojson   1 "" "error writing to standard output" "" convert plain.bvnr)
    # ...and the usage error keeps ITS status: the write check must not turn a
    # 2 into a 1, nor invent a failure where nothing was printed.
    _case(devfull_usage    2 "" "unknown option" "error writing" validate --nope a.bvnr)
    unset(_stdout_file)
endif()

# ── 7. query under a conversion policy ───────────────────────────────────────
# `<uint:8,k~m> 2` in metres is 2000: the DOM kept the uint:8 and failed the
# whole document, for a policy validate and events accept.
_case(query_widens_converted_int 0 "^2000\n$" "" "Parse error"
      query --unit m .i u.bvnr)
_case(query_unrelated_key_not_failed 0 "^-3\n$" "" "Parse error"
      query --unit m .j u.bvnr)
_case(validate_same_policy 0 "OK" "" "" validate --unit m u.bvnr)
# --base has nothing to render into here; it was either invisible or fatal.
_case(query_base_refused 2 "" "query: --base is not supported here" "Parse error|unknown option"
      query --unit m --base 2 .len u.bvnr)

# ── 8. convert says what it lost, and refuses what it would change ───────────
file(WRITE "${_dir}/nonfinite.bvnr" ".a = <float:64> inf;\n.b = <float:64> nan;\n")
file(WRITE "${_dir}/wide.bvnr"
     ".a = <float:128> 0.1234567890123456789012345678901234;\n.b = <float_dec:128> 0.1;\n.c = <float:64> 0.5;\n.d = <float_dec:32> 0.5;\n")
file(WRITE "${_dir}/dt.bvnr" "#!bovnar 1.1\n.t = <datetime:64,gps> 5;\n")
_case(lossy_nonfinite 1 "\"a\": null" "2 non-finite float\\(s\\) become null, e\\.g\\. \\.a \\(inf\\)" ""
      convert nonfinite.bvnr)
_case(lossy_wide_float 1 "\"c\": 0.5"
      "2 float\\(s\\) wider or more exact than a 64-bit binary float are rounded to one, e\\.g\\. \\.a \\(<float:128>\\)" ""
      convert wide.bvnr)
_case(lossy_datetime 1 "\"t\": 5"
      "1 datetime\\(s\\) become bare integers, e\\.g\\. \\.t \\(seconds since the gps epoch\\)" ""
      convert dt.bvnr)
_case(lossless_stays_zero 0 "\"name\": \"x\"" "" "LOSSY" convert plain.bvnr)
file(WRITE "${_dir}/overflow.json" "{\"ok\":1.5,\"big\":{\"x\":[1.0,1e999]}}")
_case(json_float_overflow 1 "^$" "JSON number under key 'x' is too large for a 64-bit float" "<float> inf"
      convert overflow.json)
file(WRITE "${_dir}/underflow.json" "{\"tiny\":1e-999,\"long\":0.12345678901234567890123}")
_case(json_underflow_is_rounding 0 "\\.tiny = <float> 0\\.0;" "" "" convert underflow.json)
_case(help_documents_lossy 0 "bvnr -> json is LOSSY.*exit status is 1" "" "" --help)

# ── 9. --base is validated as --base ─────────────────────────────────────────
foreach(_b 0 1 63 65 84 86 "" "+16" " 16" "16x" "-1")
    _case("base_refused_${_b}" 2 "" "--base '.*' is not a base bovnar can write"
          "--unit 'm'|unusable unit policy|: OK"
          validate --unit m --base "${_b}" u.bvnr)
endforeach()
foreach(_b 2 16 62 64 85)
    _case("base_accepted_${_b}" 0 "OK" "" "" validate --base ${_b} a.bvnr)
endforeach()

# ── 10. a recovered error is an error, from a file and from stdin alike ──────
_case(events_c_file 1 "1 error\\(s\\) recovered" "" "All tokens validated successfully"
      events -c bad.bvnr)
set(_stdin "${_dir}/bad.bvnr")
_case(events_c_stdin 1 "1 error\\(s\\) recovered" "" "All tokens validated successfully"
      events -c -)
unset(_stdin)
_case(events_c_clean 0 "All tokens validated successfully" "" "" events -c a.bvnr)

# ── 11. bench measures what it was asked to ──────────────────────────────────
_case(bench_size_is_the_size 0 "\"bytes\":(9[0-9][0-9]|10[01][0-9]|102[0-4])," "" ""
      bench --profile scalars --size 1024 --iterations 1 --warmup 0 --json)
_case(bench_failed_cell 1 "" "FAILED: mixed/8.*1 cell\\(s\\) could not be measured" ""
      bench --profile mixed --size 8 --iterations 1 --warmup 0)
_case(bench_missing_value 2 "" "bench: --profile needs a value" "unknown option" bench --profile)
_case(bench_missing_size  2 "" "bench: --size needs a value" "unknown option" bench --size)
_case(bench_size_garbage  2 "" "bench: --size '12abc' is not a payload size" "\"bytes\""
      bench --profile scalars --size 12abc --iterations 1 --json)
_case(bench_unknown_profile 2 "" "bench: --profile 'bogus' is not a known profile" "\"bytes\""
      bench --profile scalars,bogus --size 1024 --iterations 1 --json)
_case(bench_unknown_option 2 "" "bench: unknown option: --nope" "" bench --nope)
_case(bench_help_stdout 0 "--min-overhead +Drop the on_unverified" "^$" "" bench --help)

# ── 12. one rule for arguments, one meaning per exit status ──────────────────
_case(help_stdout          0 "Usage: .* <command>" "^$" "" --help)
_case(help_short_stdout    0 "Usage: .* <command>" "^$" "" -h)
_case(no_args_is_usage     2 "^$" "Usage: " "")
_case(unknown_command      2 "^$" "Unknown command: nope" "" nope)
_case(validate_no_file     2 "" "Usage: .* validate" "" validate)
_case(query_too_few        2 "" "Usage: .* query" "" query .a)
_case(query_too_many       2 "" "Usage: .* query" "" query .a a.bvnr b.bvnr)
_case(pretty_no_file       2 "" "Usage: .* pretty-print" "" pretty-print)
_case(convert_no_file      2 "" "Usage: .* convert" "" convert)
_case(convert_two_files    2 "^$" "convert: takes exactly one file" "" convert j.json a.bvnr)
_case(convert_unknown_opt  2 "^$" "convert: unknown option: --bogus" "" convert --bogus j.json)
_case(convert_from_missing 2 "" "convert: --from needs a value" "" convert j.json --from)
_case(convert_bad_format   2 "" "convert: --from 'xml' is not a format" "" convert --from xml j.json)
_case(convert_from_alone   0 "\\.a = <uint> 1;" "" "" convert --from json j.txt)
_case(convert_to_alone     0 "\\.a = <uint> 1;" "" "" convert --to bvnr j.txt)
_case(convert_to_json_alone 0 "\"a\": 1" "" "json -> json" convert --to json plain.bvnr)
_case(convert_no_extension 2 "" "cannot detect format of 'j.txt'" "" convert j.txt)
# doc/06 7.3: convert takes the unit-policy flags and ignores them, arguments
# included -- `m` here is the flag's value, not a second file.
_case(convert_ignores_policy 0 "\\.a = <uint> 1;" "" "unknown option|exactly one"
      convert --unit m --si --base 16 --require-field .a=m j.json)
_case(events_two_files     2 "" "events: takes exactly one file" "" events a.bvnr b.bvnr)
_case(frames_list_two      2 "" "frames list: takes exactly one file" "" frames list a.bvnr b.bvnr)
_case(mux_list_two         2 "" "mux list: takes exactly one file" "" mux list a.bvnr b.bvnr)
_case(version_extra        2 "" "version: takes no arguments" "" version x)
# mux pack checks the whole line before writing: a bad second argument used to
# leave half a stream on stdout and exit 1.
_case(mux_pack_bad_arg     2 "^$" "mux pack: expected channel:file, got 'oops'" "" mux pack 1:a.bvnr oops)
_case(mux_pack_bad_channel 2 "^$" "mux pack: invalid channel '-1'" "" mux pack 1:a.bvnr -1:b.bvnr)
_case(mux_pack_no_file     2 "^$" "mux pack: no file after '1:'" "" mux pack 1:)
# Runtime failures stay 1.
_case(missing_file_is_1    1 "" "No such file" "" validate nope.bvnr)
_case(bad_document_is_1    1 "" "Validation failed" "" validate bad.bvnr)
_case(query_missing_path_is_1 1 "" "not found" "" query .nope a.bvnr)

# ── 17. messages that name the cause ─────────────────────────────────────────
file(WRITE "${_dir}/rescale.bvnr" ".e = <uint:8,c~m> 5;\n")
_case(canonical_rescale_named 1 ""
      "pretty-print: rescale.bvnr: cannot write the value at line 1, col [0-9]+: value_out_of_range.*--canonical rescales"
      "parse error|scanner_callback_failed|Writer finish failed"
      pretty-print --canonical rescale.bvnr)
# A prefix power whose scale no double holds is still a legal unit: the value is
# moved exactly, and what cannot be written is the VALUE, said as such. Both
# used to be "unit_illegal", about a unit `validate` had just accepted.
file(WRITE "${_dir}/farscale.bvnr" ".e = <float_dec:128> 5.0 q~m^11;\n")
_case(canonical_far_scale_written 0 "<float_dec:128> 0\\.0+5 m¹¹;" "" "unit_illegal"
      pretty-print --canonical farscale.bvnr)
file(WRITE "${_dir}/farscale_int.bvnr" ".e = 5 y~m^20;\n")
_case(canonical_far_scale_int 1 "" "value_out_of_range.*--canonical rescales" "unit_illegal"
      pretty-print --canonical farscale_int.bvnr)
_case(parse_error_still_a_parse_error 1 "" "Pretty-print parse error: unexpected_input_byte at line 2"
      "Writer finish failed" pretty-print bad.bvnr)
string(ASCII 8 _bs)
string(ASCII 127 _del)
file(WRITE "${_dir}/ctrl.json" "{\"outer\":{\"note\":\"a\\bb\"}}")
_case(json_control_char_named 1 "^$"
      "JSON string under key 'note' contains the control character U\\+0008, which a bovnar string cannot carry"
      "unexpected_input_byte" convert ctrl.json)
file(WRITE "${_dir}/ctrl2.json" "{\"list\":[\"ok\",\"x${_del}y\"]}")
_case(json_control_char_in_array 1 "^$" "under key 'list' contains the control character U\\+007F"
      "unexpected_input_byte" convert ctrl2.json)
file(WRITE "${_dir}/tab.json" "{\"t\":\"a\\tb\\nc\\fd\"}")
_case(json_escapable_controls_ok 0 "\\.t = \"a\\\\tb\\\\nc\\\\fd\";" "" "" convert tab.json)
# A NUL after the closing brace hid everything behind it.
# CMake cannot put a NUL into a file from a string (file(WRITE) stops at it), so
# the fixture is written by printf where there is one, and the case is skipped
# where there is not.
set(_have_nul FALSE)
find_program(_printf printf)
if(_printf AND NOT WIN32)
    execute_process(COMMAND "${_printf}" "{\"a\":1}\\000garbage"
                    OUTPUT_FILE "${_dir}/nul.json" RESULT_VARIABLE _prc)
    if(_prc EQUAL 0)
        set(_have_nul TRUE)
    endif()
endif()
if(_have_nul)
    _case(json_trailing_nul 1 "^$" "trailing content after JSON value \\(a NUL byte\\)" ""
          convert nul.json)
endif()
_case(help_says_comments_dropped 0 "Comments are not part of a.*NOT preserved" "" "" --help)
_case(pretty_drops_comments 0 "^\\.a = <uint:64,_10,no_unit> 1;\n$" "" "comment" pretty-print comment.bvnr)

# ── verdict ──────────────────────────────────────────────────────────────────
if(NOT _failures STREQUAL "")
    message(FATAL_ERROR "bovnar CLI regressions (${_ncases} cases):\n${_failures}")
endif()
message(STATUS "bovnar CLI regressions: ${_ncases} cases passed")
