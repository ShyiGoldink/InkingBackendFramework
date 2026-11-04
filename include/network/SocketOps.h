#ifndef INKING_BACKEND_FRAMEWORK_SOCKET_OPS_H
#define INKING_BACKEND_FRAMEWORK_SOCKET_OPS_H

/**
 * @file SocketOps.h
 * @brief 平台差异的唯一收口处：句柄类型、非阻塞、错误码语义、关闭与发送标志。
 *
 * 上层（Poller / Shard / Listener）从这以后只认 SocketType 与这里的几个函数，
 * 不再出现 #ifdef _WIN32。这一层的东西必须是「纯函数 + 纯常量」，不带任何状态：
 * 一旦它开始持有东西，就又要回答「谁先初始化、谁最后清理」这种问题。
 *
 * 三个必须统一的语义（设计文档 M1）：
 * 1. 句柄类型：Linux/macOS 是 int，Windows 是 SOCKET（无符号，INVALID_SOCKET 不是 -1）。
 * 2. 「现在不能收发但这不是错误」：Linux 是 EAGAIN/EWOULDBLOCK，Windows 是 WSAEWOULDBLOCK。
 * 3. 「对端已关闭」：统一以 recv 返回 0 为准，不依赖 POLLHUP（WSAPoll 上不可靠）。
 */

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <cstddef>
#include <string>

#ifdef _WIN32
/** Windows 的 socket 句柄不是 int，且无效值不是 -1：这条差异必须在这里就吃掉 */
using SocketType = SOCKET;
inline constexpr SocketType kInvalidSocket = INVALID_SOCKET;
#else
using SocketType = int;
inline constexpr SocketType kInvalidSocket = -1;
#endif

namespace SocketOps
{
    /**
     * @brief 进程级网络初始化。
     *
     * Windows 上必须走一次 WSAStartup，POSIX 上什么都不做。可以重复调用
     * （WSAStartup 自带引用计数），但语义上应该只在网络服务启动时调一次。
     */
    bool startUp(std::string &errorMessage);

    /** 与 startUp 配对。Windows 上是 WSACleanup，POSIX 上是空实现。 */
    void cleanUp();

    bool isValid(SocketType socket);

    /**
     * @brief 设为非阻塞。
     *
     * 这是后面所有事情的前提：阻塞的 recv/send 会把整条事件线程按在那里，
     * 一条慢连接就能拖死同一个分片里的其它几百条。
     */
    bool setNonBlocking(SocketType socket);

    bool setReuseAddress(SocketType socket);

    /**
     * @brief 打开 TCP_NODELAY（关闭 Nagle 合并）。
     *
     * 请求-响应型协议必须开：Nagle 会把小包攒着等 ACK，延迟直接拖到几十毫秒级。
     */
    bool setNoDelay(SocketType socket);

    /** 最近一次 socket 调用是不是「稍后再试」（EAGAIN / EWOULDBLOCK） */
    bool wouldBlock();

    /** 最近一次错误的可读描述，用于日志与自检消息 */
    std::string lastErrorText();

    /**
     * @brief 发数据时该带的标志位。
     *
     * Linux 用 MSG_NOSIGNAL 避免「对端已关，send 触发 SIGPIPE 直接杀进程」；
     * 没有这个宏的平台（macOS）返回 0，靠进程级忽略 SIGPIPE 兜底。
     */
    int sendFlags();

    /**
     * @brief 关闭句柄。
     *
     * 只做关闭，不做「从事件集合里摘除」——顺序反了会摘错人（fd 号会被复用），
     * 摘除由 Poller 负责，调用方必须先摘后关。
     */
    void close(SocketType socket);

    /** 半关闭写方向：告诉对端「我不再发了」，用于优雅关闭 */
    void shutdownWrite(SocketType socket);
}

#endif // INKING_BACKEND_FRAMEWORK_SOCKET_OPS_H
