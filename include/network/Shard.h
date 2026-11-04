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
#include "network/Poller.h"
#include "network/ShardMailbox.h"
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
     */
    Shard(std::size_t index, std::size_t maxConnections);
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

    /** 只做登记：真正的摘除 + close 在一轮事件处理结束之后的清理阶段 */
    void closeConnection(Connection *connection, CloseReason reason);
    void removeClosedConnections();
    void closeAllConnections();

    Connection *findConnection(std::uint64_t id);

    const std::size_t _index;
    const std::size_t _maxConnections;

    std::thread _thread;

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
    } _counters;

    /** 复用的事件缓冲，避免每轮都分配一次 */
    std::vector<PollerEvent> _ready;
    /** 复用的回投缓冲 */
    std::vector<ShardMailbox::Reply> _replies;
};

#endif // INKING_BACKEND_FRAMEWORK_SHARD_H
