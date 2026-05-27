cmake_minimum_required(VERSION 3.20)

project(uring-kv
    VERSION 0.1.0
    DESCRIPTION "In-memory key-value TCP server built on io_uring"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(URING_KV_BUILD_SERVER "Build the uring-kv server target" ON)
option(ASAN "Enable AddressSanitizer" OFF)

function(configure_uring_kv_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    )

    if(ASAN OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address)
    endif()
endfunction()

if(URING_KV_BUILD_SERVER)
    find_package(liburing REQUIRED)

    add_executable(uring_kv
        src/main.cpp
        src/kv_store.cpp
        src/protocol.cpp
        src/uring_loop.cpp
    )
    configure_uring_kv_target(uring_kv)
    target_link_libraries(uring_kv PRIVATE liburing::uring)
endif()

add_executable(test_protocol
    tools/test_protocol.cpp
    src/protocol.cpp
    src/kv_store.cpp
)
configure_uring_kv_target(test_protocol)

add_executable(test_kv
    tools/test_kv.cpp
    src/kv_store.cpp
)
configure_uring_kv_target(test_kv)
