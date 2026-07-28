# Do the conformance case counts in doc/13 still match the suite?
#
# The documented total said 306 in six places while the suite ran 319, and a
# whole group ("limits") was missing from the table. Numbers copied into prose
# drift; this one had drifted already, so check it rather than fix it again.
#
# The suite prints a per-group TAP plan and a total, both of which are compared
# against the table in doc/13_bovnar_conformance.md.
# The TAP stream goes to stdout, the summary line to stderr; look at both.
#
# Launch through CMAKE_CROSSCOMPILING_EMULATOR when there is one, the way
# query_expect.cmake and events_ok.cmake already do. Without it the MinGW cross
# build ran the .exe directly and the shell tried to execute a PE binary as a
# script -- "MZ...: not found", "Syntax error: \"(\" unexpected" -- so this gate
# was permanently red on the Windows target, and red in a way that says nothing
# about whether the counts actually drifted.
set(_cmd ${CONFORMANCE})
if(EMULATOR)
    set(_cmd ${EMULATOR} ${CONFORMANCE})
endif()
execute_process(COMMAND ${_cmd} OUTPUT_VARIABLE _out
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "conformance suite failed to run: ${_rc}\n${_err}")
endif()
set(_tap "${_out}${_err}")
if(NOT _tap MATCHES "All ([0-9]+) conformance tests passed")
    message(FATAL_ERROR "could not read the case total from the suite output")
endif()
set(_actual ${CMAKE_MATCH_1})

file(READ "${DOCFILE}" _doc)
if(NOT _doc MATCHES "\\| \\*\\*Total\\*\\* \\| \\*\\*([0-9]+)\\*\\* \\|")
    message(FATAL_ERROR "no Total row in ${DOCFILE}")
endif()
set(_documented ${CMAKE_MATCH_1})

if(NOT _actual EQUAL _documented)
    message(FATAL_ERROR
        "conformance case count drifted: the suite runs ${_actual}, "
        "doc/13_bovnar_conformance.md says ${_documented}.\n"
        "Update the per-group table and its Total row (and the counts quoted in "
        "README.md and web/index.html).")
endif()

# Every group the suite reports must appear in the table, with the right count.
string(REGEX MATCHALL "# Subtest: [a-z_]+" _groups "${_tap}")
string(REGEX MATCHALL "    1\\.\\.[0-9]+" _plans "${_tap}")
list(LENGTH _groups _ngroups)
list(LENGTH _plans _nplans)
if(NOT _ngroups EQUAL _nplans)
    message(FATAL_ERROR "malformed TAP: ${_ngroups} groups, ${_nplans} plans")
endif()
math(EXPR _last "${_ngroups} - 1")
foreach(i RANGE ${_last})
    list(GET _groups ${i} _g)
    list(GET _plans  ${i} _p)
    string(REPLACE "# Subtest: " "" _g "${_g}")
    string(REPLACE "    1.." "" _p "${_p}")
    if(NOT _doc MATCHES "\\| \\`${_g}\\` \\| ${_p} \\|")
        message(FATAL_ERROR
            "group '${_g}' runs ${_p} cases; doc/13's table does not say so "
            "(row missing, or a different count)")
    endif()
endforeach()
message(STATUS "conformance counts match doc/13 (${_actual} cases, ${_ngroups} groups)")
