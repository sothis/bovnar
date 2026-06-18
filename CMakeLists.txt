cmake_minimum_required(VERSION 3.21)
project(bovnar VERSION 1.1.0 LANGUAGES C)

# Windows builds are 64-bit only (MinGW64 and MSVC x64). CMAKE_SIZEOF_VOID_P is
# populated by the compiler probe that project() just ran.
if(WIN32 AND CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "Bovnar on Windows supports 64-bit builds only "
        "(detected ${CMAKE_SIZEOF_VOID_P}-byte pointers — use an x64 toolchain).")
endif()

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

if(MSVC)
    # MSVC (x64). /utf-8 so the UTF-8 string literals in the sources (unit
    # symbols such as "·"/"µ") compile correctly regardless of the active code
    # page; the _CRT_*_NO_WARNINGS defines silence the deprecation of the POSIX
    # CRT names (open/read/write/close) that the bvn_port.h shim maps, rather
    # than littering the source with underscores.
    set(BVNR_C_FLAGS
        /std:c11        # MSVC has no /std:c99; c11 enables the C99 features the
                        # sources use (mixed declarations, designated inits).
        /W3
        /utf-8
        /D_CRT_SECURE_NO_WARNINGS
        /D_CRT_NONSTDC_NO_WARNINGS
    )
else()
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
endif()
if(BVNR_WERROR)
    if(MSVC)
        list(APPEND BVNR_C_FLAGS /WX)
    else()
        list(APPEND BVNR_C_FLAGS -Werror)
    endif()
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

# These are GCC/Clang flag spellings; on MSVC let CMake supply its own
# per-config defaults (/Od /Zi, /O2, etc.).
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG          "-O0 -g3" CACHE STRING "C flags for Debug"           FORCE)
    set(CMAKE_C_FLAGS_RELEASE        "-O3 -g0${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for Release"         FORCE)
    set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -g0" CACHE STRING "C flags for MinSizeRel"      FORCE)
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g3${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for RelWithDebInfo"  FORCE)
endif()

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

# Both libraries share the base name "bvnr", differentiated only by extension:
# the static archive is libbvnr.a and the shared object is libbvnr.so (the CMake
# target names stay distinct because two targets cannot share a name).
add_library(bvnr_static STATIC $<TARGET_OBJECTS:bvnr_objects>)
# On MSVC the static archive and the DLL's import library are both "<name>.lib",
# so they would collide if both were named "bvnr". Give the static archive a
# distinct name there. MinGW (libbvnr.a vs the libbvnr.dll.a import lib) and
# POSIX (.a vs .so) disambiguate by extension, so they keep the unified "bvnr".
if(MSVC)
    set_target_properties(bvnr_static PROPERTIES OUTPUT_NAME bvnr_static)
else()
    set_target_properties(bvnr_static PROPERTIES OUTPUT_NAME bvnr)
endif()
target_include_directories(bvnr_static
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(bvnr_shared SHARED $<TARGET_OBJECTS:bvnr_objects>)
set_target_properties(bvnr_shared PROPERTIES OUTPUT_NAME bvnr)
# Export every API symbol from the DLL the same way the ELF/Mach-O builds export
# every extern symbol. The shared OBJECT library is also linked into the static
# archive, so a baked-in __declspec(dllexport) cannot be used (it would be wrong
# for the static objects); CMake's auto-export generates the .def instead. All
# public symbols are functions, which this exports cleanly.
set_target_properties(bvnr_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
target_compile_options(bvnr_shared PRIVATE ${BVNR_C_FLAGS})
target_include_directories(bvnr_shared
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Self-contained DLL on MinGW: fold in the GCC runtime so the shipped library
# has no libgcc_s_seh / libwinpthread sidecar dependency.
if(MINGW)
    target_link_options(bvnr_shared PRIVATE -static-libgcc -static)
endif()

# Versioned soname for system/distro installs so the dynamic loader can refuse an
# ABI mismatch — e.g. a consumer compiled against 1.0 headers (before a public
# by-value struct such as bvnr_data_t grew) run against a newer shared library.
# SOVERSION is the ABI-compatibility major; bump it on any incompatible ABI change.
# The wheel (SKBUILD) deliberately keeps the plain unversioned filename: _ffi.py
# loads libbvnr.so by exact name via ctypes, and wheel archives do not preserve
# the symlink chain a versioned soname creates.
if(NOT SKBUILD)
    set_target_properties(bvnr_shared PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION 1)
endif()

add_executable(bovnar src/bovnar.c)
target_compile_options(bovnar PRIVATE ${BVNR_C_FLAGS})
# The CLI TU includes the internal portability shim (bvn_port.h, src/utils).
target_include_directories(bovnar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/utils)
if(BVNR_HARDEN AND NOT MSVC)
    # Stack protector is applied to the CLI binary's own TU (where argv
    # and stdin are first touched).  Library TUs are exempt so the
    # parser hot path stays fast.  MSVC enables /GS by default.
    target_compile_options(bovnar PRIVATE -fstack-protector-strong)
endif()
set_target_properties(bovnar PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
    # -pie is an ELF executable concept; not applicable to PE/Windows.
    target_link_options(bovnar PRIVATE -pie)
endif()
if(MINGW)
    target_link_options(bovnar PRIVATE -static-libgcc -static)
endif()
target_link_libraries(bovnar PRIVATE bvnr_static)

# The test suite spins up dozens of executables at configure time and pulls in
# CTest/pytest wiring.  Skip it when building the Python wheel
# (scikit-build-core defines SKBUILD) so `pip install` only compiles the lib.
#
# It is also skipped on Windows: the harness is POSIX (the conformance driver
# forks/execs the IUT, the round-trip test uses socketpair + pthread, the fuzz
# runner uses sigaction). The Windows build target is the library + DLL + CLI;
# the Windows CI smoke-tests the CLI end-to-end (which drives the full
# read/write/DOM paths). Porting the test harness to Windows is tracked
# separately.
if(BVNR_BUILD_TESTS AND NOT SKBUILD AND NOT WIN32)
    include(CMakeLists_tests.txt)
    # Python test registration lives entirely in CMakeLists_tests.txt
    # (granular per-file targets with proper labels and library injection).
endif()

# When built as a Python wheel, install the shared library directly into the
# `bovnar` package directory so the ctypes loader (_ffi.py) finds it in-place,
# with no LIBBOVNAR_PATH or system install required.
if(SKBUILD)
    install(TARGETS bvnr_shared
        LIBRARY DESTINATION bovnar    # libbvnr.so / libbvnr.dylib
        RUNTIME DESTINATION bovnar)   # bvnr.dll (Windows)
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
