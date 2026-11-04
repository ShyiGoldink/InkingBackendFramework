#ifndef INKING_BACKEND_FRAMEWORK_SHARD_H
#define INKING_BACKEND_FRAMEWORK_SHARD_H

/**
 * @file Shard.h
 * @brief 分片：一条线程 + 一个事件集合 + 一批连接（设计文档 M4 / 台阶 B3）。
 *
 * 分片是这套架构的性能单元与隔离单元。为什么必须是一条**独立**线程、
 * 而不是塞进 ThreadPool：
 * - 池子的前提是线程可以互换，谁醒了谁干；分片恰好相反，它绑定固定的 fd 与缓冲，
 *   不能换人干，塞进池子等于让池线程变成专用线程，池子名存实亡；
 * - 池子的抽象是「共享谓词 + 共享执行体」，没有「每条线程各自一套循环状态」这个东西。
 *
 * 线程归属规则（这是本文件最重要的约束，比代码本身重要）：
 * 1. 一条连接在任一时刻只属于一个分片，**只有该分片线程**能读写它的 socket 与缓冲。
 *    ——所以本类的成员几乎没有锁，只在交接队列和计数上加了锁。
 * 2. 别的线程想影响分片，只有两个口子：`adopt()`（交一条新连接进来）和
 *    `requestStop()`（要求退出）。两者都只碰队列/原子变量，然后唤醒分片。
 * 3. 从 Poller 摘除必须在 close 之前，且都在分片线程上按顺序做。
 */

#include "network/Connection.h"
#include "network/Poller.h"
#include "network/Wakeup.h"

#include <atomic>
#include <cstddef>
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

    /** 退出路径：交接队列里还没来得及接的连接也要关掉，否则 fd 泄漏 */
    void closeIncoming();

    void handleEvent(const PollerEvent &event);
    void readFrom(Connection *connection);

    /**
     * @brief 把 `FrameCodec` 里已经凑齐的帧一帧一帧取出来处理。
     *
     * @return false 表示这条连接已经在处理过程中被关掉，调用方不要再碰它。
     *
     * 读一次字节 → 循环拆帧直到 `NeedMoreData`，是 M6 要求的形状：
     * 一次 `recv` 可能拿到半帧、一帧、多帧，三件事都要在同一条路径上处理。
     */
    bool consumeFrames(Connection *connection);

    /** 处理一条完整消息。C3 之前这里是回显（原样组帧发回） */
    bool replyFrame(Connection *connection, const CommandFrame &frame);

    void flush(Connection *connection);

    /** 更新关注的事件位；和当前一致时不做系统调用 */
    void updateInterest(Connection *connection);

    /** 只做登记：真正的摘除 + close 在一轮事件处理结束之后的清理阶段 */
    void closeConnection(Connection *connection, CloseReason reason);
    void removeClosedConnections();
    void closeAllConnections();

    const std::size_t _index;
    const std::size_t _maxConnections;

    std::thread _thread;

    /**
     * 声明顺序就是销毁顺序的逆序，所以这两个成员的位置有讲究：
     * 先销毁 Poller（它持有的集合里还挂着唤醒器那个 fd），再销毁唤醒器。
     * 反过来写也能跑，但「集合比它登记过的 fd 活得久」是个没必要留的隐患。
     */
    Wakeup _wakeup;
    std::unique_ptr<Poller> _poller;

    /** 连接表：socket -> 连接。用 unique_ptr 保证地址稳定，回调里的裸指针长期有效 */
    std::unordered_map<SocketType, std::unique_ptr<Connection>> _connections;

    /** 交接队列：acceptor 线程写，分片线程整批取走 */
    std::mutex _incomingMutex;
    std::vector<SocketType> _incoming;

    std::atomic<std::size_t> _connectionCount{0};
    std::atomic<bool> _stopRequested{false};

    /** 本轮要销毁的连接。只在分片线程里读写 */
    std::vector<SocketType> _closing;
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
        std::size_t frames = 0;        /** 成功拆出并处理的帧数 */
        std::size_t peerClosed = 0;    /** 对端正常关闭 */
        std::size_t receiveError = 0;  /** 收出错或被 RST */
        std::size_t sendError = 0;     /** 发出错 */
        std::size_t invalidFrame = 0;  /** 帧头不合法 */
        std::size_t slowConsumer = 0;  /** 写缓冲积压超限 */
    } _counters;

    /** 复用的事件缓冲，避免每轮都分配一次 */
    std::vector<PollerEvent> _ready;
};

#endif // INKING_BACKEND_FRAMEWORK_SHARD_H
