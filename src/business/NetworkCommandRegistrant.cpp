#include "business/NetworkCommandRegistrant.h"

#include "basic/ShineLog.h"
#include "network/CommandRegistry.h"
#include "network/NetworkStage.h"

#include <framework/Ping.pb.h>

#include <chrono>
#include <cstdint>
#include <thread>

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

namespace
{
    /**
     * @brief 基准专用指令 `bench-sleep`：按 payload 前 4 字节（小端）忙等对应微秒数。
     *
     * 为什么是**忙等**而不是 `sleep_for`：Windows 上 `sleep_for` 的粒度取决于计时器
     * 分辨率，默认 15.6ms，要 100µs 会睡成 15ms——那样量到的是操作系统的调度粒度，
     * 不是框架开销。忙等会占满一个核，但基准脚本本来就要求「业务耗时可控且精确」。
     *
     * 上限 1 秒：防止一条请求把业务线程钉死（这是压测工具，不是后门）。
     */
    bool handleBenchSleep(const CommandFrame &request, std::string &responsePayload)
    {
        if (request.payload.size() < 4)
        {
            return false;
        }
        const auto micros = static_cast<std::uint32_t>(
            static_cast<unsigned char>(request.payload[0]) |
            (static_cast<unsigned char>(request.payload[1]) << 8) |
            (static_cast<unsigned char>(request.payload[2]) << 16) |
            (static_cast<unsigned char>(request.payload[3]) << 24));
        if (micros > 1000000u)
        {
            return false;
        }

        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
        while (std::chrono::steady_clock::now() < until)
        {
            // 空转：这一段的长度就是「业务耗时」这个自变量
        }

        responsePayload = request.payload;
        return true;
    }
}

void registerBuiltinNetworkCommands(bool enableBench)
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
    if (enableBench)
    {
        registry.registerCommand("bench-sleep", handleBenchSleep);
    }

    // 注册完之后冻结：之后任何注册都会失败并记 Error，而不是悄悄生效。
    // 分片可能已经在并发查表，往表里插元素会让读端看到不一致的结构。
    registry.freeze();

    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kShard,
                  "已注册 " + std::to_string(registry.size()) + " 条线上指令：" +
                      registry.describe());
}
