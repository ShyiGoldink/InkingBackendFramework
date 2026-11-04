#ifndef INKING_BACKEND_FRAMEWORK_POLLER_H
#define INKING_BACKEND_FRAMEWORK_POLLER_H

/**
 * @file Poller.h
 * @brief 「fd 怎么等」这件事的唯一抽象（设计文档 M1 / 台阶 B1）。
 *
 * 抽象的价值不在接口好看，而在下面三件事被统一了：
 *
 * 1. **句柄类型**：上层只认 SocketType，不假设它是 int。
 * 2. **事件语义**：三个平台原始事件名与含义都不同，统一成 Read / Write 两位。
 *    「对端关闭」不单独给一位——它和可读合并，由 recv 返回 0 判定（WSAPoll 的
 *    POLLHUP 不可靠，这是被踩过的坑）。
 * 3. **注册时的用户数据（tag）**：注册 fd 时附带一个 void*，事件返回时原样带回。
 *    这是避免「用 fd 号认连接」的唯一办法——内核会复用 fd 号，靠 fd 认连接迟早认错人。
 *
 * 实现只有一个 private 的细节差异：Linux 走 epoll（LT 模式），Windows 走 WSAPoll。
 * 上层代码一套，不出现任何 #ifdef。
 */

#include "network/SocketOps.h"

#include <memory>
#include <string>
#include <vector>

/** 统一后的两类关注/就绪位。故意用普通常量而不是 enum class：这里要的是能按位或 */
namespace PollerBits
{
    inline constexpr unsigned Read = 0x1;
    inline constexpr unsigned Write = 0x2;
}

/** 一次就绪事件：谁（socket）、是谁（tag）、要干什么（events） */
struct PollerEvent
{
    SocketType socket = kInvalidSocket;
    void *tag = nullptr;
    unsigned events = 0;
};

class Poller
{
public:
    virtual ~Poller() = default;

    /** 创建事件集合。Linux 上是 epoll_create1，Windows 上只清空登记表 */
    virtual bool init(std::string &errorMessage) = 0;

    /**
     * @brief 注册一个 fd。
     *
     * @param tag 事件返回时原样带回的用户数据。调用方保证它在 fd 注销之前一直有效。
     *
     * 注意：tag 允许为 nullptr，但**不要让 nullptr 承担业务含义**——想要「这是监听
     * socket」之类的标记就传一个有地址的对象进去，否则「没传 tag」和「tag 恰好是空」
     * 会混成同一个分支。
     */
    virtual bool add(SocketType socket, void *tag, unsigned events) = 0;

    /** 改关注的事件。EPOLLOUT 的挂与摘就走这里 */
    virtual bool modify(SocketType socket, void *tag, unsigned events) = 0;

    /** 摘掉一个 fd。**必须在 close 之前调用**：close 之后 fd 号可能立刻被复用，摘错人 */
    virtual bool remove(SocketType socket) = 0;

    /**
     * @brief 等就绪事件，最多等 timeoutMs 毫秒。
     *
     * @param ready 输出参数，函数内部先清空再填充，允许调用方复用以避免每轮分配。
     * @return 就绪事件数；负数是错误（EINTR 已在实现内部重试，不会冒出来）。
     *
     * 超时必须是接口的一部分：定时器（C5）没有它就没法实现，而「等多久」也是
     * 分片唯一的主动退出时机。
     */
    virtual int wait(std::vector<PollerEvent> &ready, int timeoutMs) = 0;

    /** 集合的容量上限说明，仅用于日志：Linux 是「无所谓」，Windows 是「当前槽位数」 */
    virtual std::size_t capacity() const = 0;
};

/**
 * @brief 按平台创建实现。
 *
 * 返回的实现由调用方持有（每个分片一个），生命周期跟着分片走。
 */
std::unique_ptr<Poller> createPoller();

#endif // INKING_BACKEND_FRAMEWORK_POLLER_H
