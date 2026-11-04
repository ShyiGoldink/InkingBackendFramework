#ifndef INKING_BACKEND_FRAMEWORK_SHARD_MAILBOX_H
#define INKING_BACKEND_FRAMEWORK_SHARD_MAILBOX_H

/**
 * @file ShardMailbox.h
 * @brief 业务线程 → 分片的回投通道（设计文档 §2.2 的「跨线程边界 2」）。
 *
 * 为什么需要它：业务线程算完了要把字节交回去，但它**不许碰 socket、不许碰连接对象**
 * （连接归分片独占）。所以两边只通过这个信箱传两样东西：**一个能识别连接的标识** +
 * **一段字节**。分片拿到后自己去找连接、自己进写缓冲、自己发。
 *
 * 为什么它不是分片的成员变量而是一个 shared_ptr 对象：
 * 一次请求派发出去之后，分片可能先退出（停机、或连接被断），而业务线程手里的任务
 * 还在跑。任务结束时如果往一个已销毁的分片成员里塞东西，就是 use-after-free。
 * 把信箱做成独立对象、由分片和「在飞的任务」共同持有，分片析构时只是 `detach()`：
 * 之后 `post()` 会看到关闭标记并安静丢弃，不会碰到任何已经失效的内存。
 *
 * 唤醒的合并规则与交接队列一致：**只在队列由空变非空时通知一次**，
 * 分片正忙时到达的 N 条响应只产生 1 次唤醒写。
 */

#include "network/Wakeup.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class ShardMailbox
{
public:
    struct Reply
    {
        std::uint64_t connectionId = 0; /** 分片分配的连接标识（连接死了就对不上） */
        std::string bytes;              /** 已组好的响应帧，分片把它原样倒进写缓冲 */
        bool handlerOk = true;          /** 业务处理是否成功（失败时 bytes 是空 payload 的帧） */
    };

    ShardMailbox() = default;

    /**
     * @brief 绑定唤醒器。只在分片线程上调用一次。
     *
     * 这里用 shared_ptr 而不是裸指针：`post()` 与分片析构可能并发，
     * 裸指针读出来之后对象可能已经没了；shared_ptr 的副本能把唤醒器「钉住」到本次调用结束。
     */
    void attach(const std::shared_ptr<Wakeup> &wakeup);

    /** 分片析构时调用：之后的 post 一律丢弃 */
    void detach();

    /** 投一条响应。任意线程可调用 */
    void post(std::uint64_t connectionId, std::string bytes, bool handlerOk = true);

    /** 整批取走。只在分片线程上调用 */
    void drain(std::vector<Reply> &out);

    bool isAttached() const;

private:
    mutable std::mutex _mutex;
    std::vector<Reply> _queue;
    std::shared_ptr<Wakeup> _wakeup;
    bool _closed = false;
};

#endif // INKING_BACKEND_FRAMEWORK_SHARD_MAILBOX_H
