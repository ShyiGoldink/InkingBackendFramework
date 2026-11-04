#ifndef INKING_BACKEND_FRAMEWORK_LISTENER_H
#define INKING_BACKEND_FRAMEWORK_LISTENER_H

/**
 * @file Listener.h
 * @brief 接入线程：只管 accept，并把新连接的**所有权**交给某个分片（设计文档 M5）。
 *
 * 为什么接入要单独一条线程，而不是让 0 号分片顺手 accept：
 * 接入是一条独立路径，它有自己独有的过载模式——连接风暴。风暴期间如果接入和收发
 * 挤在同一个循环里，一次风暴会把某个分片里已经在服务的几百条连接一起拖慢；
 * 而且 0 号分片会因此比别的分片多做一件事，压测时很容易被误判成「分片不均衡」。
 *
 * 这个类只有三条职责：
 * 1. 循环 accept 到 EAGAIN 为止（不是一次一个：一次一个会把风暴摊成很多轮等待）；
 * 2. 按「当前连接数最少」挑一个分片，把 fd 交接过去（只交接，从此不碰）；
 * 3. 所有分片都满时明确拒绝并记日志，而不是闷掉——闷掉最难的排查。
 *
 * 监听 socket 的所有权在 Net（它负责建、它负责关），本类只是借用。
 * 因此 stop 的顺序必须是：先停本线程并 join，再由 Net 关监听 fd。
 */

#include "network/Poller.h"
#include "network/SocketOps.h"
#include "network/Wakeup.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class Shard;

class Listener
{
public:
    /**
     * @param listenSocket 已经在监听、且已是非阻塞的 socket，本类不负责关闭它
     * @param shards 分片表，本类只持有裸指针（分片由 Net 持有并保证活得更久）
     * @param maxConnectionsPerShard 用于判断「所有分片是否都已满」
     */
    Listener(SocketType listenSocket, const std::vector<Shard *> &shards,
             std::size_t maxConnectionsPerShard);
    ~Listener();

    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;
    Listener(Listener &&) = delete;
    Listener &operator=(Listener &&) = delete;

    bool start(std::string &errorMessage);
    void requestStop();
    void join();

    /** 累计接受过多少条连接，只用于收尾日志 */
    std::size_t acceptedCount() const
    {
        return _acceptedCount.load(std::memory_order_relaxed);
    }

private:
    void serve();
    void acceptConnections();

    /**
     * @brief 挑一个分片。
     *
     * 按当前连接数最少挑，而不是按 fd 取模：取模在连接生命周期长短差异大时会倾斜
     * （短连接集中落在模运算相同的槽里），而接入路径上做一次 O(分片数) 的扫描
     * 成本可以忽略——分片数是个位数。
     *
     * @return 所有分片都已满时返回 nullptr
     */
    Shard *pickShard();

    const SocketType _listenSocket;
    const std::vector<Shard *> _shards;
    const std::size_t _maxConnectionsPerShard;

    std::thread _thread;

    /** 声明顺序即销毁顺序的逆序：Poller 先于唤醒器销毁，理由同 Shard */
    Wakeup _wakeup;
    std::unique_ptr<Poller> _poller;
    std::atomic<bool> _stopRequested{false};
    std::atomic<std::size_t> _acceptedCount{0};
    std::vector<PollerEvent> _ready;
};

#endif // INKING_BACKEND_FRAMEWORK_LISTENER_H
