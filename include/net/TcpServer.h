#ifndef INKING_BACKEND_FRAMEWORK_NET_TCP_SERVER_H
#define INKING_BACKEND_FRAMEWORK_NET_TCP_SERVER_H

#include "basic/ShineBasicModule.h"
#include "net/IProtocol.h"
#include "net/NetMessage.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

inline constexpr const char *kTcpServerModuleName = "TcpServer";
inline constexpr uint16_t kDefaultTcpPort = 8888;

/**
 * @brief 简单的 TCP 文本服务器（阻塞式、一个连接一个线程）。
 *
 * 定位：先把网络通路跑通，方便与鸿蒙硬件联调。
 * - 粘包处理：接收缓冲区按 '\n' 拆分成一条条消息（帧）；
 * - 协议解析：通过 IProtocol 注入，默认使用文本协议“指令/数据”；
 * - 业务分发：通过 setMessageHandler 注入，未设置时默认回显；
 * - 数据库暂不接入，网络层与业务层通过 NetMessage 解耦。
 */
class TcpServer : public ShineBasicModule
{
public:
    /** 业务消息处理器：返回空表示不回包 */
    using MessageHandler = std::function<std::optional<NetMessage>(const NetMessage &)>;

    TcpServer();
    ~TcpServer() override;

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    std::string moduleName() const override
    {
        return kTcpServerModuleName;
    }

    /** 设置监听端口（必须在 start 前调用） */
    void setPort(uint16_t port);
    /** 获取监听端口 */
    uint16_t port() const;
    /** 替换协议实现（默认 SimpleTextProtocol） */
    void setProtocol(std::shared_ptr<IProtocol> protocol);
    /** 设置业务消息处理器（默认回显） */
    void setMessageHandler(MessageHandler handler);

    /** 启动 TCP 服务器；返回是否启动成功 */
    bool start();
    /** 停止服务器并关闭全部连接（可重复调用） */
    void stop();
    /** 服务器是否正在运行 */
    bool isRunning() const;
    /** 当前客户端数量 */
    std::size_t clientCount() const;
    /** 当前客户端列表（ip:port），供控制台查看 */
    std::vector<std::string> clientDescriptions() const;

private:
    /** 单个客户端连接（具体定义在 TcpServer.cpp 中） */
    struct Client;

    void registerStages();
    void acceptLoop();
    void clientLoop(uint64_t clientId, std::intptr_t clientSocket, std::string remote);
    bool processReceivedBytes(std::intptr_t clientSocket, const std::string &remote,
                              std::string &buffer);
    void handleMessage(std::intptr_t clientSocket, const NetMessage &message);
    void markClientFinished(uint64_t clientId);
    void cleanupFinishedClients();
    void cleanupAllClients();
    std::optional<NetMessage> defaultHandler(const NetMessage &request) const;

    uint16_t _port = kDefaultTcpPort;
    std::atomic<bool> _running{false};
    bool _socketReady = false;
    std::uint64_t _nextClientId = 1;
    std::intptr_t _listener = -1;
    std::thread _acceptWorker;
    std::shared_ptr<IProtocol> _protocol;
    MessageHandler _handler;
    mutable std::mutex _clientMutex;
    std::vector<std::unique_ptr<Client>> _clients;
    std::unordered_map<uint64_t, std::thread> _clientWorkers;
};

#endif // INKING_BACKEND_FRAMEWORK_NET_TCP_SERVER_H
