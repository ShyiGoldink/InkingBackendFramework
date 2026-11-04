#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRY_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRY_H

/**
 * @file CommandRegistry.h
 * @brief 线上指令注册表：指令名 → 处理函数（设计文档 M7）。
 *
 * 与 `command/CommandRegistrant` 不是一回事，别混：
 * - `CommandRegistrant` 管的是**控制台命令**（人在终端里敲的 help / status / log）；
 * - 本类管的是**线上协议指令**（客户端发来的一帧里的 command 字段）。
 *
 * 三条设计约束（都来自 M7，改之前先看那节的理由）：
 *
 * 1. **注册发生在启动期，之后表只读。** 所以热路径查表不需要锁——一张不再变化的
 *    `unordered_map` 在并发读下是安全的。`freeze()` 之后任何注册都会失败并报错，
 *    而不是「悄悄生效、偶发生效」。
 * 2. **不用反射 / DynamicMessage。** 处理函数里拿到的已经是解析好的具体类型
 *    （由生成的 message 类做 `ParseFromArray`），查表 + 解析的开销压在小消息 1µs 以内。
 * 3. **本层不认识连接、不认识 socket。** 入参是「一条完整消息」，出参是「响应字节」。
 *    连接归分片独占，业务线程绝不碰。
 *
 * 业务怎么接进来（一份可照抄的样例见 `business/NetworkCommandRegistrant.cpp`）：
 *
 * ```cpp
 * CommandRegistry::instance().registerCommand("ping",
 *     [](const CommandFrame &request, std::string &responsePayload) -> bool
 *     {
 *         inking::framework::Ping ping;
 *         if (!ping.ParseFromArray(request.payload.data(),
 *                                  static_cast<int>(request.payload.size())))
 *         {
 *             return false;                     // 报错：入参解析失败
 *         }
 *         inking::framework::Pong pong;
 *         pong.set_timestampms(ping.timestampms());
 *         return pong.SerializeToString(&responsePayload);
 *     });
 * ```
 *
 * 一句话：**加一条新指令 = 一个 `.proto` message + 这里一处注册**，网络层不用改。
 */

#include "protocol/FrameCodec.h"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

class CommandRegistry
{
public:
    /**
     * @brief 一条指令的处理函数。
     *
     * @param request 请求帧（command 已由框架匹配，payload 是业务自己的 protobuf 字节）
     * @param responsePayload 出参：写响应消息序列化后的字节
     * @return false 表示处理失败。框架会用同一指令名回一个空 payload 的帧，
     *         并把这次失败计入统计——**不因此断开连接**（一次业务失败不是协议错误）。
     */
    using Handler = std::function<bool(const CommandFrame &request, std::string &responsePayload)>;

    static CommandRegistry &instance();

    CommandRegistry(const CommandRegistry &) = delete;
    CommandRegistry &operator=(const CommandRegistry &) = delete;
    CommandRegistry(CommandRegistry &&) = delete;
    CommandRegistry &operator=(CommandRegistry &&) = delete;

    /**
     * @brief 注册一条指令。
     *
     * 拒绝四种情况并返回 false：空指令名、空处理函数、重名、表已冻结。
     * 拒绝而不是覆盖，是因为「两条业务抢同一个指令名」必须当场暴露，
     * 让它悄悄覆盖的话，表现为「某个功能时好时坏」。
     */
    bool registerCommand(const std::string &command, Handler handler);

    /** 冻结注册表。启动期注册完成后调用一次；之后 `find` 可以无锁并发 */
    void freeze();

    bool isFrozen() const
    {
        return _frozen;
    }

    /**
     * @brief 热路径查表。找不到返回 nullptr。
     *
     * 返回的是表内元素的地址，表冻结后长期有效，所以调用方可以持有它到本次处理结束。
     */
    const Handler *find(const std::string &command) const;

    std::size_t size() const
    {
        return _handlers.size();
    }

    /** 已注册的指令名清单（排序后），写启动日志用 */
    std::string describe() const;

private:
    CommandRegistry() = default;

    std::unordered_map<std::string, Handler> _handlers;
    bool _frozen = false;
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRY_H
