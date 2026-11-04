#include "network/Poller.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

#ifndef _WIN32
// epoll 的头只在 Linux 侧需要。Windows 侧由 SocketOps.h 带入 winsock2.h。
// 这一行曾经漏掉过：epoll 分支在没有 Linux 编译验证的情况下写了三个月，
// 一直到 C5 阶段真拿 Linux 编才发现（见 docs/NetworkLayerLadder.md §6.5）。
#include <sys/epoll.h>
#endif

namespace
{
    /** 一轮最多取回多少个就绪事件。取太多会让单个事件的处理延迟被一批放大 */
    constexpr int kMaxEventsPerWait = 256;

#ifdef _WIN32
    unsigned convertEvents(short revents)
    {
        unsigned bits = 0;
        // POLLHUP / POLLNVAL 也算可读：让上层用 recv 拿到 0 或错误，比在这里判断更准
        if (revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
        {
            bits |= PollerBits::Read;
        }
        if (revents & (POLLOUT | POLLERR))
        {
            bits |= PollerBits::Write;
        }
        return bits;
    }

    short convertInterest(unsigned events)
    {
        short result = 0;
        if (events & PollerBits::Read)
        {
            result |= POLLIN;
        }
        if (events & PollerBits::Write)
        {
            result |= POLLOUT;
        }
        return result;
    }

    /**
     * Windows：WSAPoll 的薄封装。
     *
     * 两张平行数组是本类的全部状态：`_descriptors[i]` 是 WSAPoll 要的 pollfd，
     * `_tags[i]` 是这条 fd 的用户数据。**不能把 tag 塞进 pollfd 的同一块内存里
     * 再强转成 POLLFD 数组**——WSAPoll 按 sizeof(POLLFD) 的步长读数组，多出来的
     * 那几个字节会被当成下一条记录的开头，症状是事件对应到完全无关的连接。
     *
     * 空位的标记是 fd = INVALID_SOCKET：WSAPoll 明确忽略它，正好可以当墓碑。
     */
    class PollPoller final : public Poller
    {
    public:
        bool init(std::string &errorMessage) override
        {
            _descriptors.clear();
            _tags.clear();
            errorMessage.clear();
            return true; // WSAPoll 不需要先建集合
        }

        bool add(SocketType socket, void *tag, unsigned events) override
        {
            const std::size_t freeIndex = findFreeIndex();
            if (freeIndex != kNoIndex)
            {
                _descriptors[freeIndex].fd = socket;
                _descriptors[freeIndex].events = convertInterest(events);
                _descriptors[freeIndex].revents = 0;
                _tags[freeIndex] = tag;
                return true;
            }

            pollfd descriptor = {};
            descriptor.fd = socket;
            descriptor.events = convertInterest(events);
            _descriptors.push_back(descriptor);
            _tags.push_back(tag);
            return true;
        }

        bool modify(SocketType socket, void *tag, unsigned events) override
        {
            const std::size_t index = findIndex(socket);
            if (index == kNoIndex)
            {
                return false;
            }
            _descriptors[index].events = convertInterest(events);
            _tags[index] = tag;
            return true;
        }

        bool remove(SocketType socket) override
        {
            const std::size_t index = findIndex(socket);
            if (index == kNoIndex)
            {
                return false;
            }

            _descriptors[index].fd = INVALID_SOCKET;
            _descriptors[index].events = 0;
            _descriptors[index].revents = 0;
            _tags[index] = nullptr;

            // 尾部连续空位直接弹出：槽位数只跟「同时在线的峰值连接数」有关，
            // 而不是跟「开服以来一共连过多少条」有关。A6 留下的这条技术债在这里结掉。
            while (!_descriptors.empty() && _descriptors.back().fd == INVALID_SOCKET)
            {
                _descriptors.pop_back();
                _tags.pop_back();
            }
            return true;
        }

        int wait(std::vector<PollerEvent> &ready, int timeoutMs) override
        {
            ready.clear();

            // 空集合上 WSAPoll 会立刻返回 0，直接返回会让调用方变成忙等。
            // 分片在正常运行时至少注册了唤醒器，走不到这里；留一条兜底路径。
            if (_descriptors.empty())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::clamp(timeoutMs, 1, 100)));
                return 0;
            }

            const int readyCount = ::WSAPoll(
                _descriptors.data(),
                static_cast<ULONG>(_descriptors.size()),
                timeoutMs);
            if (readyCount <= 0)
            {
                return readyCount;
            }

            for (std::size_t index = 0; index < _descriptors.size(); ++index)
            {
                if (_descriptors[index].revents == 0)
                {
                    continue;
                }
                PollerEvent event;
                event.socket = _descriptors[index].fd;
                event.tag = _tags[index];
                event.events = convertEvents(_descriptors[index].revents);
                _descriptors[index].revents = 0;
                ready.push_back(event);
            }
            return static_cast<int>(ready.size());
        }

        std::size_t capacity() const override
        {
            return _descriptors.size();
        }

    private:
        static constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

        std::size_t findIndex(SocketType socket) const
        {
            for (std::size_t index = 0; index < _descriptors.size(); ++index)
            {
                if (_descriptors[index].fd == socket)
                {
                    return index;
                }
            }
            return kNoIndex;
        }

        std::size_t findFreeIndex() const
        {
            for (std::size_t index = 0; index < _descriptors.size(); ++index)
            {
                if (_descriptors[index].fd == INVALID_SOCKET)
                {
                    return index;
                }
            }
            return kNoIndex;
        }

        std::vector<pollfd> _descriptors;
        std::vector<void *> _tags;
    };
#else
    unsigned convertInterest(unsigned events)
    {
        std::uint32_t result = 0;
        if (events & PollerBits::Read)
        {
            result |= EPOLLIN;
        }
        if (events & PollerBits::Write)
        {
            result |= EPOLLOUT;
        }
        return result;
    }

    /**
     * Linux：epoll（LT 模式）。
     *
     * 选 LT 而不是 ET：ET 要求「必须一次读到 EAGAIN」，和「一条在飞」的语义（拆出
     * 一帧就停手）直接冲突；ET 的收益要几千连接才看得出来，代价是每处读循环都得写对。
     */
    class EpollPoller final : public Poller
    {
    public:
        ~EpollPoller() override
        {
            if (_epollFd != -1)
            {
                ::close(_epollFd);
            }
        }

        bool init(std::string &errorMessage) override
        {
            _epollFd = ::epoll_create1(0);
            if (_epollFd == -1)
            {
                errorMessage = "创建 epoll 集合失败:" + SocketOps::lastErrorText();
                return false;
            }
            errorMessage.clear();
            return true;
        }

        bool add(SocketType socket, void *tag, unsigned events) override
        {
            epoll_event event = buildEvent(tag, events);
            return ::epoll_ctl(_epollFd, EPOLL_CTL_ADD, socket, &event) == 0;
        }

        bool modify(SocketType socket, void *tag, unsigned events) override
        {
            epoll_event event = buildEvent(tag, events);
            return ::epoll_ctl(_epollFd, EPOLL_CTL_MOD, socket, &event) == 0;
        }

        bool remove(SocketType socket) override
        {
            return ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, socket, nullptr) == 0;
        }

        int wait(std::vector<PollerEvent> &ready, int timeoutMs) override
        {
            ready.clear();
            epoll_event events[kMaxEventsPerWait] = {};
            while (true)
            {
                const int readyCount = ::epoll_wait(_epollFd, events, kMaxEventsPerWait, timeoutMs);
                if (readyCount < 0)
                {
                    if (errno == EINTR)
                    {
                        continue; // 被信号打断不是错误
                    }
                    return -1;
                }

                for (int index = 0; index < readyCount; ++index)
                {
                    const epoll_event &event = events[index];
                    PollerEvent item;
                    // data.ptr 里是注册时放进去的 tag，不是 fd：fd 号会被复用。
                    // epoll 不回传 fd，所以 socket 位留空——上层一律按 tag 分派。
                    item.socket = kInvalidSocket;
                    item.tag = event.data.ptr;
                    if (event.events & (EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP))
                    {
                        item.events |= PollerBits::Read;
                    }
                    if (event.events & (EPOLLOUT | EPOLLERR))
                    {
                        item.events |= PollerBits::Write;
                    }
                    ready.push_back(item);
                }
                return readyCount;
            }
        }

        std::size_t capacity() const override
        {
            return 0; // epoll 没有「槽位」概念
        }

    private:
        static epoll_event buildEvent(void *tag, unsigned events)
        {
            epoll_event event = {};
            // EPOLLRDHUP 常开：对端半关也要能立刻知道，否则只能等下一次 recv 出错
            event.events = convertInterest(events) | EPOLLRDHUP;
            event.data.ptr = tag;
            return event;
        }

        int _epollFd = -1;
    };
#endif
}

std::unique_ptr<Poller> createPoller()
{
#ifdef _WIN32
    return std::make_unique<PollPoller>();
#else
    return std::make_unique<EpollPoller>();
#endif
}
