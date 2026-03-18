# HFT Feed Arbiter

本项目是一个追求极低延迟的多源行情去重与分发引擎，不仅包含了行情仲裁的核心数据结构（如缓存行对齐的极致无锁 SPSC/MPSC 队列），还包含了一个极为硬核的 **高频并发模型基准测试 (Benchmark)** ，用于深刻揭示在微秒/纳秒级延迟要求下，不同多线程架构的真实性能表现。

---

## 🚀 Benchmark: 并发模型的终极角斗场

在 `benchmarks/bench_mpsc_gladiators.cpp` 中，我们使用 Google Benchmark 框架，针对高频交易中典型的“多生产者（网络收包） -> 单/多消费者（行情分发/策略撮合）”场景，进行了控制变量法的交叉对比测试。

### 1. 测试矩阵设计 (2 x 6 维度)

我们设计了 **2 种业务负载** 与 **6 种并发调度机制** 的交叉测试矩阵：

**业务负载 (Workload):**
* `_Light`: 极轻量负载（约十几纳秒），模拟单纯的数据透传或极简状态机更改。
* `_Heavy`: 重负载（几百纳秒到微秒级），模拟真实的复杂 HFT 业务（如：重建中心化订单簿 Orderbook、计算风控因子）。

**并发调度机制 (Concurrency Models):**
1. **MutexInline**: 锁竞争 + 生产者原地执行业务。
2. **MutexAsync**: 锁竞争 + `std::async` 动态派发后台线程。
3. **CasInline**: 无锁 CAS 自旋 + 生产者原地执行业务。
4. **CasAsync**: 无锁 CAS 自旋 + `std::async` 动态派发后台线程。
5. **MpscQueue**: 多生产者抢占同一无锁队列 + 独立消费者绑核异步执行（流水线模式 1.0）。
6. **SpscArray**: 每个生产者独享无锁 SPSC 队列 + 独立消费者轮询（流水线模式 2.0，纯粹的零数据竞争）。

---

### 2. 基准测试结果 (基于 Windows 32-Core CPU 压测)

| Benchmark | Time | CPU | Iterations |
| :--- | :--- | :--- | :--- |
| BM_MutexInline_Light/real_time | 458950 ns | 41010 ns | 1524 |
| BM_MutexAsync_Light/real_time | 47346293 ns | 0 ns | 15 |
| BM_CasInline_Light/real_time | 268345 ns | 32111 ns | 2433 |
| BM_CasAsync_Light/real_time | 33638233 ns | 744048 ns | 21 |
| BM_MpscQueue_Light/real_time | 2581020 ns | 1678719 ns | 242 |
| BM_SpscArray_Light/real_time | 1933462 ns | 1244849 ns | 364 |
| BM_MutexInline_Heavy/real_time | 2978679 ns | 132979 ns | 235 |
| BM_MutexAsync_Heavy/real_time | 69902440 ns | 0 ns | 10 |
| BM_CasInline_Heavy/real_time | 1528021 ns | 34955 ns | 447 |
| BM_CasAsync_Heavy/real_time | 30309745 ns | 0 ns | 22 |
| BM_MpscQueue_Heavy/real_time | 6812837 ns | 1809211 ns | 95 |
| BM_SpscArray_Heavy/real_time | 6797862 ns | 1531863 ns | 102 |

---

### 3. 深度现象剖析 (Deep Dive Analysis)

这份测试数据真实地反映了计算机底层的物理规律与 HFT 架构哲学：

#### 💡 结论一：动态异步派发 (`Async`) 是 HFT 的灾难
从数据中可以看出，只要涉及 `std::async`，耗时直接飙升至 **30~70 毫秒** （几千万纳秒级别）。
* **分析：** 在关键数据链路上动态向操作系统申请线程和任务调度的开销，相比于几十纳秒的纯内存操作，慢了足足 1000 倍以上。HFT 必须使用 **预先启动并绑核死循环（Busy Polling）** 的线程模型。

#### 💡 结论二：在极轻负载下，跨核通信的运费 > 计算本身
在 `_Light` 组中，`CasInline` (0.26ms) 竟然比 `SpscArray` (1.93ms) 快了一个数量级。
* **分析：** 队列的本质是将数据从生产者核心的 L1 Cache 同步到消费者核心的 L1 Cache。在几十纳秒的极小任务下，跨核 Cache Line 的同步开销（约 40-60ns）远超计算本身。此时，直接在当前 L1 Cache 原地计算反而最快。

#### 💡 结论三：流水线队列架构 —— 应对真实重载 HFT 的唯一解
在 `_Heavy` 组中，`CasInline` (1.52ms) 表面上依然快于队列 (6.79ms)，但这其实是一个 **“压测陷阱 (Parallel vs Serial Trap)”** ：
* **伪反杀的真相：** `CasInline` 的高速是因为 2 个生产者在 **多核并行（Parallel）** 执行重载计算，而队列组是将所有重载任务交给 1 个消费者 **串行（Serial）** 执行。
* **HFT 真实场景：** 真实的下游业务（如更新统一的订单簿）必须是 **串行且数据严格一致的** ，绝不允许网络线程无锁并发读写全局状态。如果强行让网络线程串行处理 `Heavy` 业务，会导致严重的 **网卡队头阻塞 (Head-of-Line Blocking)** ，引发致命的 UDP 丢包。
* **队列的终极意义：** 使用 `MPSC/SPSC Queue`，网络收包线程只需耗费十来纳秒将数据 Push 入队即可极速脱身去收下一个包， **保证零丢包** 。即使下游业务处理耗时 6.7ms，也完美实现了网络 I/O 与业务计算的物理隔离。

---

## 🛠️ 构建与运行指南 (Build & Run)

本项目依赖 C++17，推荐在 Linux 环境下编译以开启线程绑核（Pin Thread to Core）的极致性能。

```bash
# 1. 创建构建目录
mkdir build && cd build

# 2. 生成 Makefile
cmake -DCMAKE_BUILD_TYPE=Release ..

# 3. 编译
make -j8

# 4. 运行基准测试
./benchmarks/bench_mpsc_gladiators
