#ifndef INKING_BACKEND_FRAMEWORK_NETWORK_CONFIG_H
#define INKING_BACKEND_FRAMEWORK_NETWORK_CONFIG_H

/**
 * @file NetworkConfig.h
 * @brief 网络层可调参数。
 *
 * 为什么分片数要进配置而不是写成常量：分片数应该由「物理核数 - 业务线程数」推出来，
 * 而这个差在不同机器上不一样。写死一个数字，换台机器就变成超卖或者吃不饱，
 * 而且没有任何地方能看出「当时是按几核配的」。
 */

#include <cstddef>
#include <string>

/**
 * @brief 三类超时的时长（毫秒）。0 表示「这一类超时关闭」。
 *
 * 为什么要有 0 这个取值：三类超时的默认状态不应该一样。
 * - 半包超时、写超时是「故障兜底」，不设就等于把资源永久借给一个坏客户端，默认开。
 * - 空闲超时是「业务约定」，取决于客户端会不会主动保活。默认开等于替使用者做了
 *   一个他可能不认的决定：任何长连接客户端都会在每个周期被掐一次。
 *
 * 所以空闲超时默认 **0（关闭）**，要开就在配置里写明确的值。
 */
struct NetworkTimeouts
{
    int halfFrameMs = 10000;
    int idleMs = 0;
    int writeMs = 30000;

    std::string describe() const;
};

struct NetworkConfig
{
    int port = 9000;

    /**
     * 分片数。每个分片 = 一条线程 + 一个事件集合。
     *
     * 取值参考（设计文档 M4）：分片线程数 + 业务线程数（当前 10 条）不超过物理核数 × 1.5。
     * 8 核机器上 4 个分片是安全值，再往上就要开始抢核。
     */
    std::size_t shardCount = 4;

    /**
     * 单个分片的连接上限。
     *
     * 超过上限的新连接会被直接关掉，而不是「先接下来再说」：一个已经满负荷的分片
     * 再吃新连接，只会让它管的全部连接一起变慢，那是最糟的过载处理方式。
     */
    std::size_t maxConnectionsPerShard = 1000;

    /** 三类超时（M8）。半包/写默认开，空闲默认关 */
    NetworkTimeouts timeouts;

    /**
     * @brief 优雅停机的排空上限（毫秒）。
     *
     * 停机时先停止读新请求，再等「在飞请求的回包 + 写缓冲」收干净；
     * 超过这个时间就硬关，不再等。**必须有一个上限**：一个只发不读的客户端
     * 可以让写缓冲永远排不空，没有上限的话停机就再也停不下来。
     *
     * 这个值同时决定「进程退出最慢要多久」，所以它要明显小于调用方的退出等待
     * （Application 那边给了 15 秒的安全网）。
     */
    int shutdownGraceMs = 5000;

    /**
     * @brief 是否开启分段计时（M10）。
     *
     * 默认关。开着时每请求多 5 次单调时钟读（约 100~150 ns），换来「时间花在哪一段」
     * 的直接答案。排查性能问题时打开，压测报告里也用它；常驻服务不需要。
     * 运行期可以用 `network-stat` 看累计值。
     */
    bool profileSegments = false;

    /**
     * @brief 是否注册基准用的指令（`bench-sleep`）。
     *
     * 默认关，因为它是**故意占用业务线程**的指令（忙等 N 微秒），放进生产就是把
     * 一个「自伤开关」暴露给所有客户端。只有在跑 M10 的基准时才在配置里打开。
     */
    bool enableBenchCommands = false;

    /** 给日志和自检用的一行摘要 */
    std::string describe() const;
};

/**
 * @brief 读取 config/networkConfig.json。
 *
 * 读不到、格式不对、字段缺失都不算致命错误：缺失的字段用结构体里的默认值，
 * 并把「用了默认值」的原因写进 note 交给调用方记日志。
 * 网络层是能让进程跑起来的东西，不该因为一个配置文件写错就拒绝启动。
 */
NetworkConfig loadNetworkConfig(std::string &note);

#endif // INKING_BACKEND_FRAMEWORK_NETWORK_CONFIG_H
