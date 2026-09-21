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
#include <unistd.h>
typedef int SocketType;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

inline constexpr const char *kNetModuleName = "Net";

#include "basic/ShineBasicModule.h"

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
};

#endif // INKING_BACKEND_FRAMEWORK_NET_H
