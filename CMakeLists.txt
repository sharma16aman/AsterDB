cmake_minimum_required(VERSION 3.28)

project(AsterDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(AsterDB src/main.cpp)

target_compile_options(AsterDB PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

find_package(GTest REQUIRED)

add_executable(AsterDBTests
    tests/test_basic.cpp
)

target_link_libraries(AsterDBTests
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(AsterDBTests)

find_package(benchmark REQUIRED)

add_executable(AsterDBBenchmarks
    benchmarks/benchmark_basic.cpp
)

target_link_libraries(AsterDBBenchmarks
    benchmark::benchmark
)

option(ASTERDB_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ASTERDB_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ASTERDB_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ASTERDB_ENABLE_ASAN)
    target_compile_options(AsterDB PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(AsterDB PRIVATE -fsanitize=address)
endif()

if(ASTERDB_ENABLE_UBSAN)
    target_compile_options(AsterDB PRIVATE -fsanitize=undefined)
    target_link_options(AsterDB PRIVATE -fsanitize=undefined)
endif()

if(ASTERDB_ENABLE_TSAN)
    target_compile_options(AsterDB PRIVATE -fsanitize=thread)
    target_link_options(AsterDB PRIVATE -fsanitize=thread)
endif()