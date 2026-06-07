# CTest driver for the single-file amalgamation.
#
# Generates the amalgamation into the build tree (never touching the committed
# dist/), compiles it together with the smoke test under the same strict
# warnings as the main build, runs it, and finally verifies the committed
# dist/bovnar.{h,c} are byte-identical to a fresh generation. So this one test
# catches both a broken amalgamation (compile/run failure) and a stale committed
# one (someone changed sources without re-running amalgamate.py).
#
# Required -D args: SRC_DIR, PYTHON, CC, BIN_DIR

if(NOT SRC_DIR OR NOT PYTHON OR NOT CC OR NOT BIN_DIR)
    message(FATAL_ERROR "amalgam_smoke.cmake: SRC_DIR/PYTHON/CC/BIN_DIR required")
endif()

set(gen "${BIN_DIR}/amalgam_gen")
file(MAKE_DIRECTORY "${gen}")

# 1. Generate into the build tree.
execute_process(
    COMMAND "${PYTHON}" "${SRC_DIR}/amalgamate.py" "${gen}"
    WORKING_DIRECTORY "${SRC_DIR}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "amalgamate.py failed (${rc})")
endif()

# 2. Compile the freshly-generated amalgamation + smoke test (strict flags).
set(exe "${BIN_DIR}/amalgam_smoke")
execute_process(
    COMMAND "${CC}" -std=c99 -pedantic -O2 -Wall -Wextra -Wconversion -Wno-override-init -Werror
            "-I${gen}"
            "${SRC_DIR}/tests/amalgam_smoke.c"
            "${gen}/bovnar.c"
            -lm -o "${exe}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "amalgamation failed to compile:\n${out}\n${err}")
endif()

# 3. Run it.
execute_process(COMMAND "${exe}" RESULT_VARIABLE rc OUTPUT_VARIABLE out)
message(STATUS "${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "amalgam smoke test failed (${rc})")
endif()

# 4. The committed dist/ must match the fresh generation (no stale checkout).
foreach(f bovnar.h bovnar.c)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${gen}/${f}" "${SRC_DIR}/dist/${f}"
        RESULT_VARIABLE diff)
    if(NOT diff EQUAL 0)
        message(FATAL_ERROR
            "dist/${f} is stale: re-run `python3 amalgamate.py` and commit dist/.")
    endif()
endforeach()
