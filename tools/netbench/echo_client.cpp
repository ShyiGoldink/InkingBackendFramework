// 通用 echo 压测客户端（A2 起一直用到 C 阶段）
//
// 用法：
//   stepA2_echo_client [条数] [消息内容] [端口] [主机] [-v]
//
//   条数      默认 1000
//   消息内容  默认 hello；写 #5000 表示 5000 个 'a'（测大消息不用敲长字符串）
//   端口      默认 9000
//   主机      默认 127.0.0.1
//   -v        逐条打印 send/recv 各调用了几次、每次返回多少字节
//
// 语义：一条 TCP 连接上发 N 条，每条都要完整收回来并逐字节比对。
//       只要有一条失败就非 0 退出（便于脚本判成功）。
//
// 保留的部分：一次 recv 不保证收全（TCP 是字节流），所以收的时候必须循环——
// 这正是 C1「半包/粘包」要处理的东西，现在先在客户端这边体会一遍。

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketType = SOCKET;
#define CLOSE_SOCKET closesocket
#define LAST_ERROR WSAGetLastError()
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using SocketType = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET close
#define LAST_ERROR errno
#endif

namespace
{
    /**把 "#5000" 这种写法展开成 5000 个 a；普通字符串原样返回 */
    std::string expandPayload(const std::string &raw)
    {
        if (raw.size() >= 2 && raw[0] == '#')
        {
            std::size_t count = 0;
            try
            {
                count = static_cast<std::size_t>(std::stoul(raw.substr(1)));
            }
            catch (...)
            {
                std::cerr << "消息内容写法不认识: " << raw << "（#后面要跟数字，例如 #5000）\n";
                std::exit(2);
            }
            return std::string(count, 'a');
        }
        return raw;
    }

    /**阻塞式发全：TCP 发送缓冲满了 send 会只发一部分，剩下的必须接着发 */
    bool sendAll(SocketType sock, const char *data, std::size_t size, int &callCount)
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            const int n = ::send(sock, data + sent, static_cast<int>(size - sent), 0);
            ++callCount;
            if (n <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    /**阻塞式收全：一次 recv 拿到的可能是半条，也可能是一条半，必须自己凑够 size */
    bool recvAll(SocketType sock, char *out, std::size_t size, int &callCount)
    {
        std::size_t got = 0;
        while (got < size)
        {
            const int n = ::recv(sock, out + got, static_cast<int>(size - got), 0);
            ++callCount;
            if (n <= 0)
            {
                return false;
            }
            got += static_cast<std::size_t>(n);
        }
        return true;
    }
}

int main(int argc, char **argv)
{
    std::size_t roundCount = 1000;
    std::string rawPayload = "hello";
    int port = 9000;
    std::string host = "127.0.0.1";
    bool verbose = false;

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
        {
            verbose = true;
        }
        else
        {
            positional.push_back(arg);
        }
    }

    if (!positional.empty())
    {
        roundCount = static_cast<std::size_t>(std::stoull(positional[0]));
    }
    if (positional.size() > 1)
    {
        rawPayload = positional[1];
    }
    if (positional.size() > 2)
    {
        port = std::stoi(positional[2]);
    }
    if (positional.size() > 3)
    {
        host = positional[3];
    }

    const std::string payload = expandPayload(rawPayload);

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup 失败\n";
        return 2;
    }
#endif

    const SocketType sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "创建 socket 失败: " << LAST_ERROR << "\n";
        return 2;
    }

    // 客户端自己要有超时，否则服务端不回声时脚本会永久挂住，看不出是「失败」还是「在等」
    {
        timeval tv{};
        tv.tv_sec = 10;
#ifdef _WIN32
        const char *opt = reinterpret_cast<const char *>(&tv);
#else
        const void *opt = &tv;
#endif
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, opt, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, opt, sizeof(tv));
    }
    {
        const int one = 1;
#ifdef _WIN32
        const char *opt = reinterpret_cast<const char *>(&one);
#else
        const void *opt = &one;
#endif
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, opt, sizeof(one));
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
    {
        std::cerr << "主机地址不合法: " << host << "\n";
        CLOSE_SOCKET(sock);
        return 2;
    }

    if (::connect(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        std::cerr << "连接 " << host << ":" << port << " 失败: " << LAST_ERROR
                  << "（服务端跑起来了吗？控制台里执行 network-run）\n";
        CLOSE_SOCKET(sock);
        return 2;
    }

    std::cout << "连接 " << host << ":" << port << " 成功，发 " << roundCount
              << " 条，每条 " << payload.size() << " 字节\n";

    std::vector<char> reply(payload.size());
    std::size_t successCount = 0;
    std::size_t failureCount = 0;
    std::size_t firstFailureRound = 0;
    std::string firstFailureReason;

    int totalSendCalls = 0;
    int totalRecvCalls = 0;

    const auto beginTime = std::chrono::steady_clock::now();
    for (std::size_t round = 1; round <= roundCount; ++round)
    {
        int sendCalls = 0;
        int recvCalls = 0;
        std::string reason;

        if (!sendAll(sock, payload.data(), payload.size(), sendCalls))
        {
            reason = "发送失败/超时";
        }
        else if (!recvAll(sock, reply.data(), reply.size(), recvCalls))
        {
            reason = "接收失败/超时（对端只回了一半也算）";
        }
        else if (std::memcmp(reply.data(), payload.data(), payload.size()) != 0)
        {
            reason = "内容不一致";
        }

        totalSendCalls += sendCalls;
        totalRecvCalls += recvCalls;

        if (reason.empty())
        {
            ++successCount;
        }
        else
        {
            ++failureCount;
            if (firstFailureRound == 0)
            {
                firstFailureRound = round;
                firstFailureReason = reason;
            }
        }

        if (verbose)
        {
            std::cout << "[" << round << "] send 调用 " << sendCalls << " 次, recv 调用 "
                      << recvCalls << " 次 -> " << (reason.empty() ? "ok" : reason) << "\n";
        }

        if (!reason.empty())
        {
            break; // 连接级失败之后再发也没有意义
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - beginTime;
    const double seconds = std::chrono::duration<double>(elapsed).count();

    std::cout << "----------------------------------------\n";
    std::cout << "成功 " << successCount << " 条，失败 " << failureCount << " 条\n";
    if (failureCount > 0)
    {
        std::cout << "首个失败: 第 " << firstFailureRound << " 条 -> " << firstFailureReason << "\n";
    }
    std::cout << "耗时 " << seconds << " 秒";
    if (seconds > 0.0)
    {
        std::cout << "，QPS " << static_cast<long long>(static_cast<double>(successCount) / seconds);
    }
    std::cout << "\n";
    std::cout << "send 总调用 " << totalSendCalls << " 次，recv 总调用 " << totalRecvCalls << " 次\n";

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return failureCount == 0 && successCount == roundCount ? 0 : 1;
}
