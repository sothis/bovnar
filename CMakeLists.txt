cmake_minimum_required(VERSION 3.21)
project(bovnar VERSION 1.0.0 LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BVNR_HARDEN
    "Enable runtime hardening flags (stack protector, FORTIFY, format security)"
    ON)
option(BVNR_WERROR
    "Promote all warnings to errors (for CI)"
    OFF)
option(BVNR_ANALYZER
    "Enable GCC -fanalyzer static analysis (dev/CI diagnostic; turn OFF for \
release wheel builds, where newer GCCs in the manylinux image can ICE on it)"
    ON)
option(BVNR_LTO
    "Enable link-time optimization for GCC Release/RelWithDebInfo builds (turn \
OFF for wheel builds; GCC 14's LTO ICEs on bvn_init_run_luts under manylinux)"
    ON)
option(BVNR_BUILD_TESTS
    "Build and register the C/Python test suite"
    ON)

set(BVNR_C_FLAGS
    -std=c99
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wcast-qual
    -Wconversion
    -Wno-override-init
    -Wno-missing-field-initializers
    -pedantic
    $<$<AND:$<BOOL:${BVNR_ANALYZER}>,$<C_COMPILER_ID:GNU>>:-fanalyzer>
    -fPIC
    -fvisibility=hidden
    # bvnr_source_t / bvnr_sink_t are opaque storage that the
    # library reinterprets via an internal impl struct.  Strict
    # aliasing would mis-optimise those accesses, so disable it.
    # NOTE: this only protects *this* build.  The impl structs are
    # also tagged __may_alias__ (see src/io/bvn_io_impl.h) so the
    # single-file amalgamation stays correct when a consumer compiles
    # dist/bovnar.c at -O2/-O3 without this flag.
    -fno-strict-aliasing
)
if(BVNR_HARDEN)
    # -Wformat=2 / -Wformat-security: compile-time only, zero runtime cost.
    list(APPEND BVNR_C_FLAGS
        -Wformat=2
        -Wformat-security
    )
    # FORTIFY_SOURCE: cheap runtime checks on libc string/io calls; needs
    # optimisation to be active, so silently no-op in Debug.
    list(APPEND BVNR_C_FLAGS
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
    # Stack protector: ~10-15% slowdown on the parser hot path because the
    # state-machine functions touch local arrays.  Apply it only to the
    # CLI executable (where user input enters via argv / files) instead
    # of the inner library translation units — see the bovnar target below.
endif()
if(BVNR_WERROR)
    list(APPEND BVNR_C_FLAGS -Werror)
endif()

# LTO tuning uses GCC-only spellings (-flto=auto, -fno-semantic-interposition).
# Other compilers — notably AppleClang for the macOS cibuildwheel wheels — get a
# plain optimised build so the same tree builds everywhere. The GCC path is
# unchanged.
if(BVNR_LTO AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(BVNR_LTO_CFLAGS " -flto=auto -fno-semantic-interposition")
else()
    set(BVNR_LTO_CFLAGS "")
endif()

set(CMAKE_C_FLAGS_DEBUG          "-O0 -g3" CACHE STRING "C flags for Debug"           FORCE)
set(CMAKE_C_FLAGS_RELEASE        "-O3 -g0${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for Release"         FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -g0" CACHE STRING "C flags for MinSizeRel"      FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g3${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for RelWithDebInfo"  FORCE)

if(BVNR_LTO
        AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=auto")
endif()

set(BVNR_SOURCES_LEXER
    src/lexer/bovnar_lexer.c
    src/lexer/bovnar_state_table.c
)

set(BVNR_SOURCES_VALIDATOR
    src/validator/bovnar_validator.c
)

set(BVNR_SOURCES_WRITER
    src/writer/bovnar_writer.c
    src/writer/bovnar_write_utils.c
    src/writer/bovnar_canon_observer.c
)

set(BVNR_SOURCES_IO
    src/io/bovnar_io.c
)

set(BVNR_SOURCES_DOM
    src/dom/bovnar_dom.c
    src/dom/bovnar_dom_builder.c
)

set(BVNR_SOURCES_STREAM
    src/stream/bovnar_stream.c
)

set(BVNR_SOURCES_UTILS
    src/utils/bovnar_utils.c
    src/utils/bovnar_si_units.c
    src/utils/bovnar_currency.c
    src/utils/bvn_int.c
    src/utils/bvn_float.c
    src/utils/bvn_gregorian_date.c
    src/utils/bvn_datetime.c
)

set(BVNR_SOURCES
    ${BVNR_SOURCES_LEXER}
    ${BVNR_SOURCES_VALIDATOR}
    ${BVNR_SOURCES_WRITER}
    ${BVNR_SOURCES_IO}
    ${BVNR_SOURCES_DOM}
    ${BVNR_SOURCES_STREAM}
    ${BVNR_SOURCES_UTILS}
)

add_library(bvnr_objects OBJECT ${BVNR_SOURCES})
target_compile_options(bvnr_objects PRIVATE ${BVNR_C_FLAGS})
target_include_directories(bvnr_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

add_library(bvnr_static STATIC $<TARGET_OBJECTS:bvnr_objects>)
target_include_directories(bvnr_static
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bvnr_static PUBLIC m)

add_library(bvnr_shared SHARED $<TARGET_OBJECTS:bvnr_objects>)
target_compile_options(bvnr_shared PRIVATE ${BVNR_C_FLAGS})
target_include_directories(bvnr_shared
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bvnr_shared PUBLIC m)

add_executable(bovnar src/bovnar.c)
target_compile_options(bovnar PRIVATE ${BVNR_C_FLAGS})
if(BVNR_HARDEN)
    # Stack protector is applied to the CLI binary's own TU (where argv
    # and stdin are first touched).  Library TUs are exempt so the
    # parser hot path stays fast.
    target_compile_options(bovnar PRIVATE -fstack-protector-strong)
endif()
set_target_properties(bovnar PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_options(bovnar PRIVATE -pie)
target_link_libraries(bovnar PRIVATE bvnr_static m)

# The test suite spins up dozens of executables at configure time and pulls in
# CTest/GMP/pytest wiring.  Skip it when building the Python wheel
# (scikit-build-core defines SKBUILD) so `pip install` only compiles the lib.
if(BVNR_BUILD_TESTS AND NOT SKBUILD)
    include(CMakeLists_tests.txt)
    # Python test registration lives entirely in CMakeLists_tests.txt
    # (granular per-file targets with proper labels and library injection).
endif()

# When built as a Python wheel, install the shared library directly into the
# `bovnar` package directory so the ctypes loader (_ffi.py) finds it in-place,
# with no LIBBOVNAR_PATH or system install required.
if(SKBUILD)
    install(TARGETS bvnr_shared
        LIBRARY DESTINATION bovnar    # libbvnr_shared.so / .dylib
        RUNTIME DESTINATION bovnar)   # bvnr_shared.dll (Windows)
endif()

# System integration for the Bovnar media type (text/vnd.bovnar) and the .bvnr
# extension. Skipped for the Python wheel build (SKBUILD). After `make install`
# run `update-mime-database <datadir>/mime` to refresh the desktop database.
if(NOT SKBUILD)
    include(GNUInstallDirs)
    option(BVNR_INSTALL_MIME
        "Install the freedesktop shared-mime-info entry + file(1) magic for *.bvnr"
        ON)
    if(BVNR_INSTALL_MIME)
        install(FILES dist/mime/text-vnd.bovnar.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
        install(FILES dist/mime/bovnar.magic
            DESTINATION ${CMAKE_INSTALL_DATADIR}/bovnar
            RENAME magic)
    endif()
endif()
