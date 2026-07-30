# CTest driver for a build with every unit profile switched off.
#
# The per-vocabulary switches (BVNR_WITH_UCUM_PROFILE and its six siblings) are
# compile-time, so the ordinary build can only ever exercise one of the 128
# configurations they describe. This drives the other extreme -- all seven at 0 --
# and it is the configuration where everything that can go wrong does: the
# registry array loses every row, seven groups of tables lose their last
# reference, and the strict flags an integrator uses turn either of those into an
# error rather than a warning.
#
# The amalgamation is the vehicle because it is one translation unit and one
# compiler call, and because it is also the artifact an integrator most often
# compiles by hand -- so this checks that the switches survive the trip through
# amalgamate.py, not only that they work in the CMake build.
#
# Also reports the size the switches actually buy, since that is the entire
# reason they exist. Not asserted: a threshold would be a second, worse copy of
# the compiler's decisions, and would fail for a reason that is not a defect.
#
# Required -D args: SRC_DIR, PYTHON, CC, BIN_DIR

if(NOT SRC_DIR OR NOT PYTHON OR NOT CC OR NOT BIN_DIR)
    message(FATAL_ERROR "profiles_off_build.cmake: SRC_DIR/PYTHON/CC/BIN_DIR required")
endif()

set(gen "${BIN_DIR}/gen")
file(MAKE_DIRECTORY "${gen}")

execute_process(
    COMMAND "${PYTHON}" "${SRC_DIR}/amalgamate.py" "${gen}"
    WORKING_DIRECTORY "${SRC_DIR}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "amalgamate.py failed (${rc})\n${out}\n${err}")
endif()

set(_off
    -DBVNR_WITH_UCUM_PROFILE=0
    -DBVNR_WITH_UNECE_PROFILE=0
    -DBVNR_WITH_QUDT_PROFILE=0
    -DBVNR_WITH_QUDT_QK_PROFILE=0
    -DBVNR_WITH_UDUNITS_PROFILE=0
    -DBVNR_WITH_OM_PROFILE=0
    -DBVNR_WITH_CF_PROFILE=0)

# -Werror on purpose, and the same flag set as the amalgamation smoke test: the
# whole class of defect this catches (an unreferenced table, an unused helper) is
# a warning and not an error to the compiler.
set(exe "${BIN_DIR}/profiles_off_smoke")
execute_process(
    COMMAND "${CC}" -std=c99 -pedantic -O2 -Wall -Wextra -Wconversion -Werror
            ${_off} "-I${gen}"
            "${SRC_DIR}/tests/profiles_off_smoke.c"
            "${gen}/bovnar.c"
            -o "${exe}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "the amalgamation does not compile with every profile off:\n${out}\n${err}")
endif()

execute_process(COMMAND "${exe}" RESULT_VARIABLE rc OUTPUT_VARIABLE out)
message(STATUS "${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "profiles-off smoke test failed (${rc})")
endif()

# The same source compiled with the profiles ON, so the size difference is
# attributable to the switches and to nothing else about the two builds.
set(exe_on "${BIN_DIR}/profiles_on_smoke")
execute_process(
    COMMAND "${CC}" -std=c99 -pedantic -O2 -Wall -Wextra -Wconversion -Werror
            "-I${gen}"
            "${SRC_DIR}/tests/amalgam_smoke.c"
            "${gen}/bovnar.c"
            -o "${exe_on}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
    execute_process(
        COMMAND "${CC}" -std=c99 -pedantic -O2 -Wall -Wextra -Wconversion -Werror
                ${_off} "-I${gen}"
                "${SRC_DIR}/tests/amalgam_smoke.c"
                "${gen}/bovnar.c"
                -o "${BIN_DIR}/amalgam_smoke_off"
        RESULT_VARIABLE rc2)
    if(rc2 EQUAL 0)
        file(SIZE "${exe_on}" _sz_on)
        file(SIZE "${BIN_DIR}/amalgam_smoke_off" _sz_off)
        math(EXPR _saved "(${_sz_on} - ${_sz_off}) / 1024")
        message(STATUS
            "all seven profiles cost ${_saved} KiB of binary "
            "(${_sz_on} bytes with, ${_sz_off} without)")
    endif()
endif()
