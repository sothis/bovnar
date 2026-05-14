find_path(GMP_INCLUDE_DIR
    NAMES gmp.h
    PATHS
        /usr/include
        /usr/local/include
    PATH_SUFFIXES
        ""
        gmp
        x86_64-linux-gnu
        aarch64-linux-gnu
        i386-linux-gnu
        arm-linux-gnueabihf)

find_library(GMP_LIBRARY
    NAMES gmp libgmp
    PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMP
    REQUIRED_VARS GMP_LIBRARY GMP_INCLUDE_DIR)

if(GMP_FOUND AND NOT TARGET GMP::GMP)
    add_library(GMP::GMP UNKNOWN IMPORTED)
    set_target_properties(GMP::GMP PROPERTIES
        IMPORTED_LOCATION "${GMP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GMP_INCLUDE_DIR}")
endif()

mark_as_advanced(GMP_INCLUDE_DIR GMP_LIBRARY)
