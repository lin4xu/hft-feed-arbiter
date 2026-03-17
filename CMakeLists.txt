# CMakeLists.txt (根目录)
cmake_minimum_required(VERSION 3.14)
project(hft-feed-arbiter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 👇 核心修复：根据不同的编译器，传递不同的参数！
if(MSVC)
    # Windows 微软编译器参数
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /Zc:__cplusplus")
else()
    # Linux GCC/Clang 编译器参数
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror=return-type")
endif()

add_library(hft_arbiter INTERFACE)
target_include_directories(hft_arbiter INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

option(BUILD_TESTS "Build logic correctness tests" OFF)
option(BUILD_BENCHMARKS "Build performance benchmarks" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()