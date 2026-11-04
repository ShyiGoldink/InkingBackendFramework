#ifndef INKING_BACKEND_FRAMEWORK_NETWORK_COMMAND_REGISTRANT_H
#define INKING_BACKEND_FRAMEWORK_NETWORK_COMMAND_REGISTRANT_H

/**
 * @file NetworkCommandRegistrant.h
 * @brief 线上指令的业务注册入口——**业务只在这一处往上长**。
 *
 * 分工是这样切的：
 *
 * - 网络层（`network/`）：收发、拆帧、一条在飞、跨线程回投。**不认识任何业务**。
 * - 注册表（`network/CommandRegistry.h`）：指令名 → 处理函数，只做查表。
 * - 本文件（`business/`）：每一条指令的业务实现，一个指令名一个 handler。
 *
 * 加一条新指令的完整步骤：
 * 1. 在 `proto/` 里加一个 request/response message；
 * 2. 在 `registerBuiltinNetworkCommands()` 里加一处 `registerCommand`；
 * 3. 完。网络层、分片、分片数、连接管理一行都不用改。
 *
 * 目前的实现是**框架样例**：`echo` 用于端到端验收与压测（它不解析 payload，
 * 原样回，是 M10 做纯 echo 基准档要用的那条指令）；`ping` 是 protobuf 的完整样例
 * （解析请求 → 填响应 → 序列化），照它写新指令即可。
 *
 * TODO（业务表）：按「要实现」的指令表逐条注册业务。每条都需要：
 *   - 入参解析失败时返回 false（框架会回一个空 payload 的同名帧，不断连接）；
 *   - 不做任何阻塞 IO（这里跑在业务线程池上，一条慢 SQL 会占住一个业务线程）。
 */

/**
 * @brief 注册全部线上指令。必须在分片启动之前调用一次（Net::run 负责）。
 *
 * @param enableBench 是否注册基准专用指令（`bench-sleep`）。
 *        它会在业务线程上**忙等**指定微秒数，属于故意自伤，只在压测配置里打开。
 */
void registerBuiltinNetworkCommands(bool enableBench);

#endif // INKING_BACKEND_FRAMEWORK_NETWORK_COMMAND_REGISTRANT_H
