#include <benchmark/benchmark.h>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include "../include/types/order.hpp"
#include "../include/queues/spsc_queue.hpp"

using namespace hft;

// 模拟极速的下游业务处理逻辑
inline void process(int num) {
    benchmark::DoNotOptimize(num);
}

constexpr int NUM_THREADS = 2; // 2 条专线 (2个生产者)
constexpr int ITEMS_PER_THREAD = 500'000;
constexpr size_t Q_CAPACITY = 1048576; // SPSC/MPSC 队列容量

// ==========================================
// 1. Mutex 状态机 (不用队列，直接加锁覆盖)
// ==========================================
class MutexState {
    int max_num_{0};
    std::mutex mtx_;
public:
    void gen(int num) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (num > max_num_) {
            max_num_ = num;
            process(num); // 抢到锁的生产者原地处理
        }
    }
};

// ==========================================
// 2. CAS 状态机 (不用队列，完美复刻你的代码)
// ==========================================
class CasState {
    std::atomic<int> max_num_{0};
public:
    void gen(int num) {
        int tmp = max_num_.load(std::memory_order_relaxed);
        while (num > tmp) {
            if (max_num_.compare_exchange_strong(tmp, num, 
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                process(num); // CAS 成功的生产者原地处理
                break;
            }
        }
    }
};

// ==========================================
// 3. 极简 MPSC 队列 (多个生产者挤一个队列)
// ==========================================
class SimpleMpscQueue {
    Order buffer_[Q_CAPACITY];
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t head_{0}; 
    std::atomic<bool> ready_[Q_CAPACITY]; // 粗略的就绪标志
public:
    SimpleMpscQueue() {
        for(size_t i=0; i<Q_CAPACITY; ++i) ready_[i] = false;
    }
    void push(int num) {
        size_t my_tail = tail_.fetch_add(1, std::memory_order_relaxed);
        buffer_[my_tail].order_id = num;
        ready_[my_tail].store(true, std::memory_order_release);
    }
    bool try_pop(int& num) {
        if (!ready_[head_].load(std::memory_order_acquire)) return false;
        num = buffer_[head_].order_id;
        head_++;
        return true;
    }
};

// ==========================================
// 压测执行宏
// ==========================================

// 测试 1: 锁覆盖
static void BM_MutexState(benchmark::State& state) {
    for (auto _ : state) {
        MutexState ms;
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&]() {
                for (int n = 1; n <= ITEMS_PER_THREAD; ++n) ms.gen(n);
            });
        }
        for (auto& t : threads) t.join();
    }
}
BENCHMARK(BM_MutexState)->UseRealTime();

// 测试 2: CAS 覆盖
static void BM_CasState(benchmark::State& state) {
    for (auto _ : state) {
        CasState cs;
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&]() {
                for (int n = 1; n <= ITEMS_PER_THREAD; ++n) cs.gen(n);
            });
        }
        for (auto& t : threads) t.join();
    }
}
BENCHMARK(BM_CasState)->UseRealTime();

// 测试 3: MPSC 队列
static void BM_MpscQueue(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming(); // 暂停计时：不要把申请 25MB 内存的时间算进压测里
        auto mq = std::make_unique<SimpleMpscQueue>(); // 核心修复：放到堆上！
        std::atomic<int> consumed{0};
        state.ResumeTiming(); // 恢复计时
        
        std::thread consumer([&]() {
            int num;
            while (consumed.load(std::memory_order_relaxed) < NUM_THREADS * ITEMS_PER_THREAD) {
                if (mq->try_pop(num)) {
                    process(num);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });

        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&]() {
                for (int n = 1; n <= ITEMS_PER_THREAD; ++n) mq->push(n);
            });
        }
        for (auto& t : threads) t.join();
        consumer.join();
    }
}
BENCHMARK(BM_MpscQueue)->UseRealTime();

// 测试 4: SPSC 阵列
static void BM_SpscArray(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming(); 
        auto sq1 = std::make_unique<SPSCQueue<Order, Q_CAPACITY>>(); // 核心修复：放到堆上！
        auto sq2 = std::make_unique<SPSCQueue<Order, Q_CAPACITY>>();
        std::atomic<int> consumed{0};
        state.ResumeTiming();
        
        std::thread consumer([&]() {
            Order o;
            while (consumed.load(std::memory_order_relaxed) < NUM_THREADS * ITEMS_PER_THREAD) {
                if (sq1->try_pop(o)) { process(o.order_id); consumed.fetch_add(1, std::memory_order_relaxed); }
                if (sq2->try_pop(o)) { process(o.order_id); consumed.fetch_add(1, std::memory_order_relaxed); }
            }
        });

        auto p_func = [&](int id) {
            Order o;
            for (int n = 1; n <= ITEMS_PER_THREAD; ++n) {
                o.order_id = n;
                if (id == 0) while(!sq1->try_push(o));
                else         while(!sq2->try_push(o));
            }
        };

        std::thread p1(p_func, 0);
        std::thread p2(p_func, 1);

        p1.join();
        p2.join();
        consumer.join();
    }
}
BENCHMARK(BM_SpscArray)->UseRealTime();

BENCHMARK_MAIN();