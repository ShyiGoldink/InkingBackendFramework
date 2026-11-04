#ifndef INKING_BACKEND_FRAMEWORK_CONNECTION_H
#define INKING_BACKEND_FRAMEWORK_CONNECTION_H

/**
 * @file Connection.h
 * @brief 一条连接的全部状态（设计文档 M3 / 台阶 B2）。
 *
 * 所有权规则（比字段本身重要）：
 * - 连接对象**只由它所属的分片线程**读写，所以类里没有任何锁。
 * - 连接对象在分片里的容器中是稳定地址（unique_ptr），事件回调可以直接持有裸指针。
 * - 业务线程永远不持有这个指针，跨线程只传「连接标识 + 字节」。
 *
 * 状态机（「一条在飞」的正式定义，C2 依赖它）：
 *
 * | 状态     | Poller 关注 | 在飞 | 允许的动作            | 迁出条件              |
 * | -------- | ----------- | ---- | --------------------- | --------------------- |
 * | Idle     | 读          | 0    | 收字节、拆帧          | 拆出一整帧 → InFlight |
 * | InFlight | 无          | 1    | 等业务线程回投        | 收到响应字节 → Writing|
 * | Writing  | 写          | 1    | send 写缓冲           | 写缓冲清空 → Idle     |
 * | Closing  | 无          | ≥0   | 排空写缓冲或直接断    | 本轮到清理阶段 → 销毁 |
 *
 * 当前阶段（B3）只有 echo：读到的字节直接进写缓冲，所以走的是 Idle ↔ Writing。
 * InFlight 现在没有使用点，保留它是因为「一条在飞」是整个架构的承重墙，
 * 状态枚举要先把位置留出来，否则 C2 接入时又会有人想绕过它。
 */

#include "network/Poller.h"
#include "network/SocketOps.h"
#include "protocol/FrameCodec.h"

#include <cstddef>
#include <cstdint>
#include <vector>

enum class ConnectionState
{
    Idle,
    InFlight,
    Writing,
    Closing
};

/**
 * @brief 连接为什么被关掉。
 *
 * 分成枚举而不是「随手写一条日志」，是因为这几类的处理方式不同：
 * 对端关闭是正常事件（不告警），收/发错误是环境问题（要能统计），
 * 非法帧和慢消费者是**对端行为问题**（要能一眼看出被谁拖累）。
 * 混在一起记日志的话，1000 条正常断开会把真正的异常淹掉。
 */
enum class CloseReason
{
    None,
    PeerClosed,     /** recv 返回 0：对端正常关闭 */
    ReceiveError,   /** recv 报错，或被对端 RST */
    SendError,      /** send 报错 */
    InvalidFrame,   /** 帧头不合法：这条流已不可信 */
    UnknownCommand, /** 指令名没有注册：对端在说本服务端不支持的话 */
    SlowConsumer,   /** 写缓冲积压超过上限：对端只发不读 */

    /**
     * 以下三类来自定时器（C5）。它们和上面几种的区别是「没有发生任何事件」——
     * 连接不是被谁弄坏的，是**没人用它**。单独列出来，是因为这三种故障的排查方向
     * 完全不同：半包超时看协议实现对不对，空闲超时看客户端有没有保活，写超时看对端
     * 读得动读不动。
     */
    HalfFrameTimeout, /** 半包超时：收到一部分帧之后再没动静 */
    IdleTimeout,      /** 空闲超时：整条连接长时间没有完整交互 */
    WriteTimeout,     /** 写超时：写缓冲长时间没发完 */

    /**
     * 服务端主动停机（C6）。它和前两种的语义正好相反：不是「这条连接有问题」，
     * 而是「我们不等了」。所以它在日志里不是告警，连接也可能是在一次完整交互
     * 结束之后被干净关掉的。
     */
    ServerShutdown,
};

struct Connection
{
    /**
     * @brief 分片分配的连接标识（自增，永不重复）。
     *
     * 这是跨线程传递的「连接标识」——业务线程只拿它，不拿指针、不拿 socket。
     * 为什么不用 socket 号：内核会复用 fd，业务线程手里那个号可能在它回来的路上
     * 就已经属于另一条新连接了；自增标识没有这个问题，也就不需要「代次号」。
     */
    std::uint64_t id = 0;

    SocketType socket = kInvalidSocket;
    ConnectionState state = ConnectionState::Idle;

    /**
     * @brief 这条连接的帧解析器，同时也是这条连接唯一的读缓冲。
     *
     * **每连接一份**，不是每分片一份：`FrameCodec` 内部有「已收到但还没拆出一帧」的
     * 缓冲，那是连接级状态——两条连接的半帧混进同一个缓冲，字节就串了。
     *
     * 原来这里另有一个 `receiveBuffer`，接上 `FrameCodec` 之后两者是同一件事的
     * 两个位置，保留两份必须手工同步，漏一次就是半包丢失。所以**只留一份**：
     * 缓冲在 `FrameCodec` 里，`pendingBytes()` 就是「还没拼成整帧的字节数」。
     */
    FrameCodec codec;

    /**
     * 还没发完的字节 + 发到哪儿的游标。
     *
     * 非阻塞 send 的返回值是「这次真正发出去的字节数」，可能是 0（发送缓冲满），
     * 所以游标必须留着，等下一次可写事件接着发——这就是写缓冲存在的唯一理由。
     */
    std::vector<char> sendBuffer;
    std::size_t sendOffset = 0;

    /**
     * @brief 当前在 Poller 里关注的事件位。
     *
     * 记这一份的唯一目的是「不必要就不改」：epoll_ctl 是一次系统调用，
     * 每条事件都无脑 modify 一遍，等于把省下来的时间又还回去。
     */
    unsigned interest = PollerBits::Read;

    /** 断开原因，只在关闭时写一次；用于计数与「这条连接为什么没了」的追溯 */
    CloseReason closeReason = CloseReason::None;

    /**
     * @brief 这次请求投给业务池的时刻（单调时钟纳秒）。
     *
     * 只给分段计时用：响应回来时减一下，就是「业务往返」——包含池排队、执行、
     * 以及投回分片的唤醒延迟。这一段的数值直接决定该去优化池还是优化网络层。
     * 不开启计时时它是 0，不会被读。
     */
    std::int64_t dispatchedAtNs = 0;

    bool isOpen() const
    {
        // 注意是「两个条件」：socket 还有效，且没有进入关闭流程。
        // 早先版本用 socket = kInvalidSocket 当「已关」标记，结果清理阶段拿不到
        // fd 去摘事件集合——把「状态」和「句柄」分开，两种用途才不会互相吃掉。
        return socket != kInvalidSocket && state != ConnectionState::Closing;
    }

    bool hasPendingSend() const
    {
        return sendOffset < sendBuffer.size();
    }

    /** 还没发出去的字节数（背压判定的依据） */
    std::size_t pendingSendBytes() const
    {
        return sendBuffer.size() - sendOffset;
    }

    /** 把待发送的字节接在写缓冲后面（不会覆盖没发完的部分） */
    void appendSend(const char *data, std::size_t length)
    {
        if (length == 0)
        {
            return;
        }
        sendBuffer.insert(sendBuffer.end(), data, data + length);
    }

    /**
     * @brief 写缓冲清空。
     *
     * 小缓冲不还内存：echo 这种「发完立刻空」的路径上，每次 clear 都顺手 shrink
     * 会变成每请求一次 malloc/free，热路径上很贵。只有大包（> 64 KiB）才真的
     * 把容量还回去——那才是「一条连接长期占着大块内存」的场景。
     */
    void clearSentData()
    {
        if (sendBuffer.empty())
        {
            return;
        }
        sendBuffer.clear();
        sendOffset = 0;
        if (sendBuffer.capacity() > 64 * 1024)
        {
            sendBuffer.shrink_to_fit();
        }
    }

    /**
     * @brief 标记为「本连接已死」。
     *
     * 只改状态、不关 fd、也不清 socket：真正的 remove + close 集中在一轮事件处理
     * 结束之后的清理阶段。遍历中途关 fd 就是经典的 use-after-free
     * （后面的回调还在拿这个指针）。
     */
    void markClosing()
    {
        state = ConnectionState::Closing;
    }
};

#endif // INKING_BACKEND_FRAMEWORK_CONNECTION_H
