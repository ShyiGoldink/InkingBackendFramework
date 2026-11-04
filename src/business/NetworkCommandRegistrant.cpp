#include "business/NetworkCommandRegistrant.h"

#include "basic/ShineLog.h"
#include "network/CommandRegistry.h"
#include "network/NetworkStage.h"

#include <framework/Ping.pb.h>

namespace
{
    /**
     * @brief 内置调试指令 `echo`：payload 原样返回，不解释内容。
     *
     * 它不参与业务表，存在的理由有两个：
     * 1. 端到端验收（`tools/netbench/framed_client.py`）需要一个「一定能成功」的指令，
     *    否则每个测试脚本都要先造一份合法的业务数据；
     * 2. 设计文档 M10 的基准里有一档是「纯 echo、业务耗时 0µs」，用来把框架开销
     *    单独量出来。少了这条指令，那一档没法跑。
     */
    bool handleEcho(const CommandFrame &request, std::string &responsePayload)
    {
        responsePayload = request.payload;
        return true;
    }

    /**
     * @brief 指令 `ping`：Ping → Pong，protobuf 的完整样例。
     *
     * 这就是「按业务表加一条指令」的标准形状：解析自己的请求类型、填自己的响应类型、
     * 序列化出去。类型是编译期确定的（不是 DynamicMessage），所以没有反射开销。
     */
    bool handlePing(const CommandFrame &request, std::string &responsePayload)
    {
        inking::framework::Ping ping;
        if (!ping.ParseFromArray(request.payload.data(),
                                 static_cast<int>(request.payload.size())))
        {
            // 返回 false：框架会回一个空 payload 的同名帧，并把这次失败计数。
            // 不断连接——一次业务数据不合法不是协议错误，客户端可能只是版本旧。
            return false;
        }

        inking::framework::Pong pong;
        pong.set_timestampms(ping.timestampms());
        return pong.SerializeToString(&responsePayload);
    }
}

void registerBuiltinNetworkCommands()
{
    CommandRegistry &registry = CommandRegistry::instance();

    // 幂等：网络服务可以被停掉再拉起（控制台 network-run），注册表却是进程级单例，
    // 已经注册并冻结过就直接返回——否则第二次启动会刷出一串「重复注册」的 Error。
    if (registry.isFrozen())
    {
        return;
    }

    registry.registerCommand("echo", handleEcho);
    registry.registerCommand("ping", handlePing);

    // 注册完之后冻结：之后任何注册都会失败并记 Error，而不是悄悄生效。
    // 分片可能已经在并发查表，往表里插元素会让读端看到不一致的结构。
    registry.freeze();

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                  "已注册 " + std::to_string(registry.size()) + " 条线上指令：" +
                      registry.describe());
}
