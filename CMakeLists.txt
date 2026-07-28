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
OFF for wheel builds; GCC 14's LTO ICEs on bvn_init_run_luts under manylinux, \
and GCC 16's does the same under MinGW — that one is forced OFF automatically, \
see the MINGW guard further down)"
    ON)
option(BVNR_BUILD_TESTS
    "Build and register the C/Python test suite"
    ON)
# Declared HERE, with the other options, not next to the packaging rules further
# down: CMakeLists_tests.txt is included well before that point, and a test that
# guards on BVNR_PACKAGE was silently never registered because the variable did
# not exist yet. An undefined variable is simply false in an if().
option(BVNR_PACKAGE
    "On each build, regenerate the amalgamation and pack release archives into the build dir"
    ON)
option(BVNR_SANITIZE
    "Build with AddressSanitizer + UndefinedBehaviorSanitizer and pattern-fill \
uninitialized stack variables (GCC/Clang only; dev/CI diagnostic, never a \
release build). Note the Python ctypes tests need the sanitizer runtime \
preloaded -- see the note at the option's definition"
    OFF)
option(BVNR_REGEN_TABLES
    "Force-regenerate the unit + prefix + currency tables from src/gendata/*.bvnr. \
The *.gen.inc snippets are build artifacts in <build>/generated (not committed): \
they are regenerated automatically when missing, and unconditionally when this is \
ON. Generation uses a small built-in reader and needs only Python 3 (no bovnar)."
    OFF)

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
    if(MINGW)
        # MinGW's <math.h> isfinite/isinf/signbit are macros whose float-typed
        # branch contains a double->float cast; -Wfloat-conversion (pulled in by
        # -Wconversion) flags it at our call sites. glibc uses true builtins, so
        # the Linux build is clean. The cast lives in the system macro, not our
        # code, so drop just this sub-warning while keeping the rest of
        # -Wconversion identical to the Linux build. Must come AFTER -Wconversion,
        # so it lives in BVNR_C_FLAGS rather than the global block below.
        list(APPEND BVNR_C_FLAGS -Wno-float-conversion)
    endif()
endif()
if(BVNR_WERROR)
    if(MSVC)
        list(APPEND BVNR_C_FLAGS /WX)
    else()
        list(APPEND BVNR_C_FLAGS -Werror)
        # -Wno-error=analyzer-null-dereference used to sit here. -fanalyzer had
        # lost the invariant that bigint_ensure_cap() guarantees a non-NULL limbs
        # pointer when it returns true, and warned on the three configurations
        # that do not optimise hard enough to propagate it for the analyzer (no
        # CMAKE_BUILD_TYPE, Debug, MinSizeRel) -- so BVNR_WERROR, documented "for
        # CI", could not build in three of five. bigint_ensure_cap now states
        # that invariant itself rather than leaving it to be inferred (it returns
        # false instead of promising capacity for a buffer that is not there),
        # and bigint_addmag_into restates it at the point that depends on it, so
        # the diagnostic is gone from every configuration and the exemption with
        # it -- verified across all five.
        #
        # This does mean a heuristic can now fail a BVNR_WERROR build. That is
        # the intended trade: if a future GCC invents a new false positive here,
        # make the invariant explicit the way those two did, or put an exemption
        # back for that specific diagnostic -- do not turn BVNR_ANALYZER off.
    endif()
endif()

# Sanitizer build. Kept as a first-class option rather than a remembered shell
# incantation, because these runs are only useful if they are repeatable.
#
#   cmake -S . -B build-asan -DBVNR_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
#   ctest --test-dir build-asan -E '^bvnr_py_'
#
# -fno-sanitize-recover=all makes a diagnostic a failure instead of a log line.
# -ftrivial-auto-var-init=pattern is not a sanitizer: it fills uninitialized
# automatic variables with a conspicuous pattern instead of reporting them. GCC
# has no MemorySanitizer, and this is the closest available substitute -- it
# cannot flag such a read, but it does make one produce a distinctive value
# rather than whatever the stack happened to hold, so a test whose result
# depended on one will change its answer. It says nothing about uninitialized
# HEAP memory; closing that class properly needs clang's MSan.
#
# The bvnr_py_* tests load libbvnr.so through ctypes into an unsanitized
# interpreter, which ASan refuses ("runtime does not come first"). Exclude them,
# or preload the runtime:
#   LD_PRELOAD=$(gcc -print-file-name=libasan.so) ASAN_OPTIONS=detect_leaks=0 \
#       LIBBOVNAR_PATH=<build>/libbvnr.so python3 -m pytest python
if(BVNR_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "BVNR_SANITIZE is GCC/Clang only")
    endif()
    set(BVNR_SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all
                       -fno-omit-frame-pointer -g)
    include(CheckCCompilerFlag)
    check_c_compiler_flag(-ftrivial-auto-var-init=pattern BVNR_HAVE_AUTO_VAR_INIT)
    if(BVNR_HAVE_AUTO_VAR_INIT)
        list(APPEND BVNR_SAN_FLAGS -ftrivial-auto-var-init=pattern)
    endif()
    list(APPEND BVNR_C_FLAGS ${BVNR_SAN_FLAGS})
    add_link_options(-fsanitize=address,undefined)
    if(BVNR_HAVE_AUTO_VAR_INIT)
        message(STATUS "BVNR_SANITIZE: ASan+UBSan + pattern auto-var-init")
    else()
        message(STATUS "BVNR_SANITIZE: ASan+UBSan "
                       "(compiler has no -ftrivial-auto-var-init)")
    endif()
endif()

# MinGW printf maps onto the CRT, whose msvcrt printf lacks the C99 conversions
# (%zu, %lld, ...). Apply these to EVERY translation unit (library, CLI, and the
# tests — including the fuzz harnesses that use their own warning set) so the
# choice of stdio implementation is uniform:
#   * __USE_MINGW_ANSI_STDIO=1 selects MinGW's own ANSI-stdio implementation,
#     which handles %zu/%lld correctly at runtime.
#   * -fno-builtin-{printf,fprintf,snprintf} stops GCC from format-checking these
#     calls against its ms_printf builtin archetype; with the builtins off it
#     uses the header's gnu_printf attribute instead, so %zu/%lld no longer warn
#     under -Wformat even though they were already correct at runtime. Only
#     printf/fprintf/snprintf are used in the tree.
# Order-independent (these flags are never overridden), so the global scope is
# fine; unlike -Wno-float-conversion which must follow -Wconversion.
if(MINGW)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
    add_compile_options(-fno-builtin-printf -fno-builtin-fprintf -fno-builtin-snprintf)
endif()

# GCC's LTO inliner has now crashed twice on the same function. bvn_init_run_luts
# is the lexer's pre-main constructor: it calls bvn_build_run_lut nine times, and
# each of those walks all 256 byte values, so inlining them all gives the inliner
# one function built out of nine 256-iteration loops. GCC 14 fell over on it under
# manylinux — that is the wheel-build note on the BVNR_LTO option above, and why
# pyproject.toml passes BVNR_LTO=OFF — and GCC 16.1 now does the same in the
# MinGW64 CI job:
#
#     during IPA pass: inline
#     In function 'bvn_init_run_luts':
#     lto1.exe: internal compiler error: in copy_bb, at tree-inline.cc:2418
#
# The source is valid C and builds clean at -O3 without LTO on every compiler we
# target, so there is nothing to repair in the lexer; the crash is in the
# compiler. What this can do is stop handing that toolchain the input it chokes
# on. Scoped to MinGW rather than to GCC generally because the native GNU builds
# (including the Linux release artifacts) are unaffected and keep their LTO.
if(BVNR_LTO AND MINGW)
    message(STATUS
        "BVNR_LTO: disabled for MinGW (GCC LTO ICEs on bvn_init_run_luts)")
    set(BVNR_LTO OFF)
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
# These FORCE-overrides replace CMake's per-config defaults wholesale, so -DNDEBUG
# has to be spelled out for the optimised configurations — CMake supplies it in
# its own Release/MinSizeRel/RelWithDebInfo defaults, and dropping it silently
# left assert() live in the shipped libbvnr. An assert in a library aborts the
# HOST process, which is never an acceptable answer to a caller's bad argument;
# the library returns errors for those instead. Debug keeps asserts on purpose.
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG          "-O0 -g3" CACHE STRING "C flags for Debug"           FORCE)
    set(CMAKE_C_FLAGS_RELEASE        "-O3 -g0 -DNDEBUG${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for Release"         FORCE)
    set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -g0 -DNDEBUG" CACHE STRING "C flags for MinSizeRel"      FORCE)
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g3 -DNDEBUG${BVNR_LTO_CFLAGS}" CACHE STRING "C flags for RelWithDebInfo"  FORCE)
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
    src/utils/bovnar_profiles.c
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
# The generated *.gen.inc snippets are build artifacts (not committed); they are
# produced into BVNR_GENERATED_DIR by the generation step below and found via the
# private include path here. The public *.gen.h enum headers stay in include/.
set(BVNR_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BVNR_GENERATED_DIR})
target_include_directories(bvnr_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${BVNR_GENERATED_DIR}
)

# Generate the unit/prefix/currency tables. The *.gen.inc snippets are build
# artifacts under BVNR_GENERATED_DIR; the *.gen.h enum bodies are committed
# public headers under include/. Either way they must exist and be current before
# the library compiles. The generators parse the src/gendata/*.bvnr documents
# with a small built-in reader (bvnr_data.py) — only Python 3 is needed, no
# bovnar library.
set(BVNR_GEN_INPUTS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/units.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/prefixes.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/currencies.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/ucum.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/unece.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/qudt.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/qudt-qk.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gendata/udunits.bvnr
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_units.py
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_prefixes.py
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_currencies.py
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_profiles.py
    ${CMAKE_CURRENT_SOURCE_DIR}/bvnr_data.py)
set(BVNR_GEN_OUTPUTS
    ${BVNR_GENERATED_DIR}/bovnar_si_conv_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_base_unit_str.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_bu_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_bu_index.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_prefix_policy.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_compact_except.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_si_pfx_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_iec_pfx_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_si_prefix_str.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_iec_prefix_str.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_si_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_iec_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_currency_table.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_ucum_prefix.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_ucum_atom.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_ucum_unsupported.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profiles_conv.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profiles_policy.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profiles_str.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_ucum_reverse.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_unece_atom.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_unece_unsupported.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_unece_reverse.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_atom.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_unsupported.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_reverse.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_qk_atom.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_qk_unsupported.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_qudt_qk_reverse.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_udunits_prefix.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_udunits_atom.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_udunits_unsupported.gen.inc
    ${BVNR_GENERATED_DIR}/bovnar_profile_udunits_reverse.gen.inc
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bovnar_profiles.gen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bovnar_units.gen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bovnar_si_prefix.gen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bovnar_iec_prefix.gen.h)

# Editing a gendata document or a generator must re-run CMake, so the staleness
# check below actually gets a chance to see the change. Without this an existing
# build directory happily compiles yesterday's tables: an edit to units.bvnr —
# correcting a conversion factor, say — would silently never reach the library.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BVNR_GEN_INPUTS})

# Regenerate when forced, when an output is missing, or when any input is at
# least as new as any output. IS_NEWER_THAN is true for equal timestamps too, so
# this errs toward regenerating — cheap (three short Python runs) and always
# correct, where erring the other way ships a stale table.
set(_bvnr_regen ${BVNR_REGEN_TABLES})
if(NOT _bvnr_regen)
    foreach(_out IN LISTS BVNR_GEN_OUTPUTS)
        if(NOT EXISTS ${_out})
            set(_bvnr_regen ON)
            break()
        endif()
        foreach(_in IN LISTS BVNR_GEN_INPUTS)
            if(${_in} IS_NEWER_THAN ${_out})
                set(_bvnr_regen ON)
                break()
            endif()
        endforeach()
        if(_bvnr_regen)
            break()
        endif()
    endforeach()
endif()

if(_bvnr_regen)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    foreach(_gen gen_units gen_prefixes gen_currencies gen_profiles)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                    BVNR_GENERATED_DIR=${BVNR_GENERATED_DIR}
                    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${_gen}.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE _bvnr_gen_rc)
        if(NOT _bvnr_gen_rc EQUAL 0)
            message(FATAL_ERROR
                "Failed to generate the unit/prefix/currency/UCUM tables (${_gen}.py). "
                "These need only Python 3 — check that ${Python3_EXECUTABLE} runs.")
        endif()
    endforeach()
endif()

# Both libraries share the base name "bvnr", differentiated only by extension:
# the static archive is libbvnr.a and the shared object is libbvnr.so (the CMake
# target names stay distinct because two targets cannot share a name).
# Standard install layout (CMAKE_INSTALL_INCLUDEDIR/LIBDIR/BINDIR). Needed here so
# the exported targets below can carry an $<INSTALL_INTERFACE:> include path.
include(GNUInstallDirs)

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
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(bvnr_shared SHARED $<TARGET_OBJECTS:bvnr_objects>)
set_target_properties(bvnr_shared PROPERTIES OUTPUT_NAME bvnr)
# Clean names for the exported CMake package: bovnar::bvnr / bovnar::bvnr_static.
set_target_properties(bvnr_shared PROPERTIES EXPORT_NAME bvnr)
set_target_properties(bvnr_static PROPERTIES EXPORT_NAME bvnr_static)
# Export every API symbol from the DLL the same way the ELF/Mach-O builds export
# every extern symbol. The shared OBJECT library is also linked into the static
# archive, so a baked-in __declspec(dllexport) cannot be used (it would be wrong
# for the static objects); CMake's auto-export generates the .def instead. All
# public symbols are functions, which this exports cleanly.
set_target_properties(bvnr_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
target_compile_options(bvnr_shared PRIVATE ${BVNR_C_FLAGS})
target_include_directories(bvnr_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Self-contained DLL on MinGW: fold in the GCC runtime so the shipped library
# has no libgcc_s_seh / libwinpthread sidecar dependency.
if(MINGW)
    target_link_options(bvnr_shared PRIVATE -static-libgcc -static)
endif()

# Versioned soname for system/distro installs so the dynamic loader can refuse an
# ABI mismatch — e.g. a consumer compiled against 1.0 headers (before a public
# by-value struct such as bvnr_data_t grew) run against a newer shared library.
# SOVERSION is the ABI-compatibility major; bump it on any incompatible ABI change.
#
# 2 — the read-time unit conversion work grew bvnr_data_t (`converted` + a
#     bvnr_converted_t carrier) and reclaimed a bvnr_read_flags_t reserved slot.
#     Both are passed by value across the ABI, so a consumer built against the
#     1.x headers reads them at the wrong size. Leaving this at 1 would let the
#     loader hand such a binary the new libbvnr.so.1 and corrupt memory instead
#     of failing cleanly, which is precisely what the versioned soname is for.
#
# The wheel (SKBUILD) deliberately keeps the plain unversioned filename: _ffi.py
# loads libbvnr.so by exact name via ctypes, and wheel archives do not preserve
# the symlink chain a versioned soname creates.
#
# VERSION tracks the ABI, NOT ${PROJECT_VERSION}. The project version is the
# FORMAT version (see the CHANGELOG: "semantic versioning of the format"), which
# is deliberately still 1.1.0 here -- the wire format did not change, only the
# ABI did. Deriving the real filename from it produced libbvnr.so.1.1.0 for BOTH
# SOVERSION 1 and SOVERSION 2, so the two mutually incompatible libraries had the
# same on-disk name and would overwrite each other; a binary linked against
# libbvnr.so.1 would then be handed a file that is actually ABI 2 -- exactly the
# failure the versioned soname exists to prevent. Keying VERSION to the ABI major
# gives libbvnr.so.2.0.0, so both can coexist and the loader resolves by SONAME.
set(BVNR_ABI_MAJOR 2)
if(NOT SKBUILD)
    set_target_properties(bvnr_shared PROPERTIES
        VERSION   ${BVNR_ABI_MAJOR}.0.0
        SOVERSION ${BVNR_ABI_MAJOR})
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
# On Windows (incl. the MinGW cross build) the portable subset still builds and
# runs: CMakeLists_tests.txt guards the POSIX-only pieces (socketpair+pthread
# round-trip, fork/exec IUT mode, sigaction fuzz crash handler) and the Python
# bindings (host interpreter cannot load a PE DLL) behind NOT WIN32. The
# cross-built test exes run under Wine via CMAKE_CROSSCOMPILING_EMULATOR (set by
# the toolchain file). The native MSVC CI still passes -DBVNR_BUILD_TESTS=OFF.
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
        LIBRARY DESTINATION bovnar    # libbvnr.so / libbvnr.dylib
        RUNTIME DESTINATION bovnar)   # bvnr.dll (Windows)
endif()

# System / distro install of the C library, the CLI, the public headers, a CMake
# package (find_package(bovnar) -> bovnar::bvnr / bovnar::bvnr_static) and a
# pkg-config file (pkg-config --cflags --libs bvnr). Skipped for the Python wheel
# build, which installs only the bundled shared library above. GNUInstallDirs was
# already pulled in next to the targets so the $<INSTALL_INTERFACE:> paths resolve.
if(NOT SKBUILD)
    include(CMakePackageConfigHelpers)

    install(TARGETS bvnr_shared bvnr_static
        EXPORT bovnarTargets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}   # bvnr.dll on Windows
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(TARGETS bovnar
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES
        include/bovnar.h
        include/bovnar_units.gen.h
        include/bovnar_si_prefix.gen.h
        include/bovnar_iec_prefix.gen.h
        include/bovnar_currency.h
        include/bovnar_dom.h
        include/bovnar_si_units.h
        include/bovnar_stream.h
        include/bvn_datetime.h
        include/bvn_float.h
        include/bvn_gregorian_date.h
        include/bvn_int.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT bovnarTargets
        FILE      bovnarTargets.cmake
        NAMESPACE bovnar::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bovnar)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bovnarConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/bovnarConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bovnar)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/bovnarConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/bovnarConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/bovnarConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bovnar)

    # Relative hop from <libdir>/pkgconfig back to the prefix, so the .pc can
    # derive its own prefix from ${pcfiledir} instead of baking in the
    # configure-time CMAKE_INSTALL_PREFIX.
    #
    # Counted by string surgery -- one ".." per path component -- rather than
    # file(RELATIVE_PATH <libdir>/pkgconfig "/") rooted at "/". That command
    # demands a *full* path for both arguments, and on Windows a bare "/" is not
    # one, so the rooted form aborted configure outright ("file RELATIVE_PATH
    # must be passed a full path to the file: /") on MSVC and MinGW alike --
    # before a single object was compiled. The .pc is a Unix artefact nothing on
    # Windows consumes, but this block is not guarded and so has to at least
    # *evaluate* there. Component counting gives the identical answer on every
    # platform (lib -> ../.., lib/x86_64-linux-gnu -> ../../..) and reaches for
    # no filesystem semantics to do it.
    string(REGEX REPLACE "/+" "/"
           _bvnr_pc_dir "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    string(REGEX REPLACE "^/|/$" "" _bvnr_pc_dir "${_bvnr_pc_dir}")
    string(REGEX REPLACE "[^/]+" ".." BVNR_PC_RELDIR "${_bvnr_pc_dir}")
    if(BVNR_PC_RELDIR STREQUAL "")
        set(BVNR_PC_RELDIR ".")
    endif()

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bovnar.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/bvnr.pc
        @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bvnr.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

# System integration for the Bovnar media type (text/vnd.bovnar) and the .bvnr
# extension. Skipped for the Python wheel build (SKBUILD). After `make install`
# run `update-mime-database <datadir>/mime` to refresh the desktop database.
if(NOT SKBUILD)
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

# Convenience: also drive the MinGW-w64 Windows cross-build as part of the
# default ("all") target, so a single "build all" on a Linux host that has the
# cross toolchain installed produces the Windows artifacts (build/mingw/) too.
# This is wired ON for the VSCode workspace (.vscode/settings.json passes
# -DBVNR_CROSS_MINGW=ON) and is OFF by default everywhere else, so CI, the
# Python wheel build, and plain command-line builds are unaffected. It is a
# no-op (with a STATUS message) when the toolchain is not installed, and the
# NOT CMAKE_CROSSCOMPILING guard stops the cross build from recursing into
# itself. See cmake/toolchain-mingw-w64-x86_64.cmake.
option(BVNR_CROSS_MINGW
    "On a Linux host, also build the MinGW-w64 Windows target into build/mingw/ \
as part of 'all' when the cross toolchain is installed"
    OFF)
if(BVNR_CROSS_MINGW
        AND NOT CMAKE_CROSSCOMPILING
        AND NOT WIN32
        AND NOT SKBUILD
        AND CMAKE_HOST_UNIX)
    find_program(BVNR_MINGW_GCC x86_64-w64-mingw32-gcc)
    if(BVNR_MINGW_GCC)
        include(ExternalProject)
        # Mirror the host build's config into the cross build (Release if the
        # host build is config-less, e.g. a single-config generator with no
        # CMAKE_BUILD_TYPE set).
        set(_bvnr_cross_build_type "${CMAKE_BUILD_TYPE}")
        if(NOT _bvnr_cross_build_type)
            set(_bvnr_cross_build_type Release)
        endif()
        ExternalProject_Add(bvnr_mingw_cross
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/mingw
            CMAKE_ARGS
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-mingw-w64-x86_64.cmake
                -DCMAKE_BUILD_TYPE=${_bvnr_cross_build_type}
                # Build the portable test subset too; it runs under Wine via the
                # toolchain's CMAKE_CROSSCOMPILING_EMULATOR (ctest -C in build/mingw).
                -DBVNR_BUILD_TESTS=${BVNR_BUILD_TESTS}
                -DBVNR_CROSS_MINGW=OFF
                # The outer (native) build packs the cross artifacts from
                # build/mingw; the inner build must not recurse into packaging.
                -DBVNR_PACKAGE=OFF
            BUILD_ALWAYS TRUE       # pick up source edits on every "build all"
            INSTALL_COMMAND ""      # cross artifacts stay in build/mingw/
            USES_TERMINAL_CONFIGURE TRUE
            USES_TERMINAL_BUILD TRUE)
        message(STATUS
            "BVNR_CROSS_MINGW: cross toolchain found (${BVNR_MINGW_GCC}); "
            "'all' will also build the Windows target into build/mingw/")
    else()
        message(STATUS
            "BVNR_CROSS_MINGW is ON but x86_64-w64-mingw32-gcc was not found; "
            "skipping the Windows cross-build "
            "(install the mingw-w64 toolchain to enable it)")
    endif()
endif()

# On every build, regenerate the single-file amalgamation into
# <build>/amalgamate and pack release archives into <build>:
#   bovnar-<version>-<count>-amalgamate.tar.xz   (the amalgamation)
#   bovnar-linux-<version>-<count>.tar.xz        (Linux artifacts)
#   bovnar-windows-<version>-<count>.zip         (native or cross MinGW artifacts)
# The platform archives also bundle doc/, examples/ and highlighter/.
# <count> is the git commit count. The platform archive matches the current
# build; on a Linux host the Windows .zip is packed from the BVNR_CROSS_MINGW
# build (build/mingw) when present. Skipped for the Python wheel build (SKBUILD)
# and disabled in the inner cross build (BVNR_PACKAGE=OFF above). See
# cmake/pack_artifacts.cmake.
if(BVNR_PACKAGE AND NOT SKBUILD)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(_bvnr_pkg_py "")
    if(Python3_FOUND)
        set(_bvnr_pkg_py "${Python3_EXECUTABLE}")
    endif()
    if(MSVC)
        set(_bvnr_pkg_kind msvc)
    elseif(WIN32)
        set(_bvnr_pkg_kind mingw)
    else()
        set(_bvnr_pkg_kind linux)
    endif()
    # On a Linux host the BVNR_CROSS_MINGW superbuild drops Windows artifacts in
    # build/mingw; point the packer at them so it can also emit the .zip.
    set(_bvnr_pkg_cross "")
    if(BVNR_CROSS_MINGW AND NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
        set(_bvnr_pkg_cross "${CMAKE_CURRENT_SOURCE_DIR}/build/mingw")
    endif()
    add_custom_target(bvnr_package ALL
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBIN_DIR=${CMAKE_BINARY_DIR}
            -DVERSION=${PROJECT_VERSION}
            -DPYTHON=${_bvnr_pkg_py}
            -DNATIVE_KIND=${_bvnr_pkg_kind}
            -DCROSS_DIR=${_bvnr_pkg_cross}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pack_artifacts.cmake
        VERBATIM
        COMMENT "Regenerating amalgamation + packing release archives")
    # Pack only after the artifacts exist.
    add_dependencies(bvnr_package bvnr_static bvnr_shared bovnar)
    if(TARGET bvnr_mingw_cross)
        add_dependencies(bvnr_package bvnr_mingw_cross)
    endif()
endif()
