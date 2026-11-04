#ifndef INKING_BACKEND_FRAMEWORK_TIMER_QUEUE_H
#define INKING_BACKEND_FRAMEWORK_TIMER_QUEUE_H

/**
 * @file TimerQueue.h
 * @brief 分片内的定时器堆（设计文档 M8 / 台阶 C5）。
 *
 * 为什么不是时间轮：时间轮的价值是「大量定时器 + O(1) 插入」，代价是槽位精度固定、
 * 内存按槽位数预分配。分片里同时存在的定时器数量上限是「连接数 × 3」，量级在千位，
 * 一个最小堆（O(log n) 插入、O(1) 取最近到期）已经完全够用，而且精度按毫秒算，
 * 不用为了「槽位多大」这种问题做取舍。
 *
 * ## 三个必须写下来的决定
 *
 * 1. **取消是懒删除**。堆里不按位置删除（需要 decrease-key，代码量与出错面都翻倍），
 *    而是维护一张 `(连接标识, 超时类别) → 到期时刻` 的活表：取消只擦表，堆里的条目
 *    留着，弹出时对不上活表就丢弃。这样「取消」是 O(1)。
 *
 * 2. **活表同时兼任校验依据**。条目弹出时，活表里若还有同一个 key 且到期时刻相同，
 *    才算真的到期。只看 key 不看时刻是不够的：连接重新挂了同一个超时（刷新），
 *    旧条目会被误判成到期——那会把一条刚活跃过的连接直接掐掉。
 *
 * 3. **堆会自己压紧**。刷新超时会不断产生陈旧条目（每次 arm 都是一次新条目），
 *    长期连接能把堆撑到与实际定时器数量无关的规模。堆里条目数超过「活条目数 × 2 + 64」
 *    时整体重建一次，摊还下来是 O(log n)。
 *
 * 线程归属：**只在分片线程里用**，没有任何锁。和连接表一样，跨线程来碰它是错的。
 */

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * @brief 三类超时（设计文档 M8）。
 *
 * 之所以要分成三类而不是一个「连接超时」，是因为它们各自回答不同的问题：
 * 半包超时问「这条流是不是卡在半个帧上」，空闲超时问「这条连接还有人用吗」，
 * 写超时问「这个对端是不是只发不读」。混成一个的话，任何一种故障都无法单独定位。
 */
enum class TimeoutKind : std::uint8_t
{
    HalfFrame = 0, /** 收到一部分帧之后再没动静 */
    Idle = 1,      /** 整条连接长时间没有完整交互 */
    Write = 2,     /** 写缓冲长时间没发完 */
};

/** `TimeoutKind` 的个数。数组长度用它，新增类别时只改这里 + 枚举 */
inline constexpr std::size_t kTimeoutKindCount = 3;

/** 日志与自检里显示的名字 */
const char *timeoutKindText(TimeoutKind kind);

/** 一条已到期的定时器。`connectionId` 是分片分配的连接标识（自增、不复用） */
struct ExpiredTimer
{
    std::uint64_t connectionId = 0;
    TimeoutKind kind = TimeoutKind::HalfFrame;
};

class TimerQueue
{
public:
    /**
     * @brief 挂一个定时器：从现在起 `timeoutMs` 毫秒后到期。
     *
     * 同一个 `(connectionId, kind)` 重复挂会**覆盖**旧到期时刻（这是刷新空闲超时的
     * 常规用法），不会产生两个定时器。
     *
     * `timeoutMs <= 0` 视为「关闭这一类超时」，等同于取消。
     */
    void arm(std::uint64_t connectionId, TimeoutKind kind, int timeoutMs);

    /**
     * @brief 只在「还没挂」时挂上；已经挂着就**保持原到期时刻**。
     *
     * 与 `arm` 的区别就是刷新与否，而这个区别决定了两类完全不同的语义：
     * - 空闲/写超时用 `arm`：对端每有进展就顺延，抓的是「彻底不动了」；
     * - 半包超时用 `armIfAbsent`：从**第一个残字节**开始算绝对期限，
     *   抓的是「一直在挤、但永远挤不完」——也就是慢速攻击。若用 `arm`，
     *   对端每秒发 1 个字节就能让这条连接无限期活下去。
     */
    void armIfAbsent(std::uint64_t connectionId, TimeoutKind kind, int timeoutMs);

    /** 距离到期还有多少毫秒；没挂过返回 -1 */
    int remainingMs(std::uint64_t connectionId, TimeoutKind kind) const;

    /** 取消一个定时器。没挂过也合法（返回静默的 false，不报错） */
    bool disarm(std::uint64_t connectionId, TimeoutKind kind);

    /** 取消某条连接的全部定时器。连接关闭时调用，避免陈旧条目越积越多 */
    void disarmAll(std::uint64_t connectionId);

    /**
     * @brief 距离最近一个到期时刻还有多少毫秒，并夹到 `[0, capMs]`。
     *
     * 这个值直接作为 `Poller::wait` 的超时参数，所以它有两个职责：
     * - 有定时器时，返回真实剩余时间（分片不会睡过头，超时精度≈1ms）；
     * - 没有定时器时返回 `capMs`，保证分片最多睡 `capMs` 就能醒一次查退出标志。
     *
     * 堆顶若是陈旧条目（更早的到期时刻），返回值会偏小——只多醒一次，不影响正确性。
     */
    int nearestTimeoutMs(int capMs) const;

    /**
     * @brief 取出全部已到期的定时器，追加进 `out`（不覆盖 out 原有内容）。
     *
     * @return 本次取出的条数。
     *
     * 条目在这里就被移出队列了，所以调用方**必须处理每一个**：
     * 要么关掉对应连接，要么明确决定放它一马（放它一马就该重新 arm）。
     */
    std::size_t collectExpired(std::vector<ExpiredTimer> &out);

    /** 活定时器数（不含堆里的陈旧条目）。用于日志与自检 */
    std::size_t liveCount() const
    {
        return _live.size();
    }

    /** 堆里条目数（含陈旧）。压紧是否及时，看这两个数的差 */
    std::size_t heapSize() const
    {
        return _heap.size();
    }

    /** 单调时钟的毫秒数。只用来算相对时间，绝对值没有意义 */
    static std::int64_t nowMs();

private:
    struct Entry
    {
        std::int64_t deadlineMs = 0;
        std::uint64_t connectionId = 0;
        TimeoutKind kind = TimeoutKind::HalfFrame;
    };

    /** `std::push_heap` 默认建最大堆，这里取反成最小堆（最近到期在堆顶） */
    struct LaterFirst
    {
        bool operator()(const Entry &left, const Entry &right) const
        {
            return left.deadlineMs > right.deadlineMs;
        }
    };

    struct Key
    {
        std::uint64_t connectionId = 0;
        TimeoutKind kind = TimeoutKind::HalfFrame;

        bool operator==(const Key &other) const
        {
            return connectionId == other.connectionId && kind == other.kind;
        }
    };

    struct KeyHash
    {
        std::size_t operator()(const Key &key) const
        {
            // 连接标识低位与类别做一次混合即可：同一个连接的三个类别只差 0/1/2，
            // 直接拼在低位会让它们落在相邻桶里，混一下就散开了。
            const std::uint64_t mixed = key.connectionId * 0x9E3779B97F4A7C15ull;
            return static_cast<std::size_t>(mixed ^ (static_cast<std::uint64_t>(key.kind) * 0xC2B2AE3D27D4EB4Full));
        }
    };

    /** 堆里的陈旧条目过多时整体重建，防止长期连接把堆无限撑大 */
    void compactIfNeeded();

    std::vector<Entry> _heap;
    std::unordered_map<Key, std::int64_t, KeyHash> _live;
};

#endif // INKING_BACKEND_FRAMEWORK_TIMER_QUEUE_H
