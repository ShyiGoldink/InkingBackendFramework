#include "network/Listener.h"

#include "basic/ShineLog.h"
#include "network/NetworkStage.h"
#include "network/Shard.h"

#include <algorithm>
#include <system_error>

namespace
{
    /** 与分片一致的安全网超时：正常退出靠唤醒器，不靠这个数 */
    constexpr int kWakeTimeoutMs = 1000;
}

Listener::Listener(SocketType listenSocket, const std::vector<Shard *> &shards,
                   std::size_t maxConnectionsPerShard)
    : _listenSocket(listenSocket),
      _shards(shards),
      _maxConnectionsPerShard(maxConnectionsPerShard == 0 ? 1 : maxConnectionsPerShard)
{
}

Listener::~Listener()
{
    requestStop();
    join();
    _wakeup.close();
}

bool Listener::start(std::string &errorMessage)
{
    if (_shards.empty())
    {
        errorMessage = "没有可用的分片，接入线程无法启动";
        return false;
    }

    _poller = createPoller();
    if (!_poller->init(errorMessage))
    {
        return false;
    }

    if (!_wakeup.init(errorMessage))
    {
        return false;
    }

    // 两个 tag：this 表示监听 socket，&_wakeup 表示唤醒器。用对象地址而不是 nullptr，
    // 是为了让「还没设 tag」和「这是监听 socket」不混成同一个分支。
    if (!_poller->add(_listenSocket, this, PollerBits::Read))
    {
        errorMessage = "把监听 socket 注册进事件集合失败:" + SocketOps::lastErrorText();
        return false;
    }

    if (!_poller->add(_wakeup.socket(), &_wakeup, PollerBits::Read))
    {
        errorMessage = "把唤醒器注册进事件集合失败:" + SocketOps::lastErrorText();
        return false;
    }

    _ready.reserve(64);

    try
    {
        _thread = std::thread([this]()
                              { serve(); });
    }
    catch (const std::system_error &error)
    {
        errorMessage = "拉起接入线程失败:" + std::string(error.what());
        return false;
    }

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kAcceptor,
                  "接入线程已启动，分片数 " + std::to_string(_shards.size()));
    return true;
}

void Listener::requestStop()
{
    _stopRequested.store(true);
    _wakeup.notify();
}

void Listener::join()
{
    if (_thread.joinable())
    {
        _thread.join();
    }
}

void Listener::serve()
{
    while (!_stopRequested.load())
    {
        const int readyCount = _poller->wait(_ready, kWakeTimeoutMs);
        if (readyCount < 0)
        {
            ShineLog::log(LogLevel::Error, kNetModuleName, NetworkStage::kAcceptor,
                          "接入线程等待事件失败（" + SocketOps::lastErrorText() + "），退出");
            break;
        }

        for (const PollerEvent &event : _ready)
        {
            if (event.tag == &_wakeup)
            {
                _wakeup.drain();
                continue;
            }
            if (event.tag == this)
            {
                acceptConnections();
            }
        }
    }

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kAcceptor,
                  "接入线程已退出，累计接受连接 " + std::to_string(_acceptedCount.load()));
}

void Listener::acceptConnections()
{
    while (true)
    {
        sockaddr_in clientAddress = {};
#ifdef _WIN32
        int clientAddressLength = sizeof(clientAddress);
#else
        socklen_t clientAddressLength = sizeof(clientAddress);
#endif
        const SocketType clientSocket = ::accept(
            _listenSocket,
            reinterpret_cast<sockaddr *>(&clientAddress),
            &clientAddressLength);

        if (clientSocket == kInvalidSocket)
        {
            if (SocketOps::wouldBlock())
            {
                return; // 接到 EAGAIN 为止：一次风暴不能被摊成很多轮等待
            }

            // 真错误（Windows 上常见的是 EMFILE 前兆 / 连接被对端 RST）。
            // 不在这里循环重试：立刻重试大概率还是失败，会把 CPU 打满。
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kAcceptor,
                          "接受连接失败:" + SocketOps::lastErrorText());
            return;
        }

        // 请求-响应型协议必须关 Nagle，否则小包会被攒着等 ACK，延迟到几十毫秒
        SocketOps::setNoDelay(clientSocket);

        if (!SocketOps::setNonBlocking(clientSocket))
        {
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kAcceptor,
                          "新连接设为非阻塞失败，已关闭:" + SocketOps::lastErrorText());
            SocketOps::close(clientSocket);
            continue;
        }

        Shard *shard = pickShard();
        if (shard == nullptr)
        {
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kAcceptor,
                          "所有分片都已达连接上限，拒绝新连接");
            SocketOps::close(clientSocket);
            continue;
        }

        // 交出去之后这个 fd 就归分片了：本线程不得再 close 它
        shard->adopt(clientSocket);
        _acceptedCount.fetch_add(1, std::memory_order_relaxed);
    }
}

Shard *Listener::pickShard()
{
    Shard *chosen = nullptr;
    std::size_t chosenCount = 0;

    for (Shard *shard : _shards)
    {
        const std::size_t count = shard->connectionCount();
        if (count >= _maxConnectionsPerShard)
        {
            continue; // 满了就不往里塞
        }
        if (chosen == nullptr || count < chosenCount)
        {
            chosen = shard;
            chosenCount = count;
        }
    }

    return chosen;
}
