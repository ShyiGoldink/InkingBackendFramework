#include "network/Shard.h"

#include "basic/ShineLog.h"
#include "dataStruct/TaskStruct.h"
#include "network/CommandRegistry.h"
#include "network/NetworkStage.h"
#include "thread/TaskQueueLoop.h"

#include <algorithm>
#include <any>
#include <system_error>

namespace
{
    /**
     * 一轮最多等多久。
     *
     * 定时器（C5）上线后，这个数的角色变了：**它不再决定超时精度**，只是
     * 「没有任何定时器时的睡眠上限」。分片睡多久由 `TimerQueue::nearestTimeoutMs`
     * 算出来（有定时器就精确到毫秒），只有在一条连接都没挂定时器时才落到这个上限。
     *
     * 保留上限的理由是安全网：万一定时器或唤醒路径出问题，分片最多卡 1 秒
     * 还能自己醒过来检查一次退出标志。反过来说，如果哪天慢了，别第一时间来调
     * 这个数——先查唤醒路径。
     */
    constexpr int kWakeTimeoutMs = 1000;
    /** 单次 recv 的临时缓冲。它是栈上的，不随连接数增长 */
    constexpr std::size_t kReceiveChunk = 4096;

    /**
     * @brief 停机时「读掉再关」的字节上限。
     *
     * 64 KiB 足够覆盖绝大多数「客户端刚发出一个请求」的场景（请求通常几 KB），
     * 又不至于让一个正在灌大文件的连接把停机拖住。超出的部分留在内核里，
     * 那种情况下 close 仍然会发 RST——这是可接受的：对端本来就在做与服务无关的事。
     */
    constexpr std::size_t kShutdownDiscardLimit = 64 * 1024;

    /**
     * @brief 单条连接允许积压的待发送字节上限（慢消费者防护的兜底）。
     *
     * C2 之后它基本**不会被触发**，原因值得写清楚：一条在飞意味着上一条响应没发完
     * 就不会派发下一条，写缓冲永远只有一个响应的量，涨不起来。真正的背压发生在
     * 读侧——不读，就没有新请求进来，客户端自己会写不动。
     *
     * 那为什么还留着：它是「以后允许一条连接多条在飞」或「服务端主动推送」时的
     * 最后一道闸门；真到了那一天，写缓冲才可能真的堆起来。现在它存在的成本是
     * 一次比较，收益是那种场景下不会悄悄吃内存。
     *
     * 判定的口径是「原本就有积压，再加这一帧会超限」——单条大帧（最大 16 MiB）
     * 在缓冲为空时总能发出去，不会被这条规则误杀。
     */
    constexpr std::size_t kMaxPendingSendBytes = 4 * 1024 * 1024;

    const char *closeReasonText(CloseReason reason)
    {
        switch (reason)
        {
        case CloseReason::PeerClosed:
            return "对端关闭";
        case CloseReason::ReceiveError:
            return "接收出错";
        case CloseReason::SendError:
            return "发送出错";
        case CloseReason::InvalidFrame:
            return "帧头不合法";
        case CloseReason::UnknownCommand:
            return "指令未注册";
        case CloseReason::SlowConsumer:
            return "写缓冲积压超限（对端只发不读）";
        case CloseReason::HalfFrameTimeout:
            return "半包超时（收到一部分帧之后再没动静）";
        case CloseReason::IdleTimeout:
            return "空闲超时（长时间没有完整交互）";
        case CloseReason::WriteTimeout:
            return "写超时（写缓冲长时间没发完）";
        case CloseReason::ServerShutdown:
            return "服务停止";
        case CloseReason::None:
            break;
        }
        return "未知原因";
    }
}

Shard::Shard(std::size_t index, std::size_t maxConnections, const NetworkTimeouts &timeouts)
    : _index(index), _maxConnections(maxConnections == 0 ? 1 : maxConnections), _timeouts(timeouts)
{
}

Shard::~Shard()
{
    requestStop();
    join();

    // 线程已经退出、连接由 serve() 的收尾阶段关完。这里断开回投通道：
    // 之后还在跑的业务任务 post 进来会看到关闭标记并安静丢弃，
    // 不会碰到任何已经销毁的东西。
    if (_mailbox)
    {
        _mailbox->detach();
    }
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

    _wakeup = std::make_shared<Wakeup>();
    if (!_wakeup->init(errorMessage))
    {
        return false;
    }

    _mailbox = std::make_shared<ShardMailbox>();
    _mailbox->attach(_wakeup);

    // 监听 socket 走的是 acceptor 线程，分片自己只等两样东西：唤醒器 + 自己的连接。
    // 唤醒器用对象地址当 tag，与连接指针不冲突（连接指针在堆上、且不等于成员地址）。
    if (!_poller->add(_wakeup->socket(), _wakeup.get(), PollerBits::Read))
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
                  name() + " 已启动，连接上限 " + std::to_string(_maxConnections) +
                      "，超时（" + _timeouts.describe() + "）");
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
        _wakeup->notify();
    }
}

void Shard::requestStop()
{
    _stopRequested.store(true);
    // 必须唤醒：分片可能正睡在 wait() 里，只置标志没人查是查不到的
    if (_wakeup)
    {
        _wakeup->notify();
    }
}

void Shard::requestDrain(int graceMs)
{
    // 顺序：先写截止时刻，再置标志。反过来会让分片读到「已排空但截止时刻是 0」，
    // 一进排空就判定超时（见头文件里关于发布顺序的说明）。
    _drainStartedMs = TimerQueue::nowMs();
    _drainDeadlineMs = _drainStartedMs + std::max(graceMs, 0);
    _draining.store(true, std::memory_order_release);

    if (_wakeup)
    {
        _wakeup->notify();
    }
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

        // ② 再倒响应。同理：业务线程刚投回来、分片又睡进去，就白等一轮
        drainMailbox();

        if (_stopRequested.load())
        {
            break;
        }

        // ③ 排空检查。放在 wait() 之前：上一次醒来已经处理完回投与写事件，
        //    此刻如果连接表空了就应该立刻退出，不该再白睡一轮。
        if (_draining.load(std::memory_order_acquire) && drainStep())
        {
            break;
        }

        // ④ 等事件。醒来有四种可能：有连接可读写、唤醒器被写、有定时器到期、安全网上限。
        //    睡多久不是常量，而是「最近一个定时器到期时刻」——这是 C5 的核心：
        //    分片因此既不会永久阻塞，也不会为了查定时器而空转。
        int waitMs = _timers.nearestTimeoutMs(kWakeTimeoutMs);

        // 排空期间还要再压一层：不能睡过排空截止时刻。否则「上限 5 秒」实际会变成
        // 5 秒 + 最多 1 秒（正好睡在截止时刻之后才醒），超时报告的时间就不准了。
        if (_draining.load(std::memory_order_acquire))
        {
            const std::int64_t remaining = _drainDeadlineMs - TimerQueue::nowMs();
            if (remaining < waitMs)
            {
                waitMs = remaining > 0 ? static_cast<int>(remaining) : 0;
            }
        }

        const int readyCount = _poller->wait(_ready, waitMs);
        if (readyCount < 0)
        {
            ShineLog::log(LogLevel::Error, kNetModuleName, NetworkStage::kShard,
                          name() + " 等待事件失败（" + SocketOps::lastErrorText() + "），分片退出");
            break;
        }

        // ⑤ 逐个处理。回调里可能关闭连接，但只是「标记」，真正的销毁在 ⑦
        for (const PollerEvent &event : _ready)
        {
            handleEvent(event);
        }

        // ⑥ 到期定时器。必须紧挨着 ⑦，**不能挪到 wait() 之前**：
        //    wait 正是被「最近的到期时刻」叫醒的，醒来就地处理才是准点；
        //    放在 wait 之前的话，到期会被推迟一整个等待周期（实测晚了整整 1 秒，
        //    因为关闭标记留到下一轮清理才生效）。
        //    这里只标记关闭，真正的 remove + close 在 ⑦。
        handleTimeouts();

        // ⑦ 清理阶段。遍历中途删连接就是经典的 use-after-free
        removeClosedConnections();
    }

    // 退出路径：交接队列里还没来得及接进来的 fd 必须关掉，否则是实打实的 fd 泄漏
    closeIncoming();
    closeAllConnections();

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                  name() + " 已退出，峰值连接 " + std::to_string(_peakConnections) +
                      "，拆帧 " + std::to_string(_counters.frames) +
                      "，派发 " + std::to_string(_counters.dispatched) +
                      "，回投 " + std::to_string(_counters.replies) +
                      "，业务失败 " + std::to_string(_counters.handlerFailed) +
                      "，丢弃响应 " + std::to_string(_counters.droppedReplies) +
                      "；关闭原因：对端 " + std::to_string(_counters.peerClosed) +
                      " / 收错 " + std::to_string(_counters.receiveError) +
                      " / 发错 " + std::to_string(_counters.sendError) +
                      " / 非法帧 " + std::to_string(_counters.invalidFrame) +
                      " / 未知指令 " + std::to_string(_counters.unknownCommand) +
                      " / 慢消费者 " + std::to_string(_counters.slowConsumer) +
                      " / 半包超时 " + std::to_string(_counters.halfFrameTimeout) +
                      " / 空闲超时 " + std::to_string(_counters.idleTimeout) +
                      " / 写超时 " + std::to_string(_counters.writeTimeout) +
                      " / 服务停止 " + std::to_string(_counters.serverShutdown) +
                      "；定时器：活 " + std::to_string(_timers.liveCount()) +
                      "，堆 " + std::to_string(_timers.heapSize()) +
                      "；排空：" + (_drainTimedOut ? "超时（剩 " + std::to_string(_drainSurvivors) +
                                                     " 条 / " + std::to_string(_drainSurvivorBytes) + " 字节）"
                                                   : "完成") +
                      "，用时 " + std::to_string(_drainElapsedMs) + "ms");
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

        // 排空期间不再接新连接。接入线程此时已经停了，正常不会有新的进来；
        // 这一条挡的是「交接队列里还压着几条」的那个窗口。
        if (_draining.load(std::memory_order_acquire))
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
        connection->id = _nextConnectionId++;
        connection->socket = socket;
        Connection *raw = connection.get();

        // 先入表再注册：注册失败时能按标识直接找到并删掉。
        // unordered_map 的节点地址稳定，所以 raw 在整个连接生命周期内都有效。
        _connections.emplace(connection->id, std::move(connection));

        if (!_poller->add(socket, raw, PollerBits::Read))
        {
            const std::uint64_t id = raw->id;
            _connections.erase(id);
            SocketOps::close(socket);
            ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kShard,
                          name() + " 注册连接失败:" + SocketOps::lastErrorText());
            continue;
        }

        const std::size_t count = _connectionCount.fetch_add(1) + 1;
        _peakConnections = std::max(_peakConnections, count);

        // 新连接从挂上空闲超时开始。半包/写暂时没东西可挂：
        // 一个字节都还没收到、写缓冲也空着，这两种超时此刻没有判据。
        refreshTimers(raw);
    }
}

void Shard::drainMailbox()
{
    _mailbox->drain(_replies);
    if (_replies.empty())
    {
        return;
    }

    for (const ShardMailbox::Reply &reply : _replies)
    {
        Connection *connection = findConnection(reply.connectionId);

        // 响应回来时连接可能已经没了（对端断开、被断开、或停机）。
        // 这不是错误，是「请求还在飞、连接先死」的正常竞态，丢弃即可。
        if (connection == nullptr || !connection->isOpen() ||
            connection->state != ConnectionState::InFlight)
        {
            ++_counters.droppedReplies;
            continue;
        }

        if (!reply.handlerOk)
        {
            ++_counters.handlerFailed;
        }

        const std::size_t pending = connection->pendingSendBytes();
        if (pending > 0 && pending + reply.bytes.size() > kMaxPendingSendBytes)
        {
            closeConnection(connection, CloseReason::SlowConsumer);
            continue;
        }

        connection->appendSend(reply.bytes.data(), reply.bytes.size());
        connection->state = ConnectionState::Writing;
        ++_counters.replies;

        flush(connection); // 立刻尝试发出去，别等可写事件
    }

    _replies.clear();
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
    if (event.tag == _wakeup.get())
    {
        // 规则 2：必须一次排空，留字节就会变成忙等
        _wakeup->drain();
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
        // 在飞期间不读：宁可晚一轮，也不能让同一条连接出现两条在飞。
        // 摘掉读关注之后仍可能因为 EPOLLRDHUP 被报告可读，所以这个判断必须有。
        //
        // 排空期间同样不读：这时候读进来的请求没人保证能处理完（分片马上就要停了），
        // 受理了却回不了包，比直接不收更糟。
        if (connection->state == ConnectionState::Idle &&
            !_draining.load(std::memory_order_acquire))
        {
            readFrom(connection);
        }
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
            closeConnection(connection, CloseReason::PeerClosed);
            return;
        }

        if (receivedBytes < 0)
        {
            if (SocketOps::wouldBlock())
            {
                break; // 这一轮的数据读完了
            }
            // 连接被重置（客户端强杀）也会走到这里，不是「服务坏了」
            closeConnection(connection, CloseReason::ReceiveError);
            return;
        }

        // 关键一步：字节交给这条连接自己的解析器，由它判断「到哪儿是一条消息」。
        // 每读一块就立刻拆一遍，而不是把整轮数据攒起来再拆——一个猛发 100 MiB
        // 的客户端因此只能让缓冲长到一个 recv 块的大小，而不是 100 MiB。
        connection->codec.push(buffer, static_cast<std::size_t>(receivedBytes));

        if (!consumeFrames(connection))
        {
            return; // 出帧阶段已经把这条连接关掉了
        }

        if (connection->state != ConnectionState::Idle)
        {
            // 拆出一帧就已经在飞了：读关注被摘掉，别再读，剩下的字节留在 codec 里
            break;
        }
    }

    refreshTimers(connection);
    flush(connection);
}

bool Shard::consumeFrames(Connection *connection)
{
    while (connection->state == ConnectionState::Idle)
    {
        CommandFrame frame;
        const FrameCodec::Status status = connection->codec.next(frame);

        if (status == FrameCodec::Status::NeedMoreData)
        {
            return true; // 半帧：留着，等下一次 recv 补齐
        }

        if (status == FrameCodec::Status::InvalidFrame)
        {
            // 帧层已经丢了缓冲。这里必须断连接：错位一旦发生，后面每一帧都会跟着错，
            // 在同一个字节流上继续解析没有意义（理由见 ProtocolFrame.h）。
            closeConnection(connection, CloseReason::InvalidFrame);
            return false;
        }

        // 到这里才第一次拥有「一条完整消息」
        ++_counters.frames;
        if (!dispatchFrame(connection, frame))
        {
            return false;
        }
    }
    return true;
}

bool Shard::dispatchFrame(Connection *connection, const CommandFrame &frame)
{
    const CommandRegistry::Handler *handler = CommandRegistry::instance().find(frame.command);
    if (handler == nullptr)
    {
        // 协议里没有错误码字段，所以「指令不支持」只能体现为断开 + 日志。
        // 让连接继续活着会诱使对端反复重试一条永远不可能成功的指令。
        closeConnection(connection, CloseReason::UnknownCommand);
        return false;
    }

    // 一条在飞：先把这条连接的读关注摘掉，再去投递。
    // 顺序不能反——先摘再投，业务线程回投时状态已经是 InFlight，
    // 不会出现「响应先到、状态还没置」的窗口。
    connection->state = ConnectionState::InFlight;
    updateInterest(connection);

    // 在飞期间三类超时全部摘掉：半包与空闲的判据是「连接在等人用」，
    // 此刻是业务在算；写缓冲必然为空，写超时本就挂不上。
    // 不摘的话，一个耗时 30 秒的业务请求会被 10 秒的半包超时掐死。
    refreshTimers(connection);

    auto mailbox = _mailbox;
    const std::uint64_t connectionId = connection->id;
    const CommandRegistry::Handler *entry = handler;
    CommandFrame request = frame; // 拷一份：业务线程拥有它，分片不再碰

    ++_counters.dispatched;

    Task<std::any> task;
    task.action = [mailbox, connectionId, entry, request = std::move(request)](
                      const std::vector<std::any> &) -> std::any
    {
        std::string payload;
        const bool handled = (*entry)(request, payload);
        if (!handled)
        {
            payload.clear();
        }

        // 响应帧沿用请求的指令名：协议里没有「响应类型」字段，一一对应最简单。
        // 以后要支持「一个请求触发多种响应」时，这里才需要引入消息类型。
        CommandFrame response;
        response.command = request.command;
        response.payload = std::move(payload);

        std::string encoded;
        if (FrameCodec::encode(response, encoded) == FrameCodec::Status::Ok)
        {
            mailbox->post(connectionId, std::move(encoded), handled);
        }
        return {};
    };

    TaskQueueLoop::instance().addTask(std::move(task));
    return true;
}

void Shard::flush(Connection *connection)
{
    while (connection->hasPendingSend())
    {
        const std::size_t pending = connection->pendingSendBytes();
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
            closeConnection(connection, CloseReason::SendError);
            return;
        }
        break;
    }

    if (connection->hasPendingSend())
    {
        connection->state = ConnectionState::Writing;
        updateInterest(connection);
        // 写超时从「确认发不出去」这一刻开始计时。挂在 append 那一刻也可以，
        // 但那样一次成功的发送也会走一遍挂表/摘表，纯属白做。
        refreshTimers(connection);
        return;
    }

    connection->clearSentData();
    refreshTimers(connection); // 写缓冲已空：摘掉写超时

    // 写完了：Writing → Idle 并恢复读，同时消化摘读关注期间积压的帧。
    // InFlight（响应还没回来）时什么也不做，摘着的读关注继续摘着。
    if (connection->state == ConnectionState::Writing ||
        connection->state == ConnectionState::Idle)
    {
        resumeReading(connection);
    }
}

void Shard::updateInterest(Connection *connection)
{
    if (!connection->isOpen())
    {
        return;
    }

    // 关注位由状态唯一决定，不另记一套「有没有积压」的布尔值：
    // 两处各写一半就是 M7 说的「状态机被拆散」，早晚出现「忘了恢复读」。
    unsigned wanted = 0;
    switch (connection->state)
    {
    case ConnectionState::Idle:
        wanted = PollerBits::Read;
        break;
    case ConnectionState::InFlight:
        wanted = 0; // 一条在飞：连读都不读，背压在这里产生
        break;
    case ConnectionState::Writing:
        wanted = PollerBits::Write;
        break;
    case ConnectionState::Closing:
        return;
    }

    if (wanted == connection->interest)
    {
        return; // 关注位没变就不调 epoll_ctl，这是热路径上最省的一次系统调用
    }

    if (_poller->modify(connection->socket, connection, wanted))
    {
        connection->interest = wanted;
    }
}

void Shard::resumeReading(Connection *connection)
{
    if (!connection->isOpen())
    {
        return;
    }

    // 排空期间不再恢复读：这条连接已经「处理完手上的事」了，正好在帧边界处断开。
    // 在这里断是干净的——响应已经发完，对端收到的是完整帧 + FIN，不会读到半个包。
    if (_draining.load(std::memory_order_acquire))
    {
        // 响应已经发完（此刻写缓冲为空），先把对端可能已经发来的下一条请求读掉，
        // 再干净关断。不读的话 close 会发 RST，客户端会丢掉刚收到的那条完整响应。
        discardIncoming(connection, kShutdownDiscardLimit);
        closeConnection(connection, CloseReason::ServerShutdown);
        return;
    }

    connection->state = ConnectionState::Idle;
    updateInterest(connection);

    // 摘读关注期间对端可能又发了几帧进来（甚至就在同一个 recv 块里），
    // 它们正压在 codec 的缓冲里。不主动消化的话，要等到下一次网络事件
    // 才会被处理——「同一连接连发 1000 条、最后几条永远不回」就是这么来的。
    if (connection->codec.pendingBytes() > 0)
    {
        consumeFrames(connection);
    }

    // 回到 Idle 是一次「完整交互走完」，空闲超时要重新计时；
    // codec 里若还压着残帧，半包超时也从此刻重新起算（上一段已经结束）。
    refreshTimers(connection);
}

void Shard::refreshTimers(Connection *connection)
{
    if (!connection->isOpen())
    {
        _timers.disarmAll(connection->id);
        return;
    }

    // 空闲：只有 Idle 才算「连接在等人用」。InFlight 是业务在算、Writing 是我方在发，
    // 两种都不是连接的错——挂上空闲超时就会把一条正忙的连接掐掉。
    if (connection->state == ConnectionState::Idle)
    {
        armRefreshingTimer(connection, TimeoutKind::Idle, _timeouts.idleMs);
    }
    else
    {
        _timers.disarm(connection->id, TimeoutKind::Idle);
    }

    // 半包：Idle 且 codec 里压着不完整的字节。用 armIfAbsent 而不是刷新，
    // 原因见头文件：顺延的话，每秒发一个字节就能让连接永久占着一份缓冲。
    if (connection->state == ConnectionState::Idle && connection->codec.pendingBytes() > 0)
    {
        _timers.armIfAbsent(connection->id, TimeoutKind::HalfFrame, _timeouts.halfFrameMs);
    }
    else
    {
        _timers.disarm(connection->id, TimeoutKind::HalfFrame);
    }

    // 写：有积压才算。InFlight 时写缓冲必然为空，所以不用单独判状态。
    if (connection->hasPendingSend())
    {
        armRefreshingTimer(connection, TimeoutKind::Write, _timeouts.writeMs);
    }
    else
    {
        _timers.disarm(connection->id, TimeoutKind::Write);
    }
}

void Shard::armRefreshingTimer(Connection *connection, TimeoutKind kind, int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        _timers.disarm(connection->id, kind);
        return;
    }

    // 剩余时间还过半就保持原样。每次事件都重挂会让堆条目疯长（每次 arm 一个新条目），
    // 1000 条连接满负荷时能造到每秒百万级，压紧的开销反而超过定时器本身的价值。
    const int remaining = _timers.remainingMs(connection->id, kind);
    if (remaining >= 0 && remaining > timeoutMs / 2)
    {
        return;
    }

    _timers.arm(connection->id, kind, timeoutMs);
}

void Shard::handleTimeouts()
{
    if (_timers.collectExpired(_expired) == 0)
    {
        return;
    }

    for (const ExpiredTimer &timer : _expired)
    {
        Connection *connection = findConnection(timer.connectionId);

        // 连接可能已经没了（上一轮关掉的、或者对端在这期间断开）。
        // 定时器条目是「按标识」记录的，找不到就是找不到，不是错误。
        if (connection == nullptr || !connection->isOpen())
        {
            continue;
        }

        switch (timer.kind)
        {
        case TimeoutKind::HalfFrame:
            closeConnection(connection, CloseReason::HalfFrameTimeout);
            break;
        case TimeoutKind::Idle:
            closeConnection(connection, CloseReason::IdleTimeout);
            break;
        case TimeoutKind::Write:
            closeConnection(connection, CloseReason::WriteTimeout);
            break;
        }
        // 同一条连接可能同时挂着两三个定时器（比如又半包又写不动），
        // closeConnection 之后 isOpen() 为假，后面的条目会在下一次循环里被跳过。
    }

    _expired.clear();
}

void Shard::discardIncoming(Connection *connection, std::size_t limitBytes)
{
    if (!connection->isOpen())
    {
        return;
    }

    char buffer[kReceiveChunk];
    std::size_t discarded = 0;
    while (discarded < limitBytes)
    {
        const int received = ::recv(connection->socket, buffer,
                                    static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0)
        {
            // 0 = 对端已经关了；<0 = EAGAIN（没更多了）或真错误。
            // 三种情况都该停止读，区别只在要不要记日志——停机路径上不值得区分。
            break;
        }
        discarded += static_cast<std::size_t>(received);
    }
}

bool Shard::drainStep()
{
    const std::int64_t now = TimerQueue::nowMs();

    if (!_drainPrepared)
    {
        _drainPrepared = true;

        std::size_t keep = 0;
        std::size_t keepBytes = 0;

        // 先关「没事可做」的：Idle 且没有待发字节。它们此刻没有任何未完成的交互，
        // 留着只会让调用方白等一个排空周期。
        for (const auto &entry : _connections)
        {
            Connection *connection = entry.second.get();
            if (!connection->isOpen())
            {
                continue;
            }
            if (connection->state == ConnectionState::Idle && !connection->hasPendingSend())
            {
                // 先读掉接收缓冲再关，否则内核会发 RST（见 discardIncoming 的说明）
                discardIncoming(connection, kShutdownDiscardLimit);
                closeConnection(connection, CloseReason::ServerShutdown);
                continue;
            }
            ++keep;
            keepBytes += connection->pendingSendBytes();
        }
        removeClosedConnections();

        ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                      name() + " 开始排空：已断开空闲连接，保留 " + std::to_string(keep) +
                          " 条（" + std::to_string(keepBytes) + " 字节待发），上限 " +
                          std::to_string(_drainDeadlineMs - _drainStartedMs) + "ms");
    }

    if (_connections.empty())
    {
        _drainElapsedMs = now - _drainStartedMs;
        ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                      name() + " 排空完成，用时 " + std::to_string(_drainElapsedMs) + "ms");
        return true;
    }

    if (now >= _drainDeadlineMs)
    {
        _drainTimedOut = true;
        _drainElapsedMs = now - _drainStartedMs;
        _drainSurvivors = _connections.size();
        _drainSurvivorBytes = 0;
        for (const auto &entry : _connections)
        {
            _drainSurvivorBytes += entry.second->pendingSendBytes();
        }

        // Warn 而不是 Info：这说明「有人在停机时还占着连接不放」，
        // 要么是慢消费者，要么是排空上限配小了——两种情况都值得事后查。
        ShineLog::log(LogLevel::Warn, kNetModuleName, NetworkStage::kShard,
                      name() + " 排空超时（" + std::to_string(_drainElapsedMs) + "ms）：仍有 " +
                          std::to_string(_drainSurvivors) + " 条连接、" +
                          std::to_string(_drainSurvivorBytes) + " 字节待发，强制关闭");
        return true;
    }

    return false;
}

Shard::Statistics Shard::statistics() const
{
    Statistics stats;
    stats.peakConnections = _peakConnections;
    stats.frames = _counters.frames;
    stats.dispatched = _counters.dispatched;
    stats.replies = _counters.replies;
    stats.handlerFailed = _counters.handlerFailed;
    stats.droppedReplies = _counters.droppedReplies;
    stats.peerClosed = _counters.peerClosed;
    stats.shutdownCloses = _counters.serverShutdown;
    stats.timeouts = _counters.halfFrameTimeout + _counters.idleTimeout + _counters.writeTimeout;
    stats.errors = _counters.receiveError + _counters.sendError +
                   _counters.invalidFrame + _counters.unknownCommand;
    return stats;
}

void Shard::closeConnection(Connection *connection, CloseReason reason)
{
    if (!connection->isOpen())
    {
        return;
    }

    connection->closeReason = reason;

    switch (reason)
    {
    case CloseReason::PeerClosed:
        ++_counters.peerClosed;
        break;
    case CloseReason::ReceiveError:
        ++_counters.receiveError;
        break;
    case CloseReason::SendError:
        ++_counters.sendError;
        break;
    case CloseReason::InvalidFrame:
        ++_counters.invalidFrame;
        break;
    case CloseReason::UnknownCommand:
        ++_counters.unknownCommand;
        break;
    case CloseReason::SlowConsumer:
        ++_counters.slowConsumer;
        break;
    case CloseReason::HalfFrameTimeout:
        ++_counters.halfFrameTimeout;
        break;
    case CloseReason::IdleTimeout:
        ++_counters.idleTimeout;
        break;
    case CloseReason::WriteTimeout:
        ++_counters.writeTimeout;
        break;
    case CloseReason::ServerShutdown:
        ++_counters.serverShutdown;
        break;
    case CloseReason::None:
        break;
    }

    // 对端正常关闭是预期事件，写 Trace（默认阈值下不落盘，需要时调 `log level trace` 就能看到）；
    // 其余几类都是「值得留证据」的，写 Warn。同一条消息会被日志系统折叠成一行 + ×N。
    // 「服务停止」与「对端关闭」同理：它是我们自己做的决定，不该在停机时刷出一屏告警。
    const LogLevel level = (reason == CloseReason::PeerClosed ||
                            reason == CloseReason::ServerShutdown)
                               ? LogLevel::Trace
                               : LogLevel::Warn;
    ShineLog::log(level, kNetModuleName, NetworkStage::kShard,
                  name() + " 断开连接：" + closeReasonText(reason));

    _closing.push_back(connection->id);

    // 先把这条连接的定时器全摘掉，再标记关闭。不摘的话堆里会留下一堆
    // 永远不会被处理的陈旧条目，而且它们到期时还会白唤醒分片一次。
    _timers.disarmAll(connection->id);
    connection->markClosing();
}

void Shard::removeClosedConnections()
{
    if (_closing.empty())
    {
        return;
    }

    for (const std::uint64_t id : _closing)
    {
        const auto found = _connections.find(id);
        if (found == _connections.end())
        {
            continue;
        }
        const SocketType socket = found->second->socket;

        // 顺序很重要：先从事件集合里摘掉，再 close。反过来的话，close 之后
        // 这个 fd 号可能立刻被新连接复用，摘掉时就摘错人了。
        _poller->remove(socket);
        _connections.erase(found);
        SocketOps::close(socket);
        _connectionCount.fetch_sub(1);
    }
    _closing.clear();
}

void Shard::closeAllConnections()
{
    for (const auto &entry : _connections)
    {
        // 走到这里的连接是「停机时还没收干净」的那批（排空超时，或者硬停）。
        // 它们同样是「因为服务停止而被断开」，不计数的话最终统计里
        // 「停机断开」会少掉这一批，看起来像凭空消失。
        ++_counters.serverShutdown;
        _poller->remove(entry.second->socket);
        SocketOps::close(entry.second->socket);
    }
    _connections.clear();
    _closing.clear();
    _connectionCount.store(0);
}

Connection *Shard::findConnection(std::uint64_t id)
{
    const auto found = _connections.find(id);
    if (found == _connections.end())
    {
        return nullptr;
    }
    return found->second.get();
}
