include(FetchContent)

message(STATUS "Downloading Google Benchmark...")

set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable warnings as errors" FORCE)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://gitee.com/mirrors/google-benchmark.git
  GIT_TAG        v1.8.3 
)

FetchContent_MakeAvailable(googlebenchmark)

add_executable(bench_mpsc_gladiators bench_mpsc_gladiators.cpp)

find_package(Threads REQUIRED)

target_link_libraries(bench_mpsc_gladiators PRIVATE 
    hft_arbiter 
    benchmark::benchmark 
    Threads::Threads
)