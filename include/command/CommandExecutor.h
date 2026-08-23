#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_EXECUTOR_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_EXECUTOR_H

#include "dataStruct/CommandStruct.h"

class CommandRegistrant;
class CommandExecutor
{
public:
    CommandExecutor(CommandRegistrant &commandRegistrant);
    ~CommandExecutor() = default;
    /**
     * @brief 执行一行用户输入。
     * @param input 用户输入的完整文本。
     * @return 命令执行结果。
     */
    CommandResult execute(const std::string &input) const;

private:
    CommandRegistrant *_commandRegistrant = nullptr; /**从commandRegistrant处获数据 */
                                                     /**辅助方法-指令token化*/
    std::vector<std::string> tokenize(const std::string &input) const;
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_EXECUTOR_H
