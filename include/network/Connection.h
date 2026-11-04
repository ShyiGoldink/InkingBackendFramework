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

#include <cstddef>
#include <vector>

enum class ConnectionState
{
    Idle,
    InFlight,
    Writing,
    Closing
};

struct Connection
{
    SocketType socket = kInvalidSocket;
    ConnectionState state = ConnectionState::Idle;

    /** 已收到、还没被处理掉的字节。C1 接上 FrameCodec 之后它是半包/粘包的落点 */
    std::vector<char> receiveBuffer;

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

    bool isOpen() const
    {
        return socket != kInvalidSocket;
    }

    bool hasPendingSend() const
    {
        return sendOffset < sendBuffer.size();
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

    /** 写缓冲清空：整块丢掉并把游标归零，不要留着一个越来越大的空 vector */
    void clearSentData()
    {
        if (sendBuffer.empty())
        {
            return;
        }
        sendBuffer.clear();
        sendBuffer.shrink_to_fit();
        sendOffset = 0;
    }

    /**
     * @brief 标记为「本连接已死」。
     *
     * 只改状态、不关 fd：真正的 remove + close 集中在一轮事件处理结束之后的清理阶段。
     * 遍历中途关 fd 就是经典的 use-after-free（后面的回调还在拿这个指针）。
     */
    void markClosing()
    {
        state = ConnectionState::Closing;
        socket = kInvalidSocket;
    }
};

#endif // INKING_BACKEND_FRAMEWORK_CONNECTION_H
