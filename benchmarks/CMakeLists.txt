# benchmarks/CMakeLists.txt

include(FetchContent)

message(STATUS "Downloading Google Benchmark...")

# 👇 核心修复 1：强行关闭 Benchmark 的警告即错误机制 (解决 MSVC 下正则检测失败的 BUG)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable warnings as errors" FORCE)

# 👇 核心修复 2：关闭 Benchmark 自带的测试项目
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