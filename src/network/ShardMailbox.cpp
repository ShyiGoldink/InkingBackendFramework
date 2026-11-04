#include "network/ShardMailbox.h"

void ShardMailbox::attach(const std::shared_ptr<Wakeup> &wakeup)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _wakeup = wakeup;
    _closed = false;
}

void ShardMailbox::detach()
{
    std::shared_ptr<Wakeup> wakeup;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _closed = true;
        wakeup = std::move(_wakeup); // 释放引用，让唤醒器随最后一个持有者一起销毁
        _queue.clear();
    }
    // 唤醒器由 shared_ptr 管理，这里不需要显式关闭；它自己的析构会关 fd
}

void ShardMailbox::post(std::uint64_t connectionId, std::string bytes, bool handlerOk)
{
    std::shared_ptr<Wakeup> wakeup;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_closed)
        {
            // 分片已经退出。这条响应没有归宿，安静丢掉是唯一正确的处理：
            // 连接已经没了，报错也没有人能让它变得有意义。
            return;
        }
        if (_queue.empty())
        {
            // 「由空变非空」才唤醒：分片正忙时到达的 N 条响应只产生 1 次唤醒写
            wakeup = _wakeup;
        }
        _queue.push_back(Reply{connectionId, std::move(bytes), handlerOk});
    }

    // 通知放在锁外：唤醒本身是一次系统调用，不该占着锁做
    if (wakeup)
    {
        wakeup->notify();
    }
}

void ShardMailbox::drain(std::vector<Reply> &out)
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_queue.empty())
        {
            return;
        }
        out.swap(_queue);
    }
    // 交换之后 out 里是本轮的响应；调用方处理完自己 clear 复用
}
