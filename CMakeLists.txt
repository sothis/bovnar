cmake_minimum_required(VERSION 3.21)
project(bovnar C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(BVNR_C_FLAGS
    -std=c99
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wcast-qual
    -Wconversion
    -Wno-override-init
    -pedantic
    $<$<C_COMPILER_ID:GNU>:-fanalyzer>
    -fPIC
    -fvisibility=hidden
)

set(CMAKE_C_FLAGS_DEBUG          "-O0 -g3" CACHE STRING "C flags for Debug"           FORCE)
set(CMAKE_C_FLAGS_RELEASE        "-O3 -g0 -flto=auto -fno-semantic-interposition" CACHE STRING "C flags for Release"         FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -g0" CACHE STRING "C flags for MinSizeRel"      FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g3 -flto=auto -fno-semantic-interposition" CACHE STRING "C flags for RelWithDebInfo"  FORCE)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
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

set(BVNR_SOURCES_UTILS
    src/utils/bovnar_utils.c
    src/utils/bovnar_si_units.c
    src/utils/bovnar_currency.c
    src/utils/bvn_int.c
    src/utils/bvn_float.c
)

set(BVNR_SOURCES
    ${BVNR_SOURCES_LEXER}
    ${BVNR_SOURCES_VALIDATOR}
    ${BVNR_SOURCES_WRITER}
    ${BVNR_SOURCES_IO}
    ${BVNR_SOURCES_DOM}
    ${BVNR_SOURCES_UTILS}
)

add_library(bvnr_objects OBJECT ${BVNR_SOURCES})
target_compile_options(bvnr_objects PRIVATE ${BVNR_C_FLAGS})
target_include_directories(bvnr_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
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
target_link_libraries(bovnar PRIVATE bvnr_static m)

include(CMakeLists_tests.txt)

option(BVNR_BUILD_PYTHON_TESTS "Build and register Python tests" OFF)

if(BVNR_BUILD_PYTHON_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(PYTHON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/python")
    if(DEFINED ENV{PYTHONPATH})
        set(PYTHON_PATH "${PYTHON_PATH}:$ENV{PYTHONPATH}")
    endif()

    add_test(NAME bvnr_python_tests
        COMMAND ${Python3_EXECUTABLE} -m pytest
            "${CMAKE_CURRENT_SOURCE_DIR}/python/tests"
            -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    set_tests_properties(bvnr_python_tests PROPERTIES
        ENVIRONMENT
            "LIBBOVNAR_PATH=${CMAKE_BINARY_DIR}/libbvnr_shared.so;PYTHONPATH=${PYTHON_PATH}"
        LABELS "python;integration"
        TIMEOUT 300
    )

    message(STATUS "Python tests enabled. LIBBOVNAR_PATH set to "
                   "${CMAKE_BINARY_DIR}/libbvnr_shared.so during testing.")
endif()
