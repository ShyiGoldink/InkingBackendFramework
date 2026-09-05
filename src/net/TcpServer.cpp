#include "net/TcpServer.h"

#include "basic/ShineLog.h"
#include "net/SimpleTextProtocol.h"
#include "ui/UIMessageLibrary.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline constexpr int kShutdownBoth = SD_BOTH;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
inline constexpr int kShutdownBoth = SHUT_RDWR;
#endif

namespace
{
    constexpr int STAGE_SOCKET_LIBRARY = 1;
    constexpr int STAGE_CREATE_LISTENER = 2;
    constexpr int STAGE_START_ACCEPT = 3;
    constexpr int STAGE_STOP_SERVER = 4;

    constexpr std::size_t kReceiveChunkSize = 4096;
    constexpr std::size_t kMaxPacketBytes = 64 * 1024;    /** 单条协议文本上限 */
    constexpr std::size_t kMaxPendingBytes = 256 * 1024;  /** 未拆完缓冲区上限 */
    constexpr int kSelectTimeoutMs = 200;                 /** accept 轮询间隔，用于响应停止 */
    constexpr int kSendTimeoutMs = 5000;                  /** 发送超时，防止 join 永久阻塞 */

    std::string socketErrorText()
    {
#ifdef _WIN32
        return "WSA error " + std::to_string(WSAGetLastError());
#else
        return std::strerror(errno);
#endif
    }

    void closeSocket(SocketHandle socket)
    {
        if (socket == kInvalidSocket)
        {
            return;
        }
#ifdef _WIN32
        ::closesocket(socket);
#else
        ::close(socket);
#endif
    }

    /** 把整数 id 转换成 SocketHandle */
    SocketHandle toSocket(std::intptr_t value)
    {
        return static_cast<SocketHandle>(value);
    }

    bool sendAll(SocketHandle socket, const std::string &text)
    {
        std::size_t sent = 0;
        while (sent < text.size())
        {
#ifdef _WIN32
            const int result = ::send(socket, text.data() + sent,
                                      static_cast<int>(text.size() - sent), 0);
#else
            const ssize_t result = ::send(socket, text.data() + sent,
                                          text.size() - sent, MSG_NOSIGNAL);
#endif
            if (result <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(result);
        }
        return true;
    }

    std::string describePeer(const sockaddr_storage &address)
    {
        char host[NI_MAXHOST] = {};
        char service[NI_MAXSERV] = {};
#ifdef _WIN32
        const int addressLength = static_cast<int>(sizeof(address));
#else
        const socklen_t addressLength = sizeof(address);
#endif
        if (::getnameinfo(reinterpret_cast<const sockaddr *>(&address), addressLength,
                          host, sizeof(host), service, sizeof(service),
                          NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        {
            return std::string(host) + ":" + service;
        }
        return "unknown";
    }

#ifdef _WIN32
    std::mutex gSocketLibraryMutex;
    int gSocketLibraryRefCount = 0;
    bool gSocketLibraryReady = false;

    /** 获取 Winsock 环境引用（计数递增） */
    bool acquireSocketLibrary()
    {
        std::lock_guard<std::mutex> lock(gSocketLibraryMutex);
        if (gSocketLibraryRefCount == 0)
        {
            WSADATA wsaData;
            gSocketLibraryReady = ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
        }
        if (gSocketLibraryReady)
        {
            ++gSocketLibraryRefCount;
        }
        return gSocketLibraryReady;
    }

    /** 释放 Winsock 环境引用（引用归零时清理） */
    void releaseSocketLibrary()
    {
        std::lock_guard<std::mutex> lock(gSocketLibraryMutex);
        if (gSocketLibraryReady && gSocketLibraryRefCount > 0)
        {
            --gSocketLibraryRefCount;
            if (gSocketLibraryRefCount == 0)
            {
                ::WSACleanup();
                gSocketLibraryReady = false;
            }
        }
    }
#endif
}

/** 客户端连接的运行时数据（仅 TcpServer.cpp 内部可见） */
struct TcpServer::Client
{
    uint64_t id = 0;
    SocketHandle socket = kInvalidSocket;
    std::string remote;                 /** ip:port */
    std::atomic<bool> finished{false};  /** 接收线程是否已经结束 */
};

TcpServer::TcpServer()
{
    registerStages();
    registerToStatusChecker();
    // 默认协议：文本协议“指令/数据”，确认后如需替换实现，注入新 IProtocol 即可
    _protocol = std::make_shared<SimpleTextProtocol>();
    // 默认业务处理：回显原消息，方便先跑通联调
    _handler = [this](const NetMessage &request)
    { return defaultHandler(request); };
}

TcpServer::~TcpServer()
{
    stop();
}

void TcpServer::registerStages()
{
    setStageDetail(STAGE_SOCKET_LIBRARY, "初始化系统 socket 环境", "如果是 Windows，请确认 Winsock 可正常初始化。");
    setStageDetail(STAGE_CREATE_LISTENER, "创建监听 socket 并绑定端口", "端口被占用或权限不足时请换一个端口后重新 net-start。");
    setStageDetail(STAGE_START_ACCEPT, "启动 accept 线程", "accept 线程退出后请检查监听 socket 状态。");
    setStageDetail(STAGE_STOP_SERVER, "停止服务器并清理连接", "如果无法停止，请检查是否有连接线程阻塞。");
}

void TcpServer::setPort(uint16_t port)
{
    _port = port;
}

uint16_t TcpServer::port() const
{
    return _port;
}

void TcpServer::setProtocol(std::shared_ptr<IProtocol> protocol)
{
    if (protocol)
    {
        _protocol = std::move(protocol);
    }
}

void TcpServer::setMessageHandler(MessageHandler handler)
{
    if (handler)
    {
        _handler = std::move(handler);
    }
}

bool TcpServer::start()
{
    if (_running.load())
    {
        return true;
    }
    if (_acceptWorker.joinable())
    {
        ShineLog::error(kTcpServerModuleName, "上一次 accept 线程尚未清理完成，不能再次启动");
        return false;
    }

#ifdef _WIN32
    if (!acquireSocketLibrary())
    {
        setStageStatus(STAGE_SOCKET_LIBRARY, "初始化 socket 环境", false, socketErrorText());
        return false;
    }
    _socketReady = true;
#endif
    setStageStatus(STAGE_SOCKET_LIBRARY, "初始化 socket 环境", true, "socket 环境就绪");

    _listener = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, 0));
    SocketHandle listenerSocket = toSocket(_listener);
    if (listenerSocket == kInvalidSocket)
    {
        setStageStatus(STAGE_CREATE_LISTENER, "创建监听 socket", false, socketErrorText());
#ifdef _WIN32
        releaseSocketLibrary();
        _socketReady = false;
#endif
        return false;
    }

    // 允许端口快速复用，避免刚退出时立刻重启失败
    int reuse = 1;
    ::setsockopt(listenerSocket, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(_port);

    if (::bind(listenerSocket, reinterpret_cast<const sockaddr *>(&serverAddress),
               sizeof(serverAddress)) == -1)
    {
        const std::string reason = "绑定 0.0.0.0:" + std::to_string(_port) + " 失败: " + socketErrorText();
        setStageStatus(STAGE_CREATE_LISTENER, "绑定监听端口", false, reason);
        closeSocket(listenerSocket);
        _listener = -1;
#ifdef _WIN32
        releaseSocketLibrary();
        _socketReady = false;
#endif
        return false;
    }

    if (::listen(listenerSocket, SOMAXCONN) == -1)
    {
        const std::string reason = "监听失败: " + socketErrorText();
        setStageStatus(STAGE_CREATE_LISTENER, "进入监听状态", false, reason);
        closeSocket(listenerSocket);
        _listener = -1;
#ifdef _WIN32
        releaseSocketLibrary();
        _socketReady = false;
#endif
        return false;
    }

    setStageStatus(STAGE_CREATE_LISTENER, "创建监听 socket", true,
                   "已监听 0.0.0.0:" + std::to_string(_port));

    _running.store(true);
    _acceptWorker = std::thread(&TcpServer::acceptLoop, this);
    setStageStatus(STAGE_START_ACCEPT, "启动 accept 线程", true, "accept 线程已启动");

    UIMessageLibrary::addMessage(MessageType::pass, 0.0f,
                                 "TCP 服务器已启动: 0.0.0.0:" + std::to_string(_port));
    return true;
}

void TcpServer::stop()
{
    _running.store(false);

    // 先停 accept 线程：acceptLoop 通过 select 轮询退出，并负责关闭监听 socket
    if (_acceptWorker.joinable())
    {
        _acceptWorker.join();
    }

    // 唤醒阻塞在 recv 的客户端线程，避免 join 卡死
    {
        std::lock_guard<std::mutex> lock(_clientMutex);
        for (const auto &client : _clients)
        {
            ::shutdown(client->socket, kShutdownBoth);
        }
    }
    cleanupAllClients();

    if (_listener != -1)
    {
        closeSocket(toSocket(_listener));
        _listener = -1;
    }

#ifdef _WIN32
    if (_socketReady)
    {
        releaseSocketLibrary();
        _socketReady = false;
    }
#endif
    setStageStatus(STAGE_STOP_SERVER, "停止 TCP 服务器", true, "TCP 服务器已停止");
}

bool TcpServer::isRunning() const
{
    return _running.load();
}

std::size_t TcpServer::clientCount() const
{
    std::lock_guard<std::mutex> lock(_clientMutex);
    return _clients.size();
}

std::vector<std::string> TcpServer::clientDescriptions() const
{
    std::lock_guard<std::mutex> lock(_clientMutex);
    std::vector<std::string> descriptions;
    descriptions.reserve(_clients.size());
    for (const auto &client : _clients)
    {
        descriptions.push_back("[" + std::to_string(client->id) + "] " + client->remote);
    }
    return descriptions;
}

void TcpServer::acceptLoop()
{
    const SocketHandle listenerSocket = toSocket(_listener);

    while (_running.load())
    {
        // 顺带回收已经断开连接的客户端线程
        cleanupFinishedClients();

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenerSocket, &readSet);

        timeval waitTime{};
        waitTime.tv_sec = 0;
        waitTime.tv_usec = kSelectTimeoutMs * 1000;

        const int ready = ::select(static_cast<int>(listenerSocket) + 1,
                                   &readSet, nullptr, nullptr, &waitTime);
        if (!_running.load())
        {
            break;
        }
        if (ready == 0)
        {
            continue; // 超时：回到循环顶部检查退出标志
        }
        if (ready < 0)
        {
#ifdef _WIN32
            const int errorCode = WSAGetLastError();
            if (errorCode == WSAEINTR)
            {
                continue;
            }
#else
            if (errno == EINTR)
            {
                continue;
            }
#endif
            ShineLog::error(kTcpServerModuleName, "select 失败: " + socketErrorText());
            break;
        }

        sockaddr_storage peerAddress{};
#ifdef _WIN32
        int peerAddressLength = sizeof(peerAddress);
#else
        socklen_t peerAddressLength = sizeof(peerAddress);
#endif
        const SocketHandle clientSocket = ::accept(
            listenerSocket, reinterpret_cast<sockaddr *>(&peerAddress), &peerAddressLength);
        if (clientSocket == kInvalidSocket)
        {
            if (!_running.load())
            {
                break;
            }
            continue;
        }
        if (!_running.load())
        {
            closeSocket(clientSocket);
            break;
        }

        // 发送超时：对端不读时，5 秒后发送失败，避免线程无法回收
#ifdef _WIN32
        const int sendTimeoutMs = kSendTimeoutMs;
        ::setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&sendTimeoutMs), sizeof(sendTimeoutMs));
#else
        timeval sendTimeout{};
        sendTimeout.tv_sec = kSendTimeoutMs / 1000;
        sendTimeout.tv_usec = (kSendTimeoutMs % 1000) * 1000;
        ::setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&sendTimeout), sizeof(sendTimeout));
#endif

        const std::string remote = describePeer(peerAddress);
        auto client = std::make_unique<Client>();
        client->id = _nextClientId++;
        client->socket = clientSocket;
        client->remote = remote;
        const uint64_t clientId = client->id;
        const std::string remoteCopy = remote;

        // 先在锁内登记连接，再启动接收线程，保证停止/清理时一定能找到该连接
        {
            std::lock_guard<std::mutex> lock(_clientMutex);
            _clients.push_back(std::move(client));
            _clientWorkers.emplace(
                clientId,
                std::thread(&TcpServer::clientLoop, this,
                            clientId, static_cast<std::intptr_t>(clientSocket), remoteCopy));
        }

        ShineLog::pass(kTcpServerModuleName, "客户端接入 [" + remote + "]");
        UIMessageLibrary::addMessage(MessageType::pass, 0.0f,
                                     "客户端接入: " + remote + " (当前 " +
                                         std::to_string(clientCount()) + " 个连接)");
    }

    closeSocket(listenerSocket);
    _listener = -1;
}

void TcpServer::clientLoop(uint64_t clientId, std::intptr_t clientSocketValue, std::string remote)
{
    const SocketHandle clientSocket = toSocket(clientSocketValue);
    std::string buffer;
    std::array<char, kReceiveChunkSize> chunk{};

    while (_running.load())
    {
        const int received = ::recv(clientSocket, chunk.data(),
                                    static_cast<int>(chunk.size()), 0);
        if (received > 0)
        {
            buffer.append(chunk.data(), static_cast<std::size_t>(received));
            if (buffer.size() > kMaxPendingBytes)
            {
                ShineLog::error(kTcpServerModuleName,
                                remote + " 接收缓冲区超过上限，强制断开");
                break;
            }
            if (!processReceivedBytes(clientSocket, remote, buffer))
            {
                break;
            }
        }
        else if (received == 0)
        {
            break; // 对端关闭
        }
        else
        {
            // 出错：可能是 stop() 主动 shutdown 唤醒，也可能对端异常断开
            break;
        }
    }

    ShineLog::write(kTcpServerModuleName, "连接断开 [" + remote + "]");
    markClientFinished(clientId);
}

void TcpServer::markClientFinished(uint64_t clientId)
{
    std::lock_guard<std::mutex> lock(_clientMutex);
    for (const auto &client : _clients)
    {
        if (client->id == clientId)
        {
            client->finished.store(true);
            break;
        }
    }
}

void TcpServer::cleanupFinishedClients()
{
    struct LeavingClient
    {
        SocketHandle socket = kInvalidSocket;
        std::thread worker;
    };
    std::vector<LeavingClient> leaving;

    {
        std::lock_guard<std::mutex> lock(_clientMutex);
        for (auto it = _clients.begin(); it != _clients.end();)
        {
            if (!(*it)->finished.load())
            {
                ++it;
                continue;
            }

            LeavingClient item;
            item.socket = (*it)->socket;
            const auto workerIt = _clientWorkers.find((*it)->id);
            if (workerIt != _clientWorkers.end())
            {
                item.worker = std::move(workerIt->second);
                _clientWorkers.erase(workerIt);
            }
            it = _clients.erase(it);
            leaving.push_back(std::move(item));
        }
    }

    // 锁外 join，避免客户端线程在 markClientFinished 等待锁时死锁
    for (auto &item : leaving)
    {
        if (item.worker.joinable())
        {
            item.worker.join();
        }
        closeSocket(item.socket);
    }
}

void TcpServer::cleanupAllClients()
{
    cleanupFinishedClients();

    struct LeavingClient
    {
        SocketHandle socket = kInvalidSocket;
        std::thread worker;
    };
    std::vector<LeavingClient> leaving;

    {
        std::lock_guard<std::mutex> lock(_clientMutex);
        for (const auto &client : _clients)
        {
            LeavingClient item;
            item.socket = client->socket;
            const auto workerIt = _clientWorkers.find(client->id);
            if (workerIt != _clientWorkers.end())
            {
                item.worker = std::move(workerIt->second);
                _clientWorkers.erase(workerIt);
            }
            leaving.push_back(std::move(item));
        }
        _clients.clear();
    }

    for (auto &item : leaving)
    {
        if (item.worker.joinable())
        {
            item.worker.join();
        }
        closeSocket(item.socket);
    }
}

bool TcpServer::processReceivedBytes(std::intptr_t clientSocketValue,
                                     const std::string &remote, std::string &buffer)
{
    const SocketHandle clientSocket = toSocket(clientSocketValue);

    // 一条消息 = 一个 '\n' 结尾的文本行；一次循环把缓冲区里完整行全部处理完
    while (true)
    {
        const std::size_t newlinePosition = buffer.find('\n');
        if (newlinePosition == std::string::npos)
        {
            return true; // 剩余数据不完整，等后续数据到达
        }

        std::string line = buffer.substr(0, newlinePosition);
        buffer.erase(0, newlinePosition + 1);

        // 兼容 Windows 的 \r\n
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue; // 空行直接忽略
        }
        if (line.size() > kMaxPacketBytes)
        {
            ShineLog::error(kTcpServerModuleName,
                            remote + " 单条消息超过 " + std::to_string(kMaxPacketBytes) + " 字节，强制断开");
            return false;
        }

        NetMessage message;
        if (!_protocol || !_protocol->decode(line, message))
        {
            // 协议确认前，先记录无法解析的行；不回包，避免双方对错误格式反复交互
            ShineLog::error(kTcpServerModuleName,
                            remote + " 协议解析失败: " + line);
            continue;
        }
        handleMessage(clientSocket, message);
    }
}

void TcpServer::handleMessage(std::intptr_t clientSocketValue, const NetMessage &message)
{
    const SocketHandle clientSocket = toSocket(clientSocketValue);
    ShineLog::write(kTcpServerModuleName,
                    "收到消息: 指令=" + message.command +
                        " 数据=" + message.data);

    if (!_handler)
    {
        return;
    }

    const std::optional<NetMessage> response = _handler(message);
    if (!response || !_protocol)
    {
        return;
    }

    std::string reply = _protocol->encode(*response);
    reply.push_back('\n');
    if (!sendAll(clientSocket, reply))
    {
        ShineLog::error(kTcpServerModuleName, "发送响应失败: " + socketErrorText());
    }
}

std::optional<NetMessage> TcpServer::defaultHandler(const NetMessage &request) const
{
    // 默认回显，便于先用 netcat 之类的工具验证协议与粘包处理
    return NetMessage{request.command, request.data};
}
