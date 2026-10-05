#include "network/Net.h"

#include "thread/TaskQueueLoop.h"
#include "ui/UIMessageLibrary.h"

#include <system_error>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#include <winsock2.h> // WSAPoll / pollfd 在这里
#else
#include <poll.h>
#include <sys/epoll.h>
#endif

namespace
{
    constexpr int STAGE_APPLY_NETWORK_SERVER = 1;
    constexpr int STAGE_CREATE_SOCKET = 2;
    constexpr int STAGE_BIND = 3;
    constexpr int STAGE_LISTEN = 4;
    constexpr int STAGE_START_UP = 5;
    // 这里后面应该要架着扔到分片去，不过这里我就暂时先不做了，等分片做好再说

    /** 一轮 epoll_wait 最多取回多少个事件 */
    constexpr int kMaxEventsPerWait = 256;
    /** 事件循环里 epoll_wait 的超时。A6 不做定时器，给一个大值即可（0 会把 CPU 打满） */
    constexpr int kWaitTimeoutMs = 1000;

    /**
     * 平台差异：Windows 上没有 poll(2)，只有 WSAPoll（返回值和 pollfd 语义一致）。
     * UCRT64 里连 <poll.h> 都没有，所以这个包装是必须的，不是可选的美化。
     */
    int pollNative(pollfd *descriptors, unsigned long count, int timeoutMs)
    {
#ifdef _WIN32
        return ::WSAPoll(descriptors, count, timeoutMs);
#else
        return ::poll(descriptors, static_cast<nfds_t>(count), timeoutMs);
#endif
    }

    /** send/recv 返回「现在不能收发，但这不是错误」——Linux 是 EAGAIN/EWOULDBLOCK，Windows 是 WSAEWOULDBLOCK */
    bool isWouldBlock()
    {
#ifdef _WIN32
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    /** 把 socket 设为非阻塞。非阻塞是后面所有事情的前提：不然 recv/send 会把整条线程按在那里 */
    bool setNonBlocking(SocketType socket)
    {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags == -1)
        {
            return false;
        }
        return fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
    }

    /**
     * 一条连接的全部状态（A6 第 3 步）。
     *
     * 两件事各占一块，互不干扰：
     *   - receiveBuffer：还没被处理掉的字节（A6 里就是 echo 的输入，C1 之后是半包/粘包的落点）
     *   - sendBuffer + sendOffset：还没发完的字节和「发到哪儿了」的游标。
     *     非阻塞 send 的返回值是「这次真正发出去的字节数」，可能是 0（发送缓冲满），
     *     所以游标必须留着，等下一次可写事件接着发——这就是写缓冲。
     */
    struct Connection
    {
        SocketType socket = INVALID_SOCKET; /**这条连接是谁 */
        std::vector<char> receiveBuffer;    /**已收到、还没处理的数据 */
        std::vector<char> sendBuffer;       /**还没发完的字节 */
        std::size_t sendOffset = 0;         /**已经发到 sendBuffer 的哪个位置 */
    };

    /**
     * 事件集合的封装层（Linux 用 epoll，Windows 用 WSAPoll）。
     *
     * Linux 上是货真价实的 epoll（O(1) 注册、返回的就绪表只含活跃连接）；
     * Windows 上只是把 poll 包成同样的几个动作，够功能验证，但 1000 条时是 O(N) 的。
     * B1 会把这个类抽成正式的 Poller 接口（Linux epoll / macOS poll 或 kqueue / Windows WSAPoll）。
     */
    class ConnPoller
    {
    public:
        static constexpr short kRead = 0x1;
        static constexpr short kWrite = 0x2;

        bool init()
        {
#ifdef _WIN32
            return true; // WSAPoll 不需要先建集合
#else
            _epollFd = ::epoll_create1(0);
            return _epollFd != -1;
#endif
        }

        /**
         * 注册一条连接。
         * key 是「连接标识」而不是 fd 本身：内核会复用 fd，靠 fd 认连接迟早认成别人。
         * 这里把连接对象在连接表里的地址作为标识，每个事件都带回同一个指针。
         * 连接对象一旦建立就不再搬家（unordered_map 的节点是稳定的），所以指针可以长期持有。
         */
        bool add(SocketType socket, Connection *key, short events)
        {
#ifdef _WIN32
            for (auto &descriptor : _descriptors)
            {
                if (descriptor.fd == static_cast<decltype(descriptor.fd)>(INVALID_SOCKET))
                {
                    descriptor.fd = static_cast<decltype(descriptor.fd)>(socket);
                    descriptor.events = convert(events);
                    return true;
                }
            }
            pollfd descriptor = {};
            descriptor.fd = static_cast<decltype(descriptor.fd)>(socket);
            descriptor.events = convert(events);
            _descriptors.push_back(descriptor);
            return true;
#else
            epoll_event event = {};
            event.events = convert(events) | EPOLLRDHUP; // 对端半关也告诉我们
            event.data.ptr = key;                        // 用户数据放连接标识
            return ::epoll_ctl(_epollFd, EPOLL_CTL_ADD, socket, &event) == 0;
#endif
        }

        /** 改关注的事件。EPOLLOUT 的挂/摘就走这里 */
        bool modify(SocketType socket, Connection *key, short events)
        {
#ifdef _WIN32
            for (auto &descriptor : _descriptors)
            {
                if (descriptor.fd == static_cast<decltype(descriptor.fd)>(socket))
                {
                    descriptor.events = convert(events);
                    return true;
                }
            }
            return false;
#else
            epoll_event event = {};
            event.events = convert(events) | EPOLLRDHUP;
            event.data.ptr = key;
            return ::epoll_ctl(_epollFd, EPOLL_CTL_MOD, socket, &event) == 0;
#endif
        }

        /** 摘掉一条连接。必须在 close(fd) 之前调用 */
        bool remove(SocketType socket)
        {
#ifdef _WIN32
            for (auto &descriptor : _descriptors)
            {
                if (descriptor.fd == static_cast<decltype(descriptor.fd)>(socket))
                {
                    descriptor.fd = static_cast<decltype(descriptor.fd)>(INVALID_SOCKET);
                    descriptor.events = 0;
                    return true;
                }
            }
            return false;
#else
            return ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, socket, nullptr) == 0;
#endif
        }

        /**
         * 等事件。返回的就绪项由 callback(标识, 事件位) 逐个处理。
         * 返回 -1 且被打断（EINTR）不是错误，重试即可。
         */
        template <typename Callback>
        int wait(Callback callback, int timeoutMs)
        {
#ifdef _WIN32
            if (_descriptors.empty())
            {
                return 0;
            }
            const int readyCount = pollNative(
                _descriptors.data(),
                static_cast<unsigned long>(_descriptors.size()),
                timeoutMs);
            if (readyCount <= 0)
            {
                return readyCount;
            }
            int handled = 0;
            for (auto &descriptor : _descriptors)
            {
                if (descriptor.revents == 0)
                {
                    continue;
                }
                short events = 0;
                if (descriptor.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
                {
                    events |= kRead;
                }
                if (descriptor.revents & (POLLOUT | POLLERR))
                {
                    events |= kWrite;
                }
                descriptor.revents = 0;
                callback(static_cast<SocketType>(descriptor.fd), events);
                ++handled;
            }
            return handled;
#else
            epoll_event events[kMaxEventsPerWait] = {};
            while (true)
            {
                // 返回值是「这次有几个事件」，可能小于你给的上限
                const int readyCount = ::epoll_wait(_epollFd, events, kMaxEventsPerWait, timeoutMs);
                if (readyCount >= 0)
                {
                    for (int index = 0; index < readyCount; ++index)
                    {
                        const epoll_event &event = events[index];
                        short bits = 0;
                        if (event.events & (EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP))
                        {
                            bits |= kRead;
                        }
                        if (event.events & (EPOLLOUT | EPOLLERR))
                        {
                            bits |= kWrite;
                        }
                        // data.ptr 里是我们放进去的连接标识
                        callback(static_cast<Connection *>(event.data.ptr), bits);
                    }
                    return readyCount;
                }
                if (errno == EINTR)
                {
                    continue; // 被信号打断：不是错误，重试
                }
                return -1;
            }
#endif
        }

        ~ConnPoller()
        {
#ifndef _WIN32
            if (_epollFd != -1)
            {
                ::close(_epollFd);
            }
#endif
        }

    private:
#ifdef _WIN32
        static short convert(short events)
        {
            short result = 0;
            if (events & kRead)
            {
                result |= POLLIN;
            }
            if (events & kWrite)
            {
                result |= POLLOUT;
            }
            return result;
        }

        std::vector<pollfd> _descriptors; /**Windows 上只能靠这张表轮询 */
#else
        static std::uint32_t convert(short events)
        {
            std::uint32_t result = 0;
            if (events & kRead)
            {
                result |= EPOLLIN;
            }
            if (events & kWrite)
            {
                result |= EPOLLOUT;
            }
            return result;
        }

        int _epollFd = -1;
#endif
    };

    /**
     * 一个分片的雏形（A6 第 3、4、5 步）：**一条线程 + 一个事件集合 + 连接表**，管住全部连接。
     *
     * 这一阶的所有权规则（后面 M3/M4 会正式化）：
     *   - 只有这条事件循环线程能碰这里的所有状态，所以不需要任何锁；
     *   - 连接的删除只在「一轮事件处理结束之后」做，遍历中途删就是经典的 use-after-free。
     */
    class ConnShard
    {
    public:
        explicit ConnShard(SocketType listenSocket) : _listenSocket(listenSocket) {}

        bool init()
        {
            if (!_poller.init())
            {
                return false;
            }
            // 监听 socket 用空标识注册：它没有对应的 Connection，事件回调里靠 socket 相等认出来
            return _poller.add(_listenSocket, nullptr, ConnPoller::kRead);
        }

        void serve()
        {
            while (true)
            {
                // 两个平台带回的事件标识类型不同（Linux 是连接指针，Windows 是 socket），
                // 用泛型 lambda 让编译器自己挑对应的 handleEvent 重载。
                _poller.wait(
                    [this](auto key, short events) -> void
                    { handleEvent(key, events); },
                    kWaitTimeoutMs);

                // 第 5 步（清理阶段）：一轮事件全部处理完之后再删连接。
                // 遍历中途删就是「偶发崩溃 / 数据错乱」那一行的成因。
                removeClosedConnections();
            }
        }

    private:
#ifdef _WIN32
        /** Windows 的 WSAPoll 只能带回 socket 本身，所以回调参数是 socket，再查一次连接表 */
        void handleEvent(SocketType socket, short events)
        {
            if (socket == _listenSocket)
            {
                acceptConnections();
                return;
            }

            const auto found = _connections.find(socket);
            if (found == _connections.end())
            {
                return; // 已经不认识的 socket，丢掉
            }
            Connection *connection = &found->second;

            if (events & ConnPoller::kRead)
            {
                handleReadable(socket, connection);
            }
            // kRead 里可能已经关掉了这条连接，别接着用
            if (connection->socket == INVALID_SOCKET)
            {
                return;
            }
            if (events & ConnPoller::kWrite)
            {
                sendPending(connection);
            }
        }
#else
        /** Linux 的 epoll 直接带回我们注册的连接标识（指针），不用再查表 */
        void handleEvent(Connection *key, short events)
        {
            if (key == nullptr)
            {
                acceptConnections(); // 空标识 = 监听 socket
                return;
            }
            Connection *connection = key;

            if (events & ConnPoller::kRead)
            {
                handleReadable(connection->socket, connection);
            }
            if (connection->socket == INVALID_SOCKET)
            {
                return;
            }
            if (events & ConnPoller::kWrite)
            {
                sendPending(connection);
            }
        }
#endif

        void handleReadable(SocketType socket, Connection *connection)
        {
            char buffer[4096];
            while (true)
            {
                const int receivedBytes = recv(socket, buffer, sizeof(buffer), 0);

                if (receivedBytes == 0)
                {
                    // 可读不等于有数据：recv 返回 0 就是对端关闭。
                    // EPOLLRDHUP 只是提示，最终以 recv 的返回值为准。
                    closeConnection(connection);
                    return;
                }

                if (receivedBytes < 0)
                {
                    if (isWouldBlock())
                    {
                        break; // 这一轮的数据读完了
                    }
                    // 连接被重置（客户端被强杀）也会走到这里，不能当成「服务坏了」
                    closeConnection(connection);
                    return;
                }

                connection->sendBuffer.insert(
                    connection->sendBuffer.end(),
                    buffer,
                    buffer + receivedBytes);
            }

            // 收到东西就尽快发回去。发不完的部分留在 sendBuffer 里，由 kWrite 事件接着发。
            sendPending(connection);
        }

        void sendPending(Connection *connection)
        {
            while (connection->sendOffset < connection->sendBuffer.size())
            {
                const int sentBytes = send(
                    connection->socket,
                    connection->sendBuffer.data() + connection->sendOffset,
                    static_cast<int>(connection->sendBuffer.size() - connection->sendOffset),
#ifdef _WIN32
                    0);
#else
                    MSG_NOSIGNAL);
#endif
                if (sentBytes > 0)
                {
                    connection->sendOffset += static_cast<std::size_t>(sentBytes);
                    continue;
                }
                // 0 和 EAGAIN 都是「发送缓冲满了」：留着剩下的字节等可写事件
                if (sentBytes < 0 && !isWouldBlock())
                {
                    closeConnection(connection);
                    return;
                }
                break;
            }

            if (connection->sendOffset >= connection->sendBuffer.size() && !connection->sendBuffer.empty())
            {
                connection->sendBuffer.clear(); // 发完了就整块丢掉，同时把游标归零
                connection->sendOffset = 0;
            }

            // 第 5 步的核心：还有没发完的才挂 EPOLLOUT，发完立刻摘。
            // 一直挂着的话，epoll_wait 每次都告诉你「可写」，等于自己造了个忙等。
            const bool hasPendingSend = connection->sendOffset < connection->sendBuffer.size();
            _poller.modify(
                connection->socket,
                connection,
                static_cast<short>(ConnPoller::kRead | (hasPendingSend ? ConnPoller::kWrite : 0)));
        }

        void acceptConnections()
        {
            while (true)
            {
                sockaddr_in clientAddress = {};
#ifdef _WIN32
                int clientAddressLength = sizeof(clientAddress);
#else
                socklen_t clientAddressLength = sizeof(clientAddress);
#endif
                const SocketType clientSocket = accept(
                    _listenSocket,
                    reinterpret_cast<sockaddr *>(&clientAddress),
                    &clientAddressLength);

                if (clientSocket == INVALID_SOCKET)
                {
                    // 接到 EAGAIN 为止。接一次就回去等事件的话，1000 条连接会被摊成 1000 轮。
                    break;
                }

                setNonBlocking(clientSocket);

                Connection connection;
                connection.socket = clientSocket;
                const auto inserted = _connections.emplace(clientSocket, std::move(connection));
                if (!_poller.add(clientSocket, &inserted.first->second, ConnPoller::kRead))
                {
                    _connections.erase(inserted.first);
                    closesocket(clientSocket);
                    continue;
                }
            }
        }

        /** 只做登记：真正的删除放在一轮事件处理结束之后 */
        void closeConnection(Connection *connection)
        {
            if (connection->socket == INVALID_SOCKET)
            {
                return;
            }
            _closing.push_back(connection->socket);
            connection->socket = INVALID_SOCKET; // 标记成「已关」，本轮内不会再用它收发
        }

        void removeClosedConnections()
        {
            if (_closing.empty())
            {
                return;
            }

            for (const SocketType socket : _closing)
            {
                // 顺序很重要：先从事件集合里摘掉，再 close。反过来的话，
                // close 之后那个 fd 号可能立刻被新连接复用，摘掉时就摘错人了。
                _poller.remove(socket);
                _connections.erase(socket);
                closesocket(socket);
            }
            _closing.clear();
        }

        SocketType _listenSocket = INVALID_SOCKET;
        ConnPoller _poller;                                      /**事件集合 */
        std::unordered_map<SocketType, Connection> _connections; /**连接表：socket -> 连接状态，同时也是「谁是谁」的标识来源 */
        std::vector<SocketType> _closing;                        /**这一轮里要销毁的连接 */
    };
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

    // 从这里开始就是「一条线程管 N 条」：本函数所在的这条线程跑完整的事件循环，
    // 连接不再交给 TaskQueueLoop 里的其它线程。
    ConnShard shard(_acceptSocket);
    if (!shard.init())
    {
        UIMessageLibrary::addMessage(
            MessageType::error,
            0.0f,
            "创建事件集合失败，网络服务无法启动");
        return;
    }

    UIMessageLibrary::addMessage(
        MessageType::normal,
        0.0f,
        "事件循环已启动，等待连接");

    shard.serve();
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

#ifndef _WIN32
    // 留个自检：监听 socket 必须在非阻塞模式，否则 accept 循环会卡住整条事件线程。
    // （原来这里那段「调整为非阻塞」用了不存在的变量 fd，还把失败判断写反了。）
    if (success)
    {
        const int flags = fcntl(_acceptSocket, F_GETFL, 0);
        if (flags == -1 || (flags & O_NONBLOCK) == 0)
        {
            success = false;
            errorMessage = "监听Socket没有处于非阻塞模式";
        }
    }
#endif

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
            // 顺手就设成非阻塞：accept 循环要「接到 EAGAIN 为止」，
            // 少了这一步就会卡在 accept 上，后面 1000 条连接一条都接不进来。
            if (!setNonBlocking(_acceptSocket))
            {
                success = false;
                errorMessage = "把监听Socket设为非阻塞失败";
                closesocket(_acceptSocket);
                _acceptSocket = INVALID_SOCKET;
            }
            else
            {
                success = true;
            }
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
    // backlog 从 128 提到 1024：第 6 步会一次性涌进 1000 条连接，
    // backlog 太小会有一部分连不上（现象是「1000 条里只有几百条连上」）。
    if (listen(_acceptSocket, 1024) == SOCKET_ERROR)
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
