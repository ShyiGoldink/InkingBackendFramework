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
