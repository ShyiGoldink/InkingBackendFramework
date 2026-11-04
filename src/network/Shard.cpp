#include "network/Shard.h"

#include "basic/ShineLog.h"
#include "network/NetworkStage.h"

#include <algorithm>
#include <system_error>

namespace
{
    /**
     * 一轮最多等多久。
     *
     * 有唤醒器兜底，这个超时已经不是「响应速度」了，只是安全网：
     * 万一条唤醒路径出问题，分片最多卡 1 秒还能自己醒过来检查一次退出标志。
     * 反过来说，如果哪天慢了，别第一时间来调这个数——先查唤醒路径。
     */
    constexpr int kWakeTimeoutMs = 1000;
    /** 单次 recv 的临时缓冲。它是栈上的，不随连接数增长 */
    constexpr std::size_t kReceiveChunk = 4096;
}

Shard::Shard(std::size_t index, std::size_t maxConnections)
    : _index(index), _maxConnections(maxConnections == 0 ? 1 : maxConnections)
{
}

Shard::~Shard()
{
    requestStop();
    join();
    // 线程已经退出，连接由 serve() 的收尾阶段关完了；这里只兜底关掉唤醒器
    _wakeup.close();
}

std::string Shard::name() const
{
    return "分片 " + std::to_string(_index);
}

bool Shard::start(std::string &errorMessage)
{
    _poller = createPoller();
    if (!_poller->init(errorMessage))
    {
        return false;
    }

    if (!_wakeup.init(errorMessage))
    {
        return false;
    }

    // 监听 socket 走的是 acceptor 线程，分片自己只等两样东西：唤醒器 + 自己的连接。
    // 唤醒器用对象地址当 tag，与连接指针不冲突（连接指针一定在堆上、且不等于成员地址）。
    if (!_poller->add(_wakeup.socket(), &_wakeup, PollerBits::Read))
    {
        errorMessage = "把唤醒器注册进事件集合失败:" + SocketOps::lastErrorText();
        return false;
    }

    _ready.reserve(256);

    try
    {
        _thread = std::thread([this]()
                              { serve(); });
    }
    catch (const std::system_error &error)
    {
        errorMessage = "拉起分片线程失败:" + std::string(error.what());
        return false;
    }

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                  name() + " 已启动，连接上限 " + std::to_string(_maxConnections));
    return true;
}

void Shard::adopt(SocketType socket)
{
    bool needWakeup = false;
    {
        std::lock_guard<std::mutex> guard(_incomingMutex);
        _incoming.push_back(socket);
        // 「由空变非空」才通知：分片正忙时投进来的 N 条新连接只产生 1 次唤醒
        needWakeup = _incoming.size() == 1;
    }

    if (needWakeup)
    {
        _wakeup.notify();
    }
}

void Shard::requestStop()
{
    _stopRequested.store(true);
    // 必须唤醒：分片可能正睡在 wait() 里，只置标志没人查是查不到的
    _wakeup.notify();
}

void Shard::join()
{
    if (_thread.joinable())
    {
        _thread.join();
    }
}

void Shard::serve()
{
    while (!_stopRequested.load())
    {
        // ① 先收新连接。放在 wait() 之前，避免「连接已经交过来、分片又睡进去」的额外一轮延迟
        drainIncoming();
        if (_stopRequested.load())
        {
            break;
        }

        // ② 等事件。醒来有三种可能：有连接可读写、唤醒器被写、超时（安全网）
        const int readyCount = _poller->wait(_ready, kWakeTimeoutMs);
        if (readyCount < 0)
        {
            ShineLog::log(LogLevel::Error, kNetModuleName, NetworkStage::kShard,
                          name() + " 等待事件失败（" + SocketOps::lastErrorText() + "），分片退出");
            break;
        }

        // ③ 逐个处理。回调里可能关闭连接，但只是「标记」，真正的销毁在 ④
        for (const PollerEvent &event : _ready)
        {
            handleEvent(event);
        }

        // ④ 清理阶段。遍历中途删连接就是经典的 use-after-free
        removeClosedConnections();
    }

    // 退出路径：交接队列里还没来得及接进来的 fd 必须关掉，否则是实打实的 fd 泄漏
    closeIncoming();
    closeAllConnections();

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                  name() + " 已退出，峰值连接 " + std::to_string(_peakConnections));
}

void Shard::drainIncoming()
{
    std::vector<SocketType> incoming;
    {
        std::lock_guard<std::mutex> guard(_incomingMutex);
        incoming.swap(_incoming);
    }

    if (incoming.empty())
    {
        return;
    }

    for (const SocketType socket : incoming)
    {
        if (_stopRequested.load())
        {
            SocketOps::close(socket);
            continue;
        }

        if (_connections.size() >= _maxConnections)
        {
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kShard,
                          name() + " 已达连接上限 " + std::to_string(_maxConnections) +
                              "，拒绝新连接");
            SocketOps::close(socket);
            continue;
        }

        auto connection = std::make_unique<Connection>();
        connection->socket = socket;
        Connection *raw = connection.get();

        // 先入表再注册：注册失败时能按 socket 直接找到并删掉。
        // unordered_map 的节点地址稳定，所以 raw 在整个连接生命周期内都有效。
        _connections.emplace(socket, std::move(connection));

        if (!_poller->add(socket, raw, PollerBits::Read))
        {
            _connections.erase(socket);
            SocketOps::close(socket);
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kShard,
                          name() + " 注册连接失败:" + SocketOps::lastErrorText());
            continue;
        }

        const std::size_t count = _connectionCount.fetch_add(1) + 1;
        _peakConnections = std::max(_peakConnections, count);
    }
}

void Shard::closeIncoming()
{
    std::vector<SocketType> incoming;
    {
        std::lock_guard<std::mutex> guard(_incomingMutex);
        incoming.swap(_incoming);
    }
    for (const SocketType socket : incoming)
    {
        SocketOps::close(socket);
    }
}

void Shard::handleEvent(const PollerEvent &event)
{
    if (event.tag == &_wakeup)
    {
        // 规则 2：必须一次排空，留字节就会变成忙等
        _wakeup.drain();
        return;
    }

    if (event.tag == nullptr)
    {
        return;
    }

    auto *connection = static_cast<Connection *>(event.tag);
    if (!connection->isOpen())
    {
        return; // 本轮里已经关掉了，不要接着用
    }

    if (event.events & PollerBits::Read)
    {
        readFrom(connection);
    }

    // readFrom 里可能已经关掉了这条连接（对端关闭 / 真错误），别接着用
    if (connection->isOpen() && (event.events & PollerBits::Write))
    {
        flush(connection);
    }
}

void Shard::readFrom(Connection *connection)
{
    char buffer[kReceiveChunk];

    while (true)
    {
        const int receivedBytes = ::recv(
            connection->socket, buffer, static_cast<int>(sizeof(buffer)), 0);

        if (receivedBytes == 0)
        {
            // 可读不等于有数据：recv 返回 0 就是对端关闭。
            // 这也是「对端关闭」唯一可靠的判定方式（WSAPoll 的 POLLHUP 不能作数）。
            closeConnection(connection);
            return;
        }

        if (receivedBytes < 0)
        {
            if (SocketOps::wouldBlock())
            {
                break; // 这一轮的数据读完了
            }
            // 连接被重置（客户端强杀）也会走到这里，不是「服务坏了」
            closeConnection(connection);
            return;
        }

        // 当前阶段（B3）仍是 echo：读到的字节直接进写缓冲。
        // C1 接上 FrameCodec 之后，这里改成「追加到 receiveBuffer → 循环拆帧」，
        // 拆出一整帧才进写缓冲／派发。读写缓冲各一份的理由在那时才完全体现出来。
        connection->appendSend(buffer, static_cast<std::size_t>(receivedBytes));
    }

    flush(connection);
}

void Shard::flush(Connection *connection)
{
    while (connection->hasPendingSend())
    {
        const std::size_t pending = connection->sendBuffer.size() - connection->sendOffset;
        const int sentBytes = ::send(
            connection->socket,
            connection->sendBuffer.data() + connection->sendOffset,
            static_cast<int>(pending),
            SocketOps::sendFlags());

        if (sentBytes > 0)
        {
            connection->sendOffset += static_cast<std::size_t>(sentBytes);
            continue;
        }

        // 0 和 wouldBlock 都是「发送缓冲满了」：剩下的字节留着等可写事件
        if (sentBytes < 0 && !SocketOps::wouldBlock())
        {
            closeConnection(connection);
            return;
        }
        break;
    }

    if (!connection->hasPendingSend())
    {
        connection->clearSentData();
    }

    updateInterest(connection);
}

void Shard::updateInterest(Connection *connection)
{
    if (!connection->isOpen())
    {
        return;
    }

    const bool pending = connection->hasPendingSend();
    const unsigned wanted = static_cast<unsigned>(
        PollerBits::Read | (pending ? PollerBits::Write : 0));

    connection->state = pending ? ConnectionState::Writing : ConnectionState::Idle;

    if (wanted == connection->interest)
    {
        return; // 关注位没变就不调 epoll_ctl，这是热路径上最省的一次系统调用
    }

    if (_poller->modify(connection->socket, connection, wanted))
    {
        connection->interest = wanted;
    }
}

void Shard::closeConnection(Connection *connection)
{
    if (!connection->isOpen())
    {
        return;
    }
    _closing.push_back(connection->socket);
    connection->markClosing(); // 标记成「已关」，本轮内不会再用它收发
}

void Shard::removeClosedConnections()
{
    if (_closing.empty())
    {
        return;
    }

    for (const SocketType socket : _closing)
    {
        // 顺序很重要：先从事件集合里摘掉，再 close。反过来的话，close 之后
        // 这个 fd 号可能立刻被新连接复用，摘掉时就摘错人了。
        _poller->remove(socket);
        _connections.erase(socket);
        SocketOps::close(socket);
        _connectionCount.fetch_sub(1);
    }
    _closing.clear();
}

void Shard::closeAllConnections()
{
    for (const auto &entry : _connections)
    {
        _poller->remove(entry.first);
        SocketOps::close(entry.first);
    }
    _connections.clear();
    _closing.clear();
    _connectionCount.store(0);
}
