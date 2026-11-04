/**
 * @file bench_client.cpp
 * @brief 负载发生器：N 条连接 × 每条 M 条请求，量吞吐与分位数（设计文档 M10 / 台阶 C7）。
 *
 * 为什么不用 Python 写：Python 的 socket + GIL 会把「客户端自己」变成瓶颈，
 * 量出来的是 Python 有多慢，不是服务端有多快。这个工具直接复用框架自己的
 * `Poller`（Windows 上是 WSAPoll、Linux 上是 epoll），单线程驱动上千条连接，
 * 客户端开销被压到最低。
 *
 * 为什么是「一条在飞」：服务端就是一条在飞的模型（M7）。客户端如果猛灌，
 * 量到的就不是「一次请求多久」，而是「服务端缓冲区能吞多少」。
 * 一条在飞时，延迟就是真正的往返时间，p99 才有意义。
 *
 * 用法：
 *   bench_client --conns 64 --frames 2000 --payload 256
 *   bench_client --conns 64 --frames 2000 --payload 256 --command bench-sleep --sleep-us 100
 *   bench_client --conns 1 --frames 20000 --payload 64 --warmup 2000
 *
 * 输出最后一行是给脚本解析的定长格式（RESULT ...），前面是人类可读的说明。
 */

#include "network/Poller.h"
#include "network/SocketOps.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::uint16_t kMagic = 0xABCD;
    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kMaxFrameBytes = 16u * 1024u * 1024u;
    constexpr std::size_t kMaxCommandBytes = 1024;
    constexpr int kConnectTimeoutMs = 5000;

    struct Options
    {
        std::string host = "127.0.0.1";
        int port = 9000;
        std::size_t conns = 64;
        std::size_t frames = 2000;
        std::size_t payload = 256;
        std::size_t warmup = 200;
        std::string command = "echo";
        int sleepUs = 0; /** 配合 bench-sleep：写进 payload 前 4 字节的微秒数 */
        std::string label;
        double timeoutSeconds = 30.0;
    };

    /** 一条连接的运行状态。客户端是单线程的，所以没有任何锁 */
    struct Peer
    {
        SocketType socket = kInvalidSocket;
        std::vector<char> incoming; /** 收到但还没拼成整帧的字节 */
        std::size_t consumed = 0;   /** incoming 里已经处理掉的字节数（避免每次 erase 头部） */
        std::size_t sendOffset = 0; /** 当前这一帧已经发出去多少 */
        bool waiting = false;       /** 已发出请求，等响应 */
        std::size_t framesDone = 0;
        Clock::time_point sentAt;
        unsigned interest = 0;
    };

    std::uint32_t readBigEndian32(const char *data)
    {
        const auto *bytes = reinterpret_cast<const unsigned char *>(data);
        return (static_cast<std::uint32_t>(bytes[0]) << 24) |
               (static_cast<std::uint32_t>(bytes[1]) << 16) |
               (static_cast<std::uint32_t>(bytes[2]) << 8) |
               static_cast<std::uint32_t>(bytes[3]);
    }

    std::string buildFrame(const std::string &command, const std::string &payload)
    {
        const std::size_t total = kHeaderBytes + command.size() + payload.size();
        std::string frame;
        frame.resize(total);
        frame[0] = static_cast<char>((kMagic >> 8) & 0xFF);
        frame[1] = static_cast<char>(kMagic & 0xFF);
        frame[2] = static_cast<char>((total >> 24) & 0xFF);
        frame[3] = static_cast<char>((total >> 16) & 0xFF);
        frame[4] = static_cast<char>((total >> 8) & 0xFF);
        frame[5] = static_cast<char>(total & 0xFF);
        frame[6] = static_cast<char>((command.size() >> 8) & 0xFF);
        frame[7] = static_cast<char>(command.size() & 0xFF);
        std::memcpy(frame.data() + kHeaderBytes, command.data(), command.size());
        std::memcpy(frame.data() + kHeaderBytes + command.size(), payload.data(), payload.size());
        return frame;
    }

    /** payload 前 4 字节写小端微秒数，剩下的填 a。bench-sleep 用它表达「睡多久」 */
    std::string buildPayload(const Options &options)
    {
        std::string payload(options.payload, 'a');
        if (payload.size() < 4)
        {
            payload.resize(4, 'a');
        }
        const auto micros = static_cast<std::uint32_t>(options.sleepUs < 0 ? 0 : options.sleepUs);
        payload[0] = static_cast<char>(micros & 0xFF);
        payload[1] = static_cast<char>((micros >> 8) & 0xFF);
        payload[2] = static_cast<char>((micros >> 16) & 0xFF);
        payload[3] = static_cast<char>((micros >> 24) & 0xFF);
        return payload;
    }

    bool parseOptions(int argc, char **argv, Options &options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string key = argv[index];
            const auto next = [&](std::string &out)
            {
                if (index + 1 >= argc)
                {
                    return false;
                }
                out = argv[++index];
                return true;
            };
            std::string value;
            if (key == "--host" && next(value))
            {
                options.host = value;
            }
            else if (key == "--port" && next(value))
            {
                options.port = std::stoi(value);
            }
            else if (key == "--conns" && next(value))
            {
                options.conns = static_cast<std::size_t>(std::stoull(value));
            }
            else if (key == "--frames" && next(value))
            {
                options.frames = static_cast<std::size_t>(std::stoull(value));
            }
            else if (key == "--payload" && next(value))
            {
                options.payload = static_cast<std::size_t>(std::stoull(value));
            }
            else if (key == "--warmup" && next(value))
            {
                options.warmup = static_cast<std::size_t>(std::stoull(value));
            }
            else if (key == "--command" && next(value))
            {
                options.command = value;
            }
            else if (key == "--sleep-us" && next(value))
            {
                options.sleepUs = std::stoi(value);
            }
            else if (key == "--label" && next(value))
            {
                options.label = value;
            }
            else if (key == "--timeout" && next(value))
            {
                options.timeoutSeconds = std::stod(value);
            }
            else
            {
                std::fprintf(stderr, "不认识的参数: %s\n", key.c_str());
                return false;
            }
        }

        if (options.conns == 0 || options.frames == 0)
        {
            std::fprintf(stderr, "--conns 与 --frames 都必须大于 0\n");
            return false;
        }
        if (options.command.size() > kMaxCommandBytes)
        {
            std::fprintf(stderr, "指令名太长\n");
            return false;
        }
        return true;
    }

    /** 把这条连接当前的整帧全取出来，记录延迟并决定「发下一条」还是「收工」 */
    struct DrainResult
    {
        std::size_t finished = 0; /** 本次取出的完整帧数 */
        bool protocolError = false;
        bool peerClosed = false;
    };

    DrainResult drainFrames(Peer &peer, const Options &options, const std::string &frame,
                            std::vector<std::uint64_t> &latencies)
    {
        DrainResult result;

        while (true)
        {
            const std::size_t available = peer.incoming.size() - peer.consumed;
            if (available < kHeaderBytes)
            {
                break;
            }
            const char *base = peer.incoming.data() + peer.consumed;
            const std::uint16_t magic = static_cast<std::uint16_t>(
                (static_cast<unsigned char>(base[0]) << 8) | static_cast<unsigned char>(base[1]));
            const std::uint32_t total = readBigEndian32(base + 2);
            if (magic != kMagic || total < kHeaderBytes || total > kMaxFrameBytes)
            {
                result.protocolError = true;
                return result;
            }
            if (available < total)
            {
                break;
            }

            peer.consumed += total;
            ++peer.framesDone;
            ++result.finished;

            // 预热阶段的前 warmup 条不计入分位数：前几条包含缓存冷启动，
            // 混进去会让 p99 看起来比真实值差（那是测量噪声，不是服务端问题）
            if (peer.framesDone > options.warmup)
            {
                const auto now = Clock::now();
                latencies.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - peer.sentAt).count()));
            }

            if (peer.framesDone >= options.frames)
            {
                peer.waiting = false;
                return result; // 这条连接跑完了
            }

            // 发下一条。小帧在这里基本一次就发完；发不完就留给可写事件
            peer.sendOffset = 0;
            peer.sentAt = Clock::now();
            peer.waiting = true;
        }

        if (peer.consumed > 0 && peer.consumed == peer.incoming.size())
        {
            peer.incoming.clear();
            peer.consumed = 0;
        }
        return result;
    }

    bool connectAll(const Options &options, std::vector<Peer> &peers, std::string &error)
    {
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(options.port));
        if (::inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1)
        {
            error = "主机地址不合法: " + options.host;
            return false;
        }

        peers.resize(options.conns);
        for (std::size_t index = 0; index < options.conns; ++index)
        {
            Peer &peer = peers[index];
            peer.socket = ::socket(AF_INET, SOCK_STREAM, 0);
            if (peer.socket == kInvalidSocket)
            {
                error = "创建 socket 失败: " + SocketOps::lastErrorText();
                return false;
            }

            // 建连阶段用阻塞 + 超时：一次连 1000 条时，非阻塞 connect 要额外维护
            // 「哪些还在连」的状态机，那是服务端才需要的东西，客户端不值得。
            {
                timeval tv{};
                tv.tv_sec = kConnectTimeoutMs / 1000;
#ifdef _WIN32
                const char *opt = reinterpret_cast<const char *>(&tv);
#else
                const void *opt = &tv;
#endif
                ::setsockopt(peer.socket, SOL_SOCKET, SO_RCVTIMEO, opt, sizeof(tv));
                ::setsockopt(peer.socket, SOL_SOCKET, SO_SNDTIMEO, opt, sizeof(tv));
            }

            if (::connect(peer.socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
            {
                error = "连接 " + options.host + ":" + std::to_string(options.port) +
                        " 失败: " + SocketOps::lastErrorText() +
                        "（服务端跑起来了吗？控制台里执行 network-run）";
                return false;
            }

            // 之后必须非阻塞：阻塞 recv 会把整个事件循环按在一条连接上
            SocketOps::setNonBlocking(peer.socket);
            SocketOps::setNoDelay(peer.socket);
        }
        return true;
    }

    bool sendRequest(Peer &peer, const std::string &frame, unsigned &interest)
    {
        while (peer.sendOffset < frame.size())
        {
            const int sent = ::send(peer.socket, frame.data() + peer.sendOffset,
                                    static_cast<int>(frame.size() - peer.sendOffset),
                                    SocketOps::sendFlags());
            if (sent > 0)
            {
                peer.sendOffset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && SocketOps::wouldBlock())
            {
                interest = PollerBits::Read | PollerBits::Write; // 等可写再接着发
                return true;
            }
            return false; // 发送出错
        }
        interest = PollerBits::Read;
        return true;
    }

    double percentile(const std::vector<std::uint64_t> &sorted, double ratio)
    {
        if (sorted.empty())
        {
            return 0.0;
        }
        std::size_t index = static_cast<std::size_t>(ratio * static_cast<double>(sorted.size() - 1));
        if (index >= sorted.size())
        {
            index = sorted.size() - 1;
        }
        return static_cast<double>(sorted[index]) / 1000.0; // 转成微秒
    }
}

int main(int argc, char **argv)
{
    Options options;
    if (!parseOptions(argc, argv, options))
    {
        return 2;
    }

    std::string error;
    if (!SocketOps::startUp(error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    const std::string payload = buildPayload(options);
    const std::string frame = buildFrame(options.command, payload);

    std::vector<Peer> peers;
    if (!connectAll(options, peers, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    std::printf("连接 %zu 条，每条 %zu 帧（预热 %zu），payload %zu 字节，指令 %s\n",
                options.conns, options.frames, options.warmup, payload.size(),
                options.command.c_str());

    auto poller = createPoller();
    if (!poller->init(error))
    {
        std::fprintf(stderr, "创建事件集合失败: %s\n", error.c_str());
        return 2;
    }

    for (std::size_t index = 0; index < peers.size(); ++index)
    {
        Peer &peer = peers[index];
        peer.sentAt = Clock::now();
        peer.waiting = true;
        if (!sendRequest(peer, frame, peer.interest))
        {
            std::fprintf(stderr, "第 %zu 条连接首发失败\n", index);
            return 2;
        }
        // tag 用下标而不是指针：peers 是 vector，resize 过之后指针会变，
        // 下标永远有效且能直接算回元素。
        if (!poller->add(peer.socket, reinterpret_cast<void *>(index + 1), peer.interest))
        {
            std::fprintf(stderr, "注册第 %zu 条连接失败: %s\n", index, SocketOps::lastErrorText());
            return 2;
        }
    }

    const auto begin = Clock::now();
    const auto deadline = begin + std::chrono::duration<double>(options.timeoutSeconds);

    std::vector<std::uint64_t> latencies;
    latencies.reserve(options.conns * (options.frames - std::min(options.frames, options.warmup)));

    std::vector<PollerEvent> ready;
    std::size_t active = 0;
    for (const Peer &peer : peers)
    {
        if (peer.socket != kInvalidSocket)
        {
            ++active;
        }
    }

    std::size_t ok = 0;
    std::size_t failed = 0;
    bool protocolError = false;

    char buffer[65536];
    while (active > 0 && Clock::now() < deadline)
    {
        const int readyCount = poller->wait(ready, 200);
        if (readyCount < 0)
        {
            std::fprintf(stderr, "等待事件失败: %s\n", SocketOps::lastErrorText());
            return 2;
        }

        for (const PollerEvent &event : ready)
        {
            const std::size_t index = reinterpret_cast<std::size_t>(event.tag) - 1;
            if (index >= peers.size())
            {
                continue;
            }
            Peer &peer = peers[index];
            if (peer.socket == kInvalidSocket)
            {
                continue;
            }

            // 先把没发完的发掉（可写事件到来时）
            if ((event.events & PollerBits::Write) && peer.sendOffset > 0 &&
                peer.sendOffset < frame.size())
            {
                unsigned interest = peer.interest;
                if (!sendRequest(peer, frame, interest))
                {
                    ++failed;
                    poller->remove(peer.socket);
                    SocketOps::close(peer.socket);
                    peer.socket = kInvalidSocket;
                    --active;
                    continue;
                }
                if (interest != peer.interest)
                {
                    poller->modify(peer.socket, event.tag, interest);
                    peer.interest = interest;
                }
            }

            if ((event.events & PollerBits::Read) == 0)
            {
                continue;
            }

            bool closed = false;
            while (true)
            {
                const int received = ::recv(peer.socket, buffer, sizeof(buffer), 0);
                if (received > 0)
                {
                    peer.incoming.insert(peer.incoming.end(), buffer, buffer + received);
                    continue;
                }
                if (received == 0)
                {
                    closed = true;
                    break;
                }
                if (SocketOps::wouldBlock())
                {
                    break;
                }
                closed = true;
                break;
            }

            const std::size_t before = peer.framesDone;
            const DrainResult drained = drainFrames(peer, options, frame, latencies);
            ok += drained.finished;
            if (drained.protocolError)
            {
                protocolError = true;
            }

            const bool done = peer.framesDone >= options.frames;
            if (!done && peer.waiting)
            {
                unsigned interest = peer.interest;
                if (!sendRequest(peer, frame, interest))
                {
                    closed = true;
                }
                if (!closed && interest != peer.interest)
                {
                    poller->modify(peer.socket, event.tag, interest);
                    peer.interest = interest;
                }
            }

            if (done || closed || protocolError)
            {
                if (closed && !done)
                {
                    failed += options.frames - before;
                }
                poller->remove(peer.socket);
                SocketOps::close(peer.socket);
                peer.socket = kInvalidSocket;
                --active;
            }
        }
    }

    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();

    for (Peer &peer : peers)
    {
        if (peer.socket != kInvalidSocket)
        {
            poller->remove(peer.socket);
            SocketOps::close(peer.socket);
            peer.socket = kInvalidSocket;
        }
    }

    std::sort(latencies.begin(), latencies.end());
    const double throughput = seconds > 0.0 ? static_cast<double>(ok) / seconds : 0.0;

    std::printf("----------------------------------------\n");
    std::printf("完成 %zu 条，失败 %zu 条，耗时 %.3f 秒，吞吐 %.0f 条/秒\n",
                ok, failed, seconds, throughput);
    std::printf("延迟(us)  p50 %.0f  p90 %.0f  p99 %.0f  p99.9 %.0f  max %.0f\n",
                percentile(latencies, 0.50), percentile(latencies, 0.90),
                percentile(latencies, 0.99), percentile(latencies, 0.999),
                latencies.empty() ? 0.0 : static_cast<double>(latencies.back()) / 1000.0);

    // 给脚本解析的一行。字段顺序固定，新增字段只能追加在末尾
    std::printf("RESULT label=%s conns=%zu frames=%zu payload=%zu command=%s ok=%zu fail=%zu "
                "elapsed_s=%.3f throughput=%.0f p50_us=%.0f p90_us=%.0f p99_us=%.0f "
                "p999_us=%.0f max_us=%.0f\n",
                options.label.empty() ? "-" : options.label.c_str(),
                options.conns, options.frames, payload.size(), options.command.c_str(),
                ok, failed, seconds, throughput,
                percentile(latencies, 0.50), percentile(latencies, 0.90),
                percentile(latencies, 0.99), percentile(latencies, 0.999),
                latencies.empty() ? 0.0 : static_cast<double>(latencies.back()) / 1000.0);

    SocketOps::cleanUp();

    const bool good = failed == 0 && !protocolError && ok > 0;
    if (protocolError)
    {
        std::fprintf(stderr, "协议错误：回包帧头不合法\n");
    }
    return good ? 0 : 1;
}
