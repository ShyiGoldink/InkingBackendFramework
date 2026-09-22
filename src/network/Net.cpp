#include "network/Net.h"

#include "thread/TaskQueueLoop.h"
#include "ui/UIMessageLibrary.h"

#include <system_error>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <thread>

namespace
{
    constexpr int STAGE_APPLY_NETWORK_SERVER = 1;
    constexpr int STAGE_CREATE_SOCKET = 2;
    constexpr int STAGE_BIND = 3;
    constexpr int STAGE_LISTEN = 4;
    constexpr int STAGE_START_UP = 5;
    // 这里后面应该要架着扔到分片去，不过这里我就暂时先不做了，等分片做好再说
}

Net::Net()
{
    registerToStatusChecker();
}

Net::~Net()
{
    if (_acceptSocket != INVALID_SOCKET)
    {
        closesocket(_acceptSocket);
        _acceptSocket = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

void Net::run()
{
    if (_acceptSocket != INVALID_SOCKET)
    {
        setStageStatus(
            STAGE_START_UP,
            "启动！",
            false,
            "网络服务已经启动，不能重复运行");
        return;
    }

    startUp(9000);
    if (_acceptSocket == INVALID_SOCKET)
    {
        return;
    }

    while (true)
    {
        sockaddr_in clientAddress = {};
#ifdef _WIN32
        int clientAddressLength = sizeof(clientAddress);
#else
        socklen_t clientAddressLength = sizeof(clientAddress);
#endif
        SocketType clientSocket = accept(
            _acceptSocket,
            reinterpret_cast<sockaddr *>(&clientAddress),
            &clientAddressLength);

        if (clientSocket == INVALID_SOCKET)
        {
            continue;
        }

        UIMessageLibrary::addMessage(
            MessageType::normal,
            0.0f,
            "接受连接");

        Task<std::any> clientTask;
        clientTask.action = [clientSocket](const std::vector<std::any> &) -> std::any
        {
            char buffer[4096];
            while (true)
            {
                const int receivedBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (receivedBytes == 0)
                {
                    UIMessageLibrary::addMessage(
                        MessageType::normal,
                        0.0f,
                        "收到 0 字节，对端关闭");
                    break;
                }

                if (receivedBytes < 0)
                {
                    break;
                }

                UIMessageLibrary::addMessage(
                    MessageType::normal,
                    0.0f,
                    "收到 " + std::to_string(receivedBytes) + " 字节");

                int sentBytes = 0;
                while (sentBytes < receivedBytes)
                {
                    const int currentSentBytes = send(
                        clientSocket,
                        buffer + sentBytes,
                        receivedBytes - sentBytes,
#ifdef _WIN32
                        0);
#else
                        MSG_NOSIGNAL);
#endif
                    if (currentSentBytes <= 0)
                    {
                        break;
                    }
                    sentBytes += currentSentBytes;
                }

                if (sentBytes < receivedBytes)
                {
                    break;
                }
            }

            closesocket(clientSocket);
            return {};
        };
        TaskQueueLoop::instance().addTask(std::move(clientTask));
    }
}

void Net::startUp(const int &port)
{
    if (_acceptSocket != INVALID_SOCKET)
    {
        setStageStatus(
            STAGE_START_UP,
            "启动！",
            false,
            "网络服务已经启动，不能重复创建监听Socket");
        return;
    }

    bool success = false;
    std::string errorMessage = "";
    if (applyNetworkServer() && createAcceptSocket() && stepBind(port) && stepListen())
    {
        success = true;
    }
    else
    {
        success = false;
        errorMessage = "请检查其它阶段报错";
    }
    setStageStatus(STAGE_START_UP, "启动！", success, errorMessage);
}

bool Net::applyNetworkServer()
{
    // 这两个变量原来声明在 #ifdef _WIN32 里面，非 Windows 分支就直接编不过了，
    // 提到外面来，两个分支都看得见。
    bool success = false;
    std::string errorMessage = "";
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (result == 0)
    {
        success = true;
    }
    else
    {
        success = false;
        int error = WSAGetLastError();
        std::error_code ec(error, std::system_category());
        errorMessage = "网络服务请求失败:" + ec.message();
    }
#else
    success = true;
#endif
    setStageStatus(STAGE_APPLY_NETWORK_SERVER, "申请本地的网络服务请求", success, errorMessage);

    return success;
}

bool Net::createAcceptSocket()
{
    bool success = false;
    std::string errorMessage = "";
    // 创建 socket
    _acceptSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_acceptSocket == INVALID_SOCKET)
    {
        success = false;
        errorMessage = "Socket创建请求失败:";
#ifdef _WIN32
        std::error_code ec(WSAGetLastError(), std::system_category());
#else
        std::error_code ec(errno, std::system_category());
#endif
        errorMessage = "Socket创建请求失败:" + ec.message();
    }
    else
    {
        int reuseAddress = 1;
#ifdef _WIN32
        const char *reuseAddressValue = reinterpret_cast<const char *>(&reuseAddress);
#else
        const void *reuseAddressValue = &reuseAddress;
#endif
        if (setsockopt(
                _acceptSocket,
                SOL_SOCKET,
                SO_REUSEADDR,
                reuseAddressValue,
                sizeof(reuseAddress)) == SOCKET_ERROR)
        {
            success = false;
#ifdef _WIN32
            std::error_code ec(WSAGetLastError(), std::system_category());
#else
            std::error_code ec(errno, std::system_category());
#endif
            errorMessage = "设置SO_REUSEADDR失败:" + ec.message();
            closesocket(_acceptSocket);
            _acceptSocket = INVALID_SOCKET;
        }
        else
        {
            success = true;
        }
    }
    setStageStatus(STAGE_CREATE_SOCKET, "创建接受连接请求专用的Socket", success, errorMessage);
    return success;
}

bool Net::stepBind(const int &port)
{
    bool success = false;
    std::string errorMessage = "";

    // 绑定
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(_acceptSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        success = false;
#ifdef _WIN32
        std::error_code ec(WSAGetLastError(), std::system_category());
#else
        std::error_code ec(errno, std::system_category());
#endif
        errorMessage = "Bind[绑定]请求失败:" + ec.message();
        closesocket(_acceptSocket);
        _acceptSocket = INVALID_SOCKET;
    }
    else
        success = true;

    setStageStatus(STAGE_BIND, "绑定端口", success, errorMessage);
    return success;
}

bool Net::stepListen()
{
    bool success = false;
    std::string errorMessage = "";
    // 监听
    if (listen(_acceptSocket, 128) == SOCKET_ERROR)
    {
        success = false;
#ifdef _WIN32
        std::error_code ec(WSAGetLastError(), std::system_category());
#else
        std::error_code ec(errno, std::system_category());
#endif
        errorMessage = "Listen[监听]请求失败:" + ec.message();
        closesocket(_acceptSocket);
        _acceptSocket = INVALID_SOCKET;
    }
    else
        success = true;

    setStageStatus(STAGE_LISTEN, "监听端口", success, errorMessage);
    return success;
}
