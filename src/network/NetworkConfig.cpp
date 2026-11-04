#include "network/NetworkConfig.h"

#include "tool/JsonTool.h"
#include "tool/PathFindTool.h"

#include <algorithm>

namespace
{
    /**
     * 取值边界。上下限的意义是「不让一个手滑的数字变成灾难」：
     * 0 个分片等于网络服务压根没起来，而 1000 个分片会在启动阶段把线程数打爆。
     */
    constexpr std::size_t kMinShardCount = 1;
    constexpr std::size_t kMaxShardCount = 64;
    constexpr std::size_t kMinConnectionsPerShard = 1;
    constexpr std::size_t kMaxConnectionsPerShard = 100000;
    constexpr int kMinPort = 1;
    constexpr int kMaxPort = 65535;
}

std::string NetworkConfig::describe() const
{
    return "端口 " + std::to_string(port) +
           "，分片 " + std::to_string(shardCount) +
           "，每分片连接上限 " + std::to_string(maxConnectionsPerShard);
}

NetworkConfig loadNetworkConfig(std::string &note)
{
    NetworkConfig config;

    PathFindTool pathFindTool;
    const std::string filePath = pathFindTool.configPath("networkConfig.json").string();
    JsonTool jsonTool;
    const auto root = jsonTool.loadFromFile(filePath);
    if (!root.has_value())
    {
        note = "读取 " + filePath + " 失败，全部使用默认值";
        return config;
    }

    const auto node = jsonTool.getValue<JsonTool::Json>(*root, "networkConfig");
    if (!node.has_value())
    {
        note = "配置缺少 networkConfig 节点，全部使用默认值";
        return config;
    }

    const auto port = jsonTool.getValue<int>(*node, "port");
    const auto shardCount = jsonTool.getValue<int>(*node, "shardCount");
    const auto maxConnections = jsonTool.getValue<int>(*node, "maxConnectionsPerShard");

    if (port.has_value())
    {
        config.port = std::clamp(*port, kMinPort, kMaxPort);
    }
    if (shardCount.has_value())
    {
        // 配置里用 int 表达，负数和 0 都归到下限；这里不报错，只夹紧
        const auto value = static_cast<long long>(*shardCount);
        config.shardCount = static_cast<std::size_t>(
            std::clamp<long long>(value, static_cast<long long>(kMinShardCount),
                                  static_cast<long long>(kMaxShardCount)));
    }
    if (maxConnections.has_value())
    {
        const auto value = static_cast<long long>(*maxConnections);
        config.maxConnectionsPerShard = static_cast<std::size_t>(
            std::clamp<long long>(value, static_cast<long long>(kMinConnectionsPerShard),
                                  static_cast<long long>(kMaxConnectionsPerShard)));
    }

    note = "已从 " + filePath + " 读取：" + config.describe();
    return config;
}
