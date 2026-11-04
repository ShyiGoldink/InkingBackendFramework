/**
 * @file timer_bench.cpp
 * @brief 定时器堆的量尺（设计文档 M8 的指标）。
 *
 * M8 要求「100 万个定时器插入/取消，耗时与 log(100 万) 呈线性」——翻成可测的话是：
 * **单次操作的耗时应该随规模对数增长**。规模从 1 万到 100 万差 100 倍，
 * 而 log 只差 2 倍多一点，所以单次耗时涨到 2~3 倍是合理的，涨到 10 倍说明
 * 某个环节是 O(n)（典型原因：取消改成线性扫描、或压紧过于频繁）。
 *
 * 用法：
 *   ./timer_bench              # 1 万 / 10 万 / 100 万三级
 *   ./timer_bench 2000000      # 指定最大规模
 */

#include "network/TimerQueue.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    double millisecondsSince(const Clock::time_point &start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    /**
     * 一条连接的三个超时是独立的 key，所以规模 N 用 N 个连接标识就够，
     * 不必再乘 3——量的东西是一样的。
     */
    void runLevel(std::size_t count)
    {
        TimerQueue queue;
        std::vector<ExpiredTimer> expired;

        // ① 插入：到期时刻打散到 [1, count] 毫秒，让堆真的做上浮比较。
        //    全部同一时刻会让比较退化成一个常数，量出来的数偏好看但不真实。
        auto start = Clock::now();
        for (std::size_t index = 0; index < count; ++index)
        {
            queue.arm(index + 1, TimeoutKind::HalfFrame,
                      static_cast<int>(index % 1000) + 1);
        }
        const double armMs = millisecondsSince(start);

        // ② 取消一半（偶数标识）。这是「连接正常结束」的常见形态。
        start = Clock::now();
        std::size_t disarmed = 0;
        for (std::size_t index = 0; index < count; index += 2)
        {
            if (queue.disarm(index + 1, TimeoutKind::HalfFrame))
            {
                ++disarmed;
            }
        }
        const double disarmMs = millisecondsSince(start);

        // ③ 刷新一半：模拟空闲超时被不断顺延，这会把陈旧条目堆起来，
        //    也是压紧逻辑唯一会被触发的路径。
        start = Clock::now();
        for (std::size_t index = 1; index < count; index += 2)
        {
            queue.arm(index + 1, TimeoutKind::HalfFrame, 1);
        }
        const double refreshMs = millisecondsSince(start);

        // ④ 收到期：等到全部过期后一次性弹出，量的是「弹出 + 陈旧条目丢弃」。
        TimerQueue::nowMs(); // 预热一下时钟调用，避免把第一次的静态初始化算进去
        const auto sleepUntil = Clock::now() + std::chrono::milliseconds(3);
        while (Clock::now() < sleepUntil)
        {
        }

        start = Clock::now();
        const std::size_t collected = queue.collectExpired(expired);
        const double collectMs = millisecondsSince(start);

        std::printf("规模 %9zu | 插入 %8.2f ms (%6.0f ns/次) | 取消 %8.2f ms (%6.0f ns/次)"
                    " | 刷新 %8.2f ms (%6.0f ns/次) | 弹出 %zu 条 %8.2f ms | 堆/活 %zu/%zu\n",
                    count,
                    armMs, armMs * 1e6 / static_cast<double>(count),
                    disarmMs, disarmMs * 1e6 / static_cast<double>(disarmed == 0 ? 1 : disarmed),
                    refreshMs, refreshMs * 1e6 / static_cast<double>(count / 2),
                    collected,
                    collectMs,
                    queue.heapSize(), queue.liveCount());
    }
}

int main(int argc, char **argv)
{
    std::size_t maxCount = 1000000;
    if (argc > 1)
    {
        const long long parsed = std::atoll(argv[1]);
        if (parsed > 0)
        {
            maxCount = static_cast<std::size_t>(parsed);
        }
    }

    std::printf("TimerQueue 基准（最小堆 + 懒删除）\n");
    std::printf("判读方法：单次耗时随规模应当只涨 log(n) 的量级。\n");
    std::printf("  1 万 → 100 万规模差 100 倍，log 只差约 2.1 倍，单次耗时涨到 2~3 倍正常；\n");
    std::printf("  涨到 10 倍说明某处退化成 O(n)（取消线性扫描、或压紧过于频繁）。\n\n");

    for (std::size_t count = 10000; count <= maxCount; count *= 10)
    {
        runLevel(count);
    }
    return 0;
}
