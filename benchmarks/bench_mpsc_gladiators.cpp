#include <benchmark/benchmark.h>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <future>
#ifdef __linux__
#include <pthread.h>
#endif

#include "../include/types/order.hpp"
#include "../include/queues/spsc_queue.hpp"

using namespace hft;

void pin_thread_to_core(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

enum class Workload {
    LIGHT,
    HEAVY
};

template <Workload WL>
inline void process(int num) {
    int loops = (WL == Workload::LIGHT) ? 50 : 2000;
    for (volatile int i = 0; i < loops; ++i) {}
    benchmark::DoNotOptimize(num);
}

constexpr int NUM_THREADS = 2; 
constexpr int ITEMS_PER_THREAD = 5000; 
constexpr size_t Q_CAPACITY = 1048576; 


// 模型 1: Mutex + 当前线程原地执行
template <Workload WL>
class MutexInlineState {
    int max_num_{0};
    std::mutex mtx_;
public:
    void gen(int num) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (num > max_num_) { max_num_ = num; process<WL>(num); }
    }
};

// 模型 2: Mutex + 异步申请新线程执行
template <Workload WL>
class MutexAsyncState {
    int max_num_{0};
    std::mutex mtx_;
public:
    void gen(int num) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (num > max_num_) {
            max_num_ = num;
            std::async(std::launch::async, [num]() { process<WL>(num); });
        }
    }
};

// 模型 3: CAS + 当前线程原地执行
template <Workload WL>
class CasInlineState {
    std::atomic<int> max_num_{0};
public:
    void gen(int num) {
        int tmp = max_num_.load(std::memory_order_relaxed);
        while (num > tmp) {
            if (max_num_.compare_exchange_weak(tmp, num, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                process<WL>(num); 
                break;
            }
        }
    }
};

// 模型 4: CAS + 异步申请新线程执行
template <Workload WL>
class CasAsyncState {
    std::atomic<int> max_num_{0};
public:
    void gen(int num) {
        int tmp = max_num_.load(std::memory_order_relaxed);
        while (num > tmp) {
            if (max_num_.compare_exchange_weak(tmp, num, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                std::async(std::launch::async, [num]() { process<WL>(num); });
                break;
            }
        }
    }
};

class OptimizedMpscQueue {
    struct Slot {
        std::atomic<uint32_t> order_id{0};
        char padding[60];
    };
    Slot buffer_[Q_CAPACITY];
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t head_{0}; 
    static constexpr size_t MASK = Q_CAPACITY - 1;
public:
    void push(int num) {
        size_t my_tail = tail_.fetch_add(1, std::memory_order_relaxed);
        size_t index = my_tail & MASK;
        while (buffer_[index].order_id.load(std::memory_order_acquire) != 0) {}
        buffer_[index].order_id.store(num, std::memory_order_release);
    }
    bool try_pop(int& num) {
        size_t index = head_ & MASK;
        uint32_t val = buffer_[index].order_id.load(std::memory_order_acquire);
        if (val == 0) return false;
        num = val;
        buffer_[index].order_id.store(0, std::memory_order_release);
        head_++;
        return true;
    }
};

#define REGISTER_TESTS(WORKLOAD_ENUM, SUFFIX) \
    static void BM_MutexInline_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            MutexInlineState<WORKLOAD_ENUM> ms; \
            std::vector<std::thread> threads; \
            for (int i=0; i<NUM_THREADS; ++i) threads.emplace_back([&,i]() { pin_thread_to_core(i+1); for (int n=1; n<=ITEMS_PER_THREAD; ++n) ms.gen(n); }); \
            for (auto& t : threads) t.join(); \
        } \
    } \
    BENCHMARK(BM_MutexInline_##SUFFIX)->UseRealTime(); \
    \
    static void BM_MutexAsync_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            MutexAsyncState<WORKLOAD_ENUM> ms; \
            std::vector<std::thread> threads; \
            for (int i=0; i<NUM_THREADS; ++i) threads.emplace_back([&,i]() { pin_thread_to_core(i+1); for (int n=1; n<=ITEMS_PER_THREAD; ++n) ms.gen(n); }); \
            for (auto& t : threads) t.join(); \
        } \
    } \
    BENCHMARK(BM_MutexAsync_##SUFFIX)->UseRealTime(); \
    \
    static void BM_CasInline_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            CasInlineState<WORKLOAD_ENUM> cs; \
            std::vector<std::thread> threads; \
            for (int i=0; i<NUM_THREADS; ++i) threads.emplace_back([&,i]() { pin_thread_to_core(i+1); for (int n=1; n<=ITEMS_PER_THREAD; ++n) cs.gen(n); }); \
            for (auto& t : threads) t.join(); \
        } \
    } \
    BENCHMARK(BM_CasInline_##SUFFIX)->UseRealTime(); \
    \
    static void BM_CasAsync_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            CasAsyncState<WORKLOAD_ENUM> cs; \
            std::vector<std::thread> threads; \
            for (int i=0; i<NUM_THREADS; ++i) threads.emplace_back([&,i]() { pin_thread_to_core(i+1); for (int n=1; n<=ITEMS_PER_THREAD; ++n) cs.gen(n); }); \
            for (auto& t : threads) t.join(); \
        } \
    } \
    BENCHMARK(BM_CasAsync_##SUFFIX)->UseRealTime(); \
    \
    static void BM_MpscQueue_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            state.PauseTiming(); \
            auto mq = std::make_unique<OptimizedMpscQueue>(); \
            std::atomic<int> consumed{0}; \
            state.ResumeTiming(); \
            std::thread consumer([&]() { pin_thread_to_core(0); int num; while (consumed.load(std::memory_order_relaxed) < NUM_THREADS * ITEMS_PER_THREAD) { if (mq->try_pop(num)) { process<WORKLOAD_ENUM>(num); consumed.fetch_add(1, std::memory_order_relaxed); } } }); \
            std::vector<std::thread> threads; \
            for (int i=0; i<NUM_THREADS; ++i) threads.emplace_back([&,i]() { pin_thread_to_core(i+1); for (int n=1; n<=ITEMS_PER_THREAD; ++n) mq->push(n); }); \
            for (auto& t : threads) t.join(); \
            consumer.join(); \
        } \
    } \
    BENCHMARK(BM_MpscQueue_##SUFFIX)->UseRealTime(); \
    \
    static void BM_SpscArray_##SUFFIX(benchmark::State& state) { \
        for (auto _ : state) { \
            state.PauseTiming(); \
            auto sq1 = std::make_unique<SPSCQueue<Order, Q_CAPACITY>>(); \
            auto sq2 = std::make_unique<SPSCQueue<Order, Q_CAPACITY>>(); \
            std::atomic<int> consumed{0}; \
            state.ResumeTiming(); \
            std::thread consumer([&]() { pin_thread_to_core(0); Order o; while (consumed.load(std::memory_order_relaxed) < NUM_THREADS * ITEMS_PER_THREAD) { if (sq1->try_pop(o)) { process<WORKLOAD_ENUM>(o.order_id); consumed.fetch_add(1, std::memory_order_relaxed); } if (sq2->try_pop(o)) { process<WORKLOAD_ENUM>(o.order_id); consumed.fetch_add(1, std::memory_order_relaxed); } } }); \
            auto p_func = [&](int id, SPSCQueue<Order, Q_CAPACITY>* sq) { pin_thread_to_core(id+1); Order o; for (int n=1; n<=ITEMS_PER_THREAD; ++n) { o.order_id = n; while(!sq->try_push(o)); } }; \
            std::thread p1(p_func, 0, sq1.get()); \
            std::thread p2(p_func, 1, sq2.get()); \
            p1.join(); p2.join(); consumer.join(); \
        } \
    } \
    BENCHMARK(BM_SpscArray_##SUFFIX)->UseRealTime();

REGISTER_TESTS(Workload::LIGHT, Light)
REGISTER_TESTS(Workload::HEAVY, Heavy)

BENCHMARK_MAIN();