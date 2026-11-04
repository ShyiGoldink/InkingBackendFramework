#ifndef INKING_BACKEND_FRAMEWORK_NET_H
#define INKING_BACKEND_FRAMEWORK_NET_H

#include "basic/ShineBasicModule.h"
#include "network/NetworkStage.h"
#include "network/SocketOps.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

class Shard;

/**
 * @brief 网络模块的门面：建立监听、组织分片与接入线程、按序启停。
 *
 * 它自己**不碰任何连接**——连接的读写全部在分片线程里。这个类只负责三件事：
 * 1. 读配置（端口 / 分片数 / 每分片连接上限）；
 * 2. 建监听 socket、拉起 N 个分片与 1 条接入线程；
 * 3. 按「先停接入、再停分片、最后关监听」的顺序收回全部资源。
 *
 * 分层关系（从下往上）：
 *
 *   SocketOps   平台 socket 语义（非阻塞、错误码、发送标志）
 *   Poller      谁在等事件（epoll / WSAPoll）
 *   Wakeup      怎么从别的线程叫醒一个睡在 wait() 里的人
 *   Shard       一条线程 + 一个 Poller + 一批连接（连接的唯一所有者）
 *   Listener    只管 accept，然后把连接交给某个分片
 *   Net         装配与启停
 */
class Net : public ShineBasicModule
{
public:
    Net();
    ~Net() override;

    std::string moduleName() const override
    {
        return kNetModuleName;
    }

    void startUp(const int &port);

    /**
     * @brief 启动网络服务并阻塞，直到 stop() 被调用。
     *
     * 「阻塞」是刻意的：调用方（Application）用一条后台线程跑它，
     * 于是这条线程的结束就等于「网络服务已停」，不需要另外的完成信号。
     */
    void run();

    /**
     * @brief 请求停止。
     *
     * 只置标志并唤醒等待中的 run()，**不关任何 socket**：监听 fd 由 run() 在自己的
     * 收尾流程里关闭。从别的线程 close 监听 fd 会和 accept 抢同一个描述符，
     * 这类竞争在退出路径上表现为「偶发崩溃」，而不是稳定复现的错误。
     *
     * 可以在任意线程调用。
     */
    void stop();

    /**
     * @brief 当前分段计时与每分片连接的快照（供 `network-stat` 命令调用）。
     *
     * 从控制台线程调用，所以只碰分片里那些原子计数器；分片线程独占的字段一律不读。
     * 服务未运行时返回一句说明而不是空串——「没数据」和「没在跑」是两回事。
     */
    std::string profileReport();

private:
    /**申请本机网络服务（Windows 上是 WSAStartup） */
    bool applyNetworkServer();
    /**创建监听 socket 并设为非阻塞 */
    bool createAcceptSocket();
    /**绑定 */
    bool stepBind(const int &port);
    /**监听 */
    bool stepListen();
    /** 关闭监听 socket（只由 run() 的收尾流程调用） */
    void closeListenSocket();

    SocketType _acceptSocket = kInvalidSocket; /** 只管 accept 的监听 socket */

    /**
     * @brief 正在运行的分片（只在 run() 期间非空）。
     *
     * 存在的唯一原因是 `network-stat`：它需要从另一个线程找到分片。
     * 用指针而不是 unique_ptr，是因为所有权仍然在 run() 的局部变量里——
     * 这个表只是「借用一下看看」，不参与生命周期管理。
     */
    std::mutex _shardsMutex;
    std::vector<Shard *> _shards;

    std::atomic<bool> _stopRequested{false}; /** 退出请求：由 stop() 置位 */
    std::mutex _stopMutex;                   /** 只用于让 run() 睡在条件变量上 */
    std::condition_variable _stopCondition;  /** stop() 用它把 run() 叫醒 */
};

#endif // INKING_BACKEND_FRAMEWORK_NET_H
