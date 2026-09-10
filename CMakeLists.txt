cmake_minimum_required(VERSION 3.28)

project(AsterDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(ASTERDB_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
)

add_library(AsterDBCore
    src/storage/page.cpp
    src/storage/disk_manager.cpp
    src/storage/database.cpp
)

target_include_directories(AsterDBCore
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(AsterDBCore PRIVATE
    ${ASTERDB_WARNING_FLAGS}
)

add_executable(AsterDB
    src/main.cpp
)

target_link_libraries(AsterDB
    PRIVATE
        AsterDBCore
)

target_compile_options(AsterDB PRIVATE
    ${ASTERDB_WARNING_FLAGS}
)

enable_testing()

find_package(GTest REQUIRED)

add_executable(AsterDBTests
    tests/test_basic.cpp
)

target_link_libraries(AsterDBTests
    PRIVATE
        AsterDBCore
        GTest::gtest_main
)

target_compile_options(AsterDBTests PRIVATE
    ${ASTERDB_WARNING_FLAGS}
)

include(GoogleTest)
gtest_discover_tests(AsterDBTests)

find_package(benchmark REQUIRED)

add_executable(AsterDBBenchmarks
    benchmarks/benchmark_basic.cpp
)

target_link_libraries(AsterDBBenchmarks
    PRIVATE
        AsterDBCore
        benchmark::benchmark
)

target_compile_options(AsterDBBenchmarks PRIVATE
    ${ASTERDB_WARNING_FLAGS}
)

option(ASTERDB_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ASTERDB_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ASTERDB_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ASTERDB_ENABLE_ASAN)
    target_compile_options(AsterDBCore PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
    target_link_options(AsterDBCore PRIVATE
        -fsanitize=address
    )
endif()

if(ASTERDB_ENABLE_UBSAN)
    target_compile_options(AsterDBCore PRIVATE
        -fsanitize=undefined
    )
    target_link_options(AsterDBCore PRIVATE
        -fsanitize=undefined
    )
endif()

if(ASTERDB_ENABLE_TSAN)
    target_compile_options(AsterDBCore PRIVATE
        -fsanitize=thread
    )
    target_link_options(AsterDBCore PRIVATE
        -fsanitize=thread
    )
endif()
