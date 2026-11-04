#ifndef INKING_BACKEND_FRAMEWORK_NET_H
#define INKING_BACKEND_FRAMEWORK_NET_H

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET SocketType;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
typedef int SocketType;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

inline constexpr const char *kNetModuleName = "Net";

#include "basic/ShineBasicModule.h"

#include <atomic>
#include <string>

/**
 * 本类是一个基础的网络模块，现在还是基础形态，之后会慢慢升级
 */
class Net : public ShineBasicModule
{
public:
    Net();
    ~Net();
    std::string moduleName() const override
    {
        return kNetModuleName;
    }

    void startUp(const int &port);
    /**启动网络服务并阻塞等待客户端 */
    void run();
    /**
     * @brief 请求事件循环退出。
     *
     * 只置标志、不关 socket：事件循环在自己的线程里发现标志后自行退出，
     * 连接清理仍由它负责。从别的线程 close 监听 fd 会和 accept/poll 抢同一个描述符，
     * 这类竞争在退出路径上表现为偶发崩溃，而不是稳定复现的错误。
     *
     * 可以在任意线程调用；实际退出延迟最多一个 poll 超时（1 秒）。
     */
    void stop();

private:
    /**申请服务器Socket */
    bool applyNetworkServer();
    /**创建专门负责客户端连接请求的Socket */
    bool createAcceptSocket();
    /**绑定 */
    bool stepBind(const int &port);
    /**监听*/
    bool stepListen();
    SocketType _acceptSocket = INVALID_SOCKET; /**一个专门负责接受客户端连接请求的socket */
    std::atomic<bool> _stopRequested{false};   /**退出请求：由 stop() 置位，事件循环自己检查 */
};

#endif // INKING_BACKEND_FRAMEWORK_NET_H
