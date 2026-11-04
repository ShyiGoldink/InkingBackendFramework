#ifndef INKING_BACKEND_FRAMEWORK_SHARD_H
#define INKING_BACKEND_FRAMEWORK_SHARD_H

/**
 * @file Shard.h
 * @brief 分片：一条线程 + 一个事件集合 + 一批连接（设计文档 M4 / 台阶 B3、C2）。
 *
 * 分片是这套架构的性能单元与隔离单元。为什么必须是一条**独立**线程、
 * 而不是塞进 ThreadPool：
 * - 池子的前提是线程可以互换，谁醒了谁干；分片恰好相反，它绑定固定的 fd 与缓冲，
 *   不能换人干，塞进池子等于让池线程变成专用线程，池子名存实亡；
 * - 池子的抽象是「共享谓词 + 共享执行体」，没有「每条线程各自一套循环状态」这个东西。
 *
 * 线程归属规则（这是本文件最重要的约束，比代码本身重要）：
 * 1. 一条连接在任一时刻只属于一个分片，**只有该分片线程**能读写它的 socket 与缓冲。
 * 2. 别的线程想影响分片，只有三个口子：`adopt()`（交一条新连接）、`requestStop()`
 *    （要求退出）、`mailbox`（投一条响应）。三者都只碰队列/原子变量，然后唤醒分片。
 * 3. 从 Poller 摘除必须在 close 之前，且都在分片线程上按顺序做。
 *
 * 「一条在飞」（C2 的核心，M7 的保序基础）：
 *
 * ```
 * 拆出一整帧 → 摘掉这条连接的读关注 → 投给业务池
 *            → 业务线程算完，把字节投回 mailbox
 *            → 分片倒进写缓冲、挂写关注
 *            → 写缓冲清空 → 恢复读关注 → 若 codec 里还压着帧，立刻接着拆
 * ```
 *
 * 一个机制同时解决三件事：顺序天然正确（不需要序号）、下游积压时自然少读（背压）、
 * 客户端猛发也灌不爆内存。**注意最后那句「接着拆」**：摘读关注期间对端可能已经又
 * 发了几帧进来（同一个 recv 里就有），它们在 `codec` 的缓冲里等着；恢复关注时必须
 * 主动消化，否则要一直等到下一次网络事件才被处理。
 */

#include "network/Connection.h"
#include "network/NetworkConfig.h"
#include "network/Poller.h"
#include "network/ShardMailbox.h"
#include "network/TimerQueue.h"
#include "network/Wakeup.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Shard
{
public:
    /**
     * @param index 分片序号，只用于日志与展示
     * @param maxConnections 连接上限。到顶之后新连接会被直接关掉并记一条 Warn，
     *        而不是「先接下来再说」——已经满负荷的分片再吃新连接，会让它管的
     *        全部连接一起变慢，那是最糟的过载处理方式。
     * @param timeouts 三类超时（C5）。0 表示不启用该类超时
     */
    Shard(std::size_t index, std::size_t maxConnections, const NetworkTimeouts &timeouts);
    ~Shard();

    Shard(const Shard &) = delete;
    Shard &operator=(const Shard &) = delete;
    Shard(Shard &&) = delete;
    Shard &operator=(Shard &&) = delete;

    /** 建事件集合、唤醒器并拉起线程 */
    bool start(std::string &errorMessage);

    /**
     * @brief 把一条新连接交给本分片。
     *
     * 只做三件事：入交接队列、判断队列是否由空变非空、必要时唤醒。
     * 判定「是否要唤醒」必须在锁内完成，否则会丢唤醒
     * （表现为「偶尔某条新连接迟迟不响应」）。
     *
     * 交进来之后 socket 的所有权就归分片了：调用方不得再碰它，包括 close。
     */
    void adopt(SocketType socket);

    /** 请求退出。任意线程可调，退出延迟由唤醒器保证是「立刻」而不是「等超时」 */
    void requestStop();

    /**
     * @brief 请求「优雅停止」（C6）：不再读新请求，等在飞的回包与写缓冲收干净。
     *
     * 与 `requestStop()` 的区别是**要不要给在飞请求一个交代**：
     * - `requestStop()` 是硬停：立刻退出，写缓冲里没发完的字节直接丢，最省时间；
     * - `requestDrain()` 是排空：已进来的请求一定拿到完整响应（或者连接在帧边界处
     *   被干净断开），代价是停机最多慢 `graceMs`。
     *
     * 三者顺序不能变：先停止接入 → 再 `requestDrain` → 再 `join`。
     *
     * @param graceMs 排空上限。0 表示不等（等价于硬停）。上限是必须的：一个只发不读的
     *        对端可以让写缓冲永远排不空，没有上限就该停不下来了。
     */
    void requestDrain(int graceMs);

    /** 排空是否因为到时限而放弃（只有 join 之后读才有意义） */
    bool drainTimedOut() const
    {
        return _drainTimedOut;
    }

    /** 到时限时还剩多少条连接 / 多少字节待发（同上，join 之后读） */
    std::size_t drainSurvivors() const
    {
        return _drainSurvivors;
    }

    std::size_t drainSurvivorBytes() const
    {
        return _drainSurvivorBytes;
    }

    /** 排空实际耗时（毫秒）。0 表示没有真正排空过 */
    long long drainElapsedMs() const
    {
        return _drainElapsedMs;
    }

    /**
     * @brief 累计统计（M9 的「最终统计上报」）。
     *
     * 只能在 `join()` 之后调用：里面的字段都是分片线程独占写的，
     * 线程还在跑的时候读就是数据竞争。这不是「顺便省个锁」，
     * 而是刻意的：这些数只用于停机报告。
     */
    struct Statistics
    {
        std::size_t peakConnections = 0;
        std::size_t frames = 0;
        std::size_t dispatched = 0;
        std::size_t replies = 0;
        std::size_t handlerFailed = 0;
        std::size_t droppedReplies = 0;
        std::size_t timeouts = 0;      /** 三类超时之和 */
        std::size_t errors = 0;        /** 收错 + 发错 + 非法帧 + 未知指令 */
        std::size_t peerClosed = 0;    /** 对端正常关闭 */
        std::size_t shutdownCloses = 0;/** 停机时主动断开 */
    };

    Statistics statistics() const;

    /** 等线程退出。可以重复调用 */
    void join();

    std::size_t connectionCount() const
    {
        return _connectionCount.load(std::memory_order_relaxed);
    }

    std::size_t index() const
    {
        return _index;
    }

    /** 日志与自检里显示的名字，例如「分片 2」 */
    std::string name() const;

private:
    /** 线程执行体：整条分片线程的全部生命周期都在这里 */
    void serve();

    /** 循环第一步：把 acceptor 交过来的连接接进来并注册进事件集合 */
    void drainIncoming();

    /** 循环第二步：把业务线程投回来的响应倒进各连接的写缓冲 */
    void drainMailbox();

    /** 退出路径：交接队列里还没来得及接的连接也要关掉，否则 fd 泄漏 */
    void closeIncoming();

    void handleEvent(const PollerEvent &event);
    void readFrom(Connection *connection);

    /**
     * @brief 把 `FrameCodec` 里已经凑齐的帧一帧一帧取出来处理。
     *
     * @return false 表示这条连接已经在处理过程中被关掉，调用方不要再碰它。
     *
     * 只在连接处于 Idle 时允许拆帧：非空闲意味着上一条请求还在飞，
     * 此时继续拆就变成「一条连接两条在飞」，顺序保证当场失效。
     */
    bool consumeFrames(Connection *connection);

    /** 把一条完整消息派给业务池。成功即把连接置为 InFlight 并摘掉读关注 */
    bool dispatchFrame(Connection *connection, const CommandFrame &frame);

    void flush(Connection *connection);

    /** 更新关注的事件位；和当前一致时不做系统调用 */
    void updateInterest(Connection *connection);

    /** 写缓冲清空后：回到 Idle、恢复读关注、把 codec 里压着的帧接着拆 */
    void resumeReading(Connection *connection);

    /**
     * @brief 按连接当前状态挂/摘三类定时器（C5）。
     *
     * 所有挂载与摘除都只发生在这一个函数里，调用点是「状态或缓冲区刚变过」的地方。
     * 不这么做的话，判断条件会被写在下标不同的四五处（拆帧后、写完后、派发前……），
     * 与 `updateInterest` 当初的理由一样：两处各写一半，早晚漏掉一处。
     *
     * 三类超时的语义并不统一，这是刻意的：
     * - 空闲 / 写：**顺延**。对端每有进展就重新计时，抓的是「彻底不动了」。
     * - 半包：**不顺延**。从第一个残字节起算绝对期限，抓的是「一直在挤、但挤不完」。
     *   顺延的话，每秒发 1 个字节就能让一条连接无限期占着缓冲。
     */
    void refreshTimers(Connection *connection);

    /**
     * @brief 挂一个「可以顺延」的定时器：剩余时间还过半就保持原样。
     *
     * 为什么需要这个折中：`refreshTimers` 会在每个事件之后被调用，如果每次都重挂，
     * 每次 `arm` 都会压一个堆条目（旧条目变成陈旧条目），1000 条连接满负荷时
     * 能造出每秒百万级的条目——压紧的开销反而超过定时器本身的价值。
     * 半程才顺延，刷新频率就被钉在「每半个超时周期最多一次」。
     */
    void armRefreshingTimer(Connection *connection, TimeoutKind kind, int timeoutMs);

    /** 处理已到期的定时器：只标记关闭，真正销毁留给清理阶段 */
    void handleTimeouts();

    /**
     * @brief 排空阶段的一步：先关掉「没事可做」的连接，再判断是否收干净。
     *
     * @return true 表示可以退出循环了（全部收干净，或者到了排空上限）。
     *
     * 只关 Idle 且没有待发字节的连接，是因为它们此刻本来就没有未完成的交互；
     * InFlight / Writing 的连接必须留着——那正是「不丢请求」的全部内容。
     */
    bool drainStep();

    /**
     * @brief 关闭前把接收缓冲里剩下的字节读掉丢掉（上限 `limitBytes`）。
     *
     * 不是为了「处理」它们——停机期间读到的新请求一律不受理——而是为了**关得干净**：
     * 内核在 close 时如果发现接收缓冲里还有未读数据，会发 RST 而不是 FIN。
     * RST 会让对端丢掉已经收到的数据（可能包括我们刚发出去的那个完整响应），
     * 表现为「服务端明明回了、客户端却报连接重置」。
     *
     * 上限是必须的：一个正在猛灌数据的对端可以让这个循环永不结束，
     * 而停机阶段最不能接受的就是「卡在某一条连接上」。
     */
    void discardIncoming(Connection *connection, std::size_t limitBytes);

    /** 只做登记：真正的摘除 + close 在一轮事件处理结束之后的清理阶段 */
    void closeConnection(Connection *connection, CloseReason reason);
    void removeClosedConnections();
    void closeAllConnections();

    Connection *findConnection(std::uint64_t id);

    const std::size_t _index;
    const std::size_t _maxConnections;
    const NetworkTimeouts _timeouts;

    std::thread _thread;

    /**
     * @brief 本分片的定时器堆（C5）。
     *
     * 每个分片一份而不是全局一份：定时器的消费者是分片线程里的连接状态机，
     * 全局一份就要跨线程取、跨线程回写，每次超时都要抢锁——而这件事每毫秒都在
     * 每个分片里发生一次。分开之后它和连接表一样只归本线程所有，无锁。
     */
    TimerQueue _timers;

    /** 本轮到期的定时器（复用，避免每轮分配） */
    std::vector<ExpiredTimer> _expired;

    /**
     * 声明顺序就是销毁顺序的逆序，所以这两个成员的位置有讲究：
     * 先销毁 Poller（它持有的集合里还挂着唤醒器那个 fd），再销毁唤醒器。
     * 反过来写也能跑，但「集合比它登记过的 fd 活得久」是个没必要留的隐患。
     *
     * 唤醒器用 shared_ptr：`ShardMailbox` 也持有一份，业务线程投响应时靠它保证
     * 唤醒器活到自己这次调用结束（见 ShardMailbox.h）。
     */
    std::shared_ptr<Wakeup> _wakeup;
    std::unique_ptr<Poller> _poller;

    /** 业务线程 → 分片的回投通道。由分片与「在飞的任务」共同持有 */
    std::shared_ptr<ShardMailbox> _mailbox;

    /**
     * 连接表：**用分片分配的连接标识做 key**，不是 socket。
     *
     * 为什么不用 socket 做 key：socket 号会被内核复用，业务线程手里那个号可能在它
     * 回来的路上就已经属于另一条新连接了。用自增标识（永不重复）就彻底消除了
     * 「把回包发给下一条连接」这种最难查的错误。
     */
    std::unordered_map<std::uint64_t, std::unique_ptr<Connection>> _connections;

    /** 交接队列：acceptor 线程写，分片线程整批取走 */
    std::mutex _incomingMutex;
    std::vector<SocketType> _incoming;

    std::atomic<std::size_t> _connectionCount{0};
    std::atomic<bool> _stopRequested{false};

    /**
     * @brief 排空状态（C6）。只由分片线程读，由停止方写。
     *
     * 到期时刻用「先写时刻、再置标志（release）」的顺序发布：分片线程读到标志为真时，
     * 时刻一定已经写好。反过来写会出现「已经开始排空了但截止时刻还是 0」——
     * 那等于一进排空就超时，所有在飞请求立刻被砍。
     */
    std::atomic<bool> _draining{false};
    std::int64_t _drainStartedMs = 0;
    std::int64_t _drainDeadlineMs = 0;
    bool _drainPrepared = false;    /** 首次进入排空时做过一次批量关闭 */
    bool _drainTimedOut = false;
    std::size_t _drainSurvivors = 0;
    std::size_t _drainSurvivorBytes = 0;
    long long _drainElapsedMs = 0;

    /** 下一个连接标识。只在分片线程里自增 */
    std::uint64_t _nextConnectionId = 1;

    /** 本轮要销毁的连接（存标识）。只在分片线程里读写 */
    std::vector<std::uint64_t> _closing;
    /** 峰值连接数：退出时记一条，用于判断「分片是否被均衡分配」 */
    std::size_t _peakConnections = 0;

    /**
     * @brief 累计计数，只在分片线程里读写（所以不需要原子）。
     *
     * 存在的理由是「不要为正常事件刷日志」：1000 条连接正常断开会写 1000 行日志，
     * 真正的异常会被淹掉。正常关闭只累加计数、写 Trace；异常原因才写 Warn。
     * 退出时把这些数一次性打进日志，够回溯。
     */
    struct Counters
    {
        std::size_t frames = 0;         /** 成功拆出并处理的帧数 */
        std::size_t dispatched = 0;     /** 派给业务池的请求数 */
        std::size_t replies = 0;        /** 投回来并写进写缓冲的响应数 */
        std::size_t handlerFailed = 0;  /** 处理函数返回 false 的次数 */
        std::size_t droppedReplies = 0; /** 响应回来时连接已经没了，被丢弃的次数 */
        std::size_t peerClosed = 0;     /** 对端正常关闭 */
        std::size_t receiveError = 0;   /** 收出错或被 RST */
        std::size_t sendError = 0;      /** 发出错 */
        std::size_t invalidFrame = 0;   /** 帧头不合法 */
        std::size_t unknownCommand = 0; /** 指令名没有注册 */
        std::size_t slowConsumer = 0;   /** 写缓冲积压超限 */
        std::size_t halfFrameTimeout = 0; /** 半包超时被回收 */
        std::size_t idleTimeout = 0;      /** 空闲超时被回收 */
        std::size_t writeTimeout = 0;     /** 写超时被回收 */
        std::size_t serverShutdown = 0;   /** 停机时主动断开 */
    } _counters;

    /** 复用的事件缓冲，避免每轮都分配一次 */
    std::vector<PollerEvent> _ready;
    /** 复用的回投缓冲 */
    std::vector<ShardMailbox::Reply> _replies;
};

#endif // INKING_BACKEND_FRAMEWORK_SHARD_H
