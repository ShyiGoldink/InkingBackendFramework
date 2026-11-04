#ifndef INKING_BACKEND_FRAMEWORK_NETWORK_PROFILE_H
#define INKING_BACKEND_FRAMEWORK_NETWORK_PROFILE_H

/**
 * @file NetworkProfile.h
 * @brief 分片内的分段计时（设计文档 M10）。
 *
 * 回答的问题只有一个：**时间花在哪一段**。没有它，优化全靠猜；有了它，
 * 「拆帧慢 / 入队慢 / 发送慢」三种完全不同的病因能分开。
 *
 * 五段的分界线是刻意选的（每段对应一种不同的修法）：
 *
 * | 段 | 量到的东西 | 这一段高说明什么 |
 * | --- | --- | --- |
 * | Receive | `recv` 系统调用本身 | 内核拷贝或缓冲区太小 |
 * | Parse | `FrameCodec::next` 一次取帧 | 缓冲里的字节搬动太多（可能是每次 erase 头部） |
 * | Dispatch | 投进业务池（含队列锁与唤醒） | 业务池结构有问题（单队列单锁） |
 * | Mailbox | 从回投队列取响应 | 回投批次太小、唤醒过多 |
 * | Send | `send` 系统调用 | 写缓冲管理或对端读得慢 |
 *
 * **默认关闭**，关闭时每个计时点的成本是一次 bool 判断。打开后每次计时是两次
 * 单调时钟读（Windows 上是 QPC，约 20~30 ns），单请求 5 次 = 100~150 ns；
 * 在 6 万 QPS 上大约 1%。所以它是「排查时打开、常驻时关掉」的东西。
 *
 * 计数器是原子的：`network-stat` 命令会在服务运行期间从控制台线程读它们。
 * 用 relaxed 是因为它们只用于统计，不需要任何顺序保证——为几个纳秒的计数
 * 引入内存屏障，属于给测量本身加成本。
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class NetworkProfile
{
public:
    enum class Segment : std::size_t
    {
        Receive = 0,
        Parse = 1,
        Dispatch = 2,
        Mailbox = 3,
        Send = 4,
        Business = 5,    /** 从投进业务池到响应回到分片：池在忙、还是在睡，看这一段 */
        BusinessRun = 6, /** 其中真正执行 handler 的那一段（扣除排队与唤醒） */
        Wait = 7,        /** 唯一不是「每请求」的一段：它是每轮事件循环的等待时间 */
        Count = 8,
    };

    static constexpr std::size_t kSegmentCount = static_cast<std::size_t>(Segment::Count);

    /**
     * @brief 一次计时的作用域。
     *
     * 关闭时构造只做一次 bool 判断、析构什么都不做——这样调用点可以写得很干净，
     * 不用在每个热路径上铺 `if (enabled)`。
     */
    class Scope
    {
    public:
        Scope(NetworkProfile &profile, Segment segment)
            : _profile(profile), _segment(segment)
        {
            if (_profile._enabled.load(std::memory_order_relaxed))
            {
                _startNs = nowNs();
            }
        }

        ~Scope()
        {
            if (_startNs != 0)
            {
                _profile.add(_segment, nowNs() - _startNs);
            }
        }

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        NetworkProfile &_profile;
        Segment _segment;
        std::int64_t _startNs = 0;
    };

    struct Snapshot
    {
        bool enabled = false;
        std::uint64_t samples = 0; /** 请求级采样次数（= 处理的帧数） */
        std::uint64_t loops = 0;   /** 事件循环轮数（Wait 的分母） */
        std::uint64_t totalNs[kSegmentCount] = {};
    };

    explicit NetworkProfile(bool enabled)
    {
        _enabled.store(enabled, std::memory_order_relaxed);
        for (auto &value : _total)
        {
            value.store(0, std::memory_order_relaxed);
        }
    }

    bool enabled() const
    {
        return _enabled.load(std::memory_order_relaxed);
    }

    void add(Segment segment, std::int64_t nanoseconds)
    {
        if (nanoseconds <= 0)
        {
            return;
        }
        _total[static_cast<std::size_t>(segment)].fetch_add(
            static_cast<std::uint64_t>(nanoseconds), std::memory_order_relaxed);
    }

    void countSample()
    {
        _samples.fetch_add(1, std::memory_order_relaxed);
    }

    void countLoop()
    {
        _loops.fetch_add(1, std::memory_order_relaxed);
    }

    Snapshot snapshot() const
    {
        Snapshot result;
        result.enabled = _enabled.load(std::memory_order_relaxed);
        result.samples = _samples.load(std::memory_order_relaxed);
        result.loops = _loops.load(std::memory_order_relaxed);
        for (std::size_t index = 0; index < kSegmentCount; ++index)
        {
            result.totalNs[index] = _total[index].load(std::memory_order_relaxed);
        }
        return result;
    }

    static const char *segmentName(Segment segment)
    {
        switch (segment)
        {
        case Segment::Receive:
            return "收包";
        case Segment::Parse:
            return "拆帧";
        case Segment::Dispatch:
            return "派发";
        case Segment::Mailbox:
            return "回投";
        case Segment::Send:
            return "发送";
        case Segment::Business:
            return "业务往返";
        case Segment::BusinessRun:
            return "业务执行";
        case Segment::Wait:
            return "等待";
        case Segment::Count:
            break;
        }
        return "未知";
    }

    /** 单调时钟纳秒。计时点统一用它，避免不同来源的时钟混算 */
    static std::int64_t nowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /** 一行摘要：每请求均摊微秒 + 合计；等待段按「每轮」算 */
    static std::string describe(const Snapshot &snapshot)
    {
        if (!snapshot.enabled)
        {
            return "分段计时未开启";
        }
        if (snapshot.samples == 0)
        {
            return "分段计时已开启，但还没有样本";
        }

        std::string text;
        double totalUs = 0.0;
        for (std::size_t index = 0; index < kSegmentCount; ++index)
        {
            const auto segment = static_cast<Segment>(index);
            if (segment == Segment::Wait)
            {
                continue;
            }
            const double us = static_cast<double>(snapshot.totalNs[index]) / 1000.0 /
                              static_cast<double>(snapshot.samples);
            totalUs += us;
            if (!text.empty())
            {
                text += " / ";
            }
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s %.2f", segmentName(segment), us);
            text += buffer;
        }

        char tail[160];
        const double waitUs = snapshot.loops == 0
                                  ? 0.0
                                  : static_cast<double>(snapshot.totalNs[static_cast<std::size_t>(Segment::Wait)]) /
                                        1000.0 / static_cast<double>(snapshot.loops);
        std::snprintf(tail, sizeof(tail), "；合计 %.2f us/请求，样本 %llu 条；等待 %.2f ms/轮（%llu 轮）",
                      totalUs,
                      static_cast<unsigned long long>(snapshot.samples),
                      waitUs / 1000.0,
                      static_cast<unsigned long long>(snapshot.loops));
        return text + tail;
    }

private:
    std::atomic<bool> _enabled;
    std::atomic<std::uint64_t> _samples{0};
    std::atomic<std::uint64_t> _loops{0};
    std::atomic<std::uint64_t> _total[kSegmentCount];
};

#endif // INKING_BACKEND_FRAMEWORK_NETWORK_PROFILE_H
