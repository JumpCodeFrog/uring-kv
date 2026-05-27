find_path(LIBURING_INCLUDE_DIR
    NAMES liburing.h
    HINTS
        "${CMAKE_CURRENT_LIST_DIR}/../.deps/liburing-dev/usr/include"
)

find_library(LIBURING_LIBRARY
    NAMES uring
    HINTS
        "${CMAKE_CURRENT_LIST_DIR}/../.deps/liburing-dev/usr/lib/x86_64-linux-gnu"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(liburing
    REQUIRED_VARS LIBURING_LIBRARY LIBURING_INCLUDE_DIR
)

if(liburing_FOUND AND NOT TARGET liburing::uring)
    add_library(liburing::uring UNKNOWN IMPORTED)
    set_target_properties(liburing::uring PROPERTIES
        IMPORTED_LOCATION "${LIBURING_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LIBURING_INCLUDE_DIR LIBURING_LIBRARY)
