#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H

#include <mutex>
#include <source_location>
#include <string>
#include <vector>
#include <optional>
#include "StatusRegisterToken.h"
#include "dataStruct/StageStrcut.h"

/**
 * @brief 自检阶段的落盘策略。
 *
 * 自检本身照旧执行（状态表照旧更新、照旧能在 status 里看到），这里只决定
 * "这一次更新要不要写进日志"。分成三档，是为了让心跳和故障不再挤在同一个层级：
 *
 * - OnChange：默认。首次登记、状态/消息变化、任意失败都会记一条；相同状态重复只计数。
 * - Always  ：每次都记。用于审计价值高的路径，例如每一条 SQL。
 * - Silent  ：成功流水一律不记，只计数。用于心跳/轮询；**失败仍然会记 Error**，
 *             因为"心跳停了"恰恰是最需要留下证据的时刻。
 */
enum class StageLogPolicy
{
    OnChange,
    Always,
    Silent
};

/**
 * @brief 所有需要自检能力的模块基类。
 * 这个类将会是所有“非纯数据类”的基类。
 * 它将日志系统包含，并且期望在子类实现中，对于每个可能会出现的函数结尾都进行自检
 * 并且提供一个公开的方法用于获取全部的状态
 * 由于每次调用某个方法的结尾都需要处理自检状态，所以理论上来说，状态的更新是非常及时的
 */
class ShineBasicModule
{
public:
    ShineBasicModule();
    virtual ~ShineBasicModule();
    // 禁止拷贝构造和赋值操作
    ShineBasicModule(const ShineBasicModule &) = delete;
    ShineBasicModule &operator=(const ShineBasicModule &) = delete;

    /** 获取模块名称，用于日志输出和状态检查器注册。 */
    virtual std::string moduleName() const = 0;

    /**给外部提供一个接口用于获取到stage数据 */
    std::vector<Stage> getStage() const;

    /**为命令提供一个快捷的方法，用于快速输出对象信息 */
    void sayMyName() const;

protected:
    /**
     * @brief 写入一个自检阶段的状态，并按策略决定是否记录日志。
     *
     * 如果 step 还不存在，会自动注册一个新阶段（并记一条"已登记"）。
     * 如果 step 已存在但 name 不一致，会写入错误日志并拒绝更新，避免阶段编号冲突。
     *
     * 无论是否写日志，阶段表里的状态、执行次数、首次/最近时间都会更新，
     * 所以 `status` 命令看到的信息不会因为日志降噪而减少。
     *
     * @param step 阶段编号，用于稳定定位阶段。
     * @param name 阶段名称，用于展示和日志输出。
     * @param statu 当前阶段是否正常；false 一定会上报（即使策略是 Silent）。
     * @param message 状态说明；可用于写入 try/catch 捕获到的异常信息。
     * @param policy 落盘策略，默认只在状态变化时记录。
     * @param site 自动捕获的调用位置；日志里的"文件:行"指向业务代码，
     *             而不是一律指向 ShineBasicModule.cpp 这个转发点。
     */
    void setStageStatus(int step, const std::string &name, bool statu, const std::string &message,
                        StageLogPolicy policy = StageLogPolicy::OnChange,
                        const std::source_location &site = std::source_location::current());

    /**
     * @brief 补充一个自检阶段的说明和错误建议。
     *
     * 该方法不会写入日志，只负责补充展示信息。
     * 如果 step 尚不存在，会先创建一个没有名称的阶段，后续 setStageStatus() 会补全名称。
     *
     * @param step 阶段编号。
     * @param description 阶段说明，用于描述这个阶段负责检查什么。
     * @param suggestion 错误建议；阶段失败时可以展示给管理员。
     */
    void setStageDetail(int step, const std::string &description, const std::string &suggestion);
    /**
     * @brief 用于向Status Checker中注册指针，方便后续的UIManager获取到最新的stage数据
     *
     * 该方法应该在子类的构造函数的末尾进行调用，以表示注册完成
     *
     */
    void registerToStatusChecker()
    {
        _statusRegisterToken.emplace(moduleName(), this);
    };

private:
    /** 调用方必须已经持有 _stageMutex */
    Stage *findStage(int step);
    const Stage *findStage(int step) const;

    /**
     * 保护 _stage 的锁。
     * 这个基类的设计是"每个函数结尾都写自检状态"，而模块本来就会被多个线程调用
     * （线程池里每个管家线程都会写自己的状态），没有这把锁的话，
     * _stage 在扩容时会被两个线程同时移动，直接造成堆损坏。
     */
    mutable std::mutex _stageMutex;
    std::vector<Stage> _stage;                               /** 模块的自检阶段表，按 step 升序保存 */
    std::optional<StatusRegisterToken> _statusRegisterToken; /**延迟构造Token */
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H
