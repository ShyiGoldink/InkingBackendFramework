#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H

#include <unordered_map>
#include "dataStruct/CommandStruct.h"
#include "CommandRegistrant.h"
#include "CommandExecutor.h"
/**
 * @brief 注册式命令中心。
 *
 * 支持命令名、别名、帮助信息和未知命令的模糊提示。
 */
class CommandCenter
{
public:
    CommandCenter();
    ~CommandCenter() = default;
    /**
     * @brief 注册命令的接口。
     * @param command 命令定义。
     */
    void registerCommand(Command command);

    /**
     * @brief 执行一行用户输入的接口。
     * @param input 用户输入的完整文本。
     * @return 命令执行结果。
     */
    CommandResult execute(const std::string &input) const;

    /**
     * @brief 打印命令帮助。
     */
    void printHelp() const;

private:
    CommandRegistrant _commandRegistrant; /**数据和注册中心 */
    CommandExecutor _commandExecutor;     /**指令的实际执行中心 */
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H
