#ifndef INKING_BACKEND_FRAMEWORK_WAKEUP_H
#define INKING_BACKEND_FRAMEWORK_WAKEUP_H

/**
 * @file Wakeup.h
 * @brief 唤醒器：让别的线程把一个已经睡在 wait() 里的分片叫醒（设计文档 M2 / 台阶 C4）。
 *
 * 要解决的问题很具体：分片线程此时阻塞在内核的 epoll_wait / WSAPoll 里，
 * **条件变量叫不醒它**——它等的不是锁，是内核事件。所以不要去「叫醒线程」，
 * 而是「让它在等的那个东西自己变得可读」。
 *
 * 实现选型（记录理由，避免以后反复改）：
 * - 不用 eventfd：Windows 没有，等于再写一套平台分支；而 Linux 半边当前无法在本机
 *   编译验证，多一条没跑过的分支就是多一份负债。
 * - 用「回环 UDP 自投递」：一个绑在 127.0.0.1 上的 UDP socket，sendto 给自己。
 *   三个平台同一份代码、零 #ifdef，读端就是普通 fd，注册进 Poller 与普通连接同一条路径。
 * - 代价：每次唤醒一次 sendto + 一次 recv（比 eventfd 多一次用户态拷贝），
 *   按当前「一条在飞 + 合并通知」的设计，唤醒次数远小于请求数，量级上可以接受。
 *
 * 三条使用规则（违反任何一条都会变成丢唤醒或忙等）：
 * 1. **只在队列由空变非空时 notify**（判定必须在队列锁内完成），否则每投一条就写一次，
 *    唤醒本身会变成系统调用放大器。
 * 2. **被唤醒后必须 drain 干净**：读到 wouldBlock 为止。留一个字节在缓冲里，
 *    LT 模式会反复报告同一个事件，直接变成忙等。
 * 3. **注册一次，长期有效**：不要每次唤醒都 add/remove。
 */
#include "network/SocketOps.h"

#include <string>

class Wakeup
{
public:
    Wakeup() = default;
    ~Wakeup();

    Wakeup(const Wakeup &) = delete;
    Wakeup &operator=(const Wakeup &) = delete;

    /** 建好自投递通道并注册成非阻塞。失败时 errorMessage 说明原因 */
    bool init(std::string &errorMessage);

    /** 读端 fd：交给 Poller 注册 */
    SocketType socket() const
    {
        return _socket;
    }

    /**
     * @brief 通知一次。可以在任意线程调用。
     *
     * 失败一律吞掉：唤醒是「尽力而为」的信号，真正的数据在队列里，
     * 即使这一次写失败，下一次队列变化还会再写一次。在这里抛异常或返回错误，
     * 只会把一条本来该成功的业务路径搞成失败。
     */
    void notify();

    /** 排空。只能在分片线程里调用，读到 wouldBlock 为止 */
    void drain();

    void close();

private:
    SocketType _socket = kInvalidSocket;
    sockaddr_in _address = {};
};

#endif // INKING_BACKEND_FRAMEWORK_WAKEUP_H
