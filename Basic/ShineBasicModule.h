#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H

#include <string>
#include <unordered_map>

/**
 * @brief 所有需要自检能力的模块基类。
 */
class ShineBasicModule
{
public:
    ShineBasicModule();
    virtual ~ShineBasicModule();

    ShineBasicModule(const ShineBasicModule&) = delete;
    ShineBasicModule& operator=(const ShineBasicModule&) = delete;

    virtual std::string moduleName() const = 0;

protected:
    /**
     * @brief 添加一个自检阶段。
     * @param stageName 阶段名称，同时作为 _stage 的 key。
     * @param description 阶段说明，用于描述这个阶段负责检查什么。
     */
    void addStage(const std::string& stageName, const std::string& description);

    /**
     * @brief 写入一个自检阶段的状态，并自动记录日志。
     * @param stageName 阶段名称，必须先通过 addStage() 注册。
     * @param status 当前阶段是否正常；true 写入通过日志，false 写入错误日志。
     * @param message 状态说明；可用于写入 try/catch 捕获到的异常信息。
     */
    void setStageStatus(const std::string& stageName, bool status, const std::string& message);

private:
    /**
     * @brief 单个自检阶段的状态信息。
     */
    struct Stage
    {
        std::string name;        /** 阶段名称 */
        std::string description; /** 阶段说明 */
        bool status = true;      /** 当前阶段是否正常 */
        std::string error;       /** 当前阶段的错误信息 */
    };

    std::unordered_map<std::string, Stage> _stage; /** 模块的自检阶段表 */
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_BASIC_MODULE_H
