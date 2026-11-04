#include "network/TimerQueue.h"

#include <algorithm>
#include <chrono>

namespace
{
    /**
     * 堆里陈旧条目超过这个比例就整体重建。
     *
     * 64 是「小规模时不折腾」的门槛：连接只有几十条时分片刚启动、还没到需要压紧的
     * 规模，重建反而比留几条垃圾更贵。
     */
    constexpr std::size_t kCompactSlack = 64;
    constexpr std::size_t kCompactFactor = 2;
}

const char *timeoutKindText(TimeoutKind kind)
{
    switch (kind)
    {
    case TimeoutKind::HalfFrame:
        return "半包超时";
    case TimeoutKind::Idle:
        return "空闲超时";
    case TimeoutKind::Write:
        return "写超时";
    }
    return "未知超时";
}

std::int64_t TimerQueue::nowMs()
{
    // steady_clock：系统时间被改（NTP 校时、手工调钟）时不会倒退。
    // 用 system_clock 的话，时钟往回跳一次，挂在上面的超时就会推迟相应的时长。
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void TimerQueue::arm(std::uint64_t connectionId, TimeoutKind kind, int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        // 0 的语义是「这一类超时关闭」，所以 arm(…, 0) 与 disarm 等价。
        // 让调用方可以无脑调用而不用先判断开关，少一处「忘了判断」的机会。
        disarm(connectionId, kind);
        return;
    }

    const Key key{connectionId, kind};
    const std::int64_t deadline = nowMs() + static_cast<std::int64_t>(timeoutMs);

    const auto found = _live.find(key);
    if (found != _live.end())
    {
        // 刷新：活表换新时刻，旧堆条目原地变成陈旧条目（弹出时会被丢掉）。
        if (found->second == deadline)
        {
            return; // 同一毫秒内重复挂，没必要多压一条
        }
        found->second = deadline;
    }
    else
    {
        _live.emplace(key, deadline);
    }

    _heap.push_back(Entry{deadline, connectionId, kind});
    std::push_heap(_heap.begin(), _heap.end(), LaterFirst{});
    compactIfNeeded();
}

bool TimerQueue::disarm(std::uint64_t connectionId, TimeoutKind kind)
{
    // 只擦活表。堆里那条记录留着，等它被弹出来时自然会被识别成陈旧条目。
    return _live.erase(Key{connectionId, kind}) > 0;
}

void TimerQueue::armIfAbsent(std::uint64_t connectionId, TimeoutKind kind, int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        disarm(connectionId, kind);
        return;
    }
    if (_live.find(Key{connectionId, kind}) != _live.end())
    {
        return; // 已经挂着：保持原来那个绝对期限
    }
    arm(connectionId, kind, timeoutMs);
}

int TimerQueue::remainingMs(std::uint64_t connectionId, TimeoutKind kind) const
{
    const auto found = _live.find(Key{connectionId, kind});
    if (found == _live.end())
    {
        return -1;
    }
    const std::int64_t remaining = found->second - nowMs();
    return remaining > 0 ? static_cast<int>(remaining) : 0;
}

void TimerQueue::disarmAll(std::uint64_t connectionId)
{
    // 三个类别固定，直接逐个擦比遍历整张表便宜（连接数上千时差别明显）
    for (std::size_t index = 0; index < kTimeoutKindCount; ++index)
    {
        _live.erase(Key{connectionId, static_cast<TimeoutKind>(index)});
    }
}

int TimerQueue::nearestTimeoutMs(int capMs) const
{
    if (capMs < 0)
    {
        capMs = 0;
    }
    if (_heap.empty())
    {
        return capMs;
    }

    const std::int64_t remaining = _heap.front().deadlineMs - nowMs();
    if (remaining <= 0)
    {
        // 已经到期了：让 wait 立刻返回，由 collectExpired 处置。
        // 这里不能返回 capMs——那会白睡一个上限时长，超时精度直接崩掉。
        return 0;
    }
    if (remaining > capMs)
    {
        return capMs;
    }
    return static_cast<int>(remaining);
}

std::size_t TimerQueue::collectExpired(std::vector<ExpiredTimer> &out)
{
    const std::int64_t now = nowMs();
    std::size_t collected = 0;

    while (!_heap.empty() && _heap.front().deadlineMs <= now)
    {
        std::pop_heap(_heap.begin(), _heap.end(), LaterFirst{});
        const Entry entry = _heap.back();
        _heap.pop_back();

        // 校验两件事：活表里还有这个 key，且到期时刻一致。
        // 只看 key 会把「刷新前的那一条」误判成到期（见头文件第 2 条）。
        const auto found = _live.find(Key{entry.connectionId, entry.kind});
        if (found == _live.end() || found->second != entry.deadlineMs)
        {
            continue; // 陈旧条目（已取消，或已被刷新）
        }

        _live.erase(found);
        out.push_back(ExpiredTimer{entry.connectionId, entry.kind});
        ++collected;
    }

    return collected;
}

void TimerQueue::compactIfNeeded()
{
    if (_heap.size() <= _live.size() * kCompactFactor + kCompactSlack)
    {
        return;
    }

    // 活表是唯一的真相，堆只是它的一个「按到期时刻排序的视图」，所以重建是安全的。
    _heap.clear();
    _heap.reserve(_live.size());
    for (const auto &entry : _live)
    {
        _heap.push_back(Entry{entry.second, entry.first.connectionId, entry.first.kind});
    }
    std::make_heap(_heap.begin(), _heap.end(), LaterFirst{});
}
