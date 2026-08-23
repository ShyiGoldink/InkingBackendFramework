#ifndef INKING_BACKEND_FRAMEWORK_DATA_STRUCT_COMMAND_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_DATA_STRUCT_COMMAND_STRUCT_H

#include <string>
#include <vector>
#include <functional>
/**
 * @brief 命令执行结果。
 */
enum class CommandResult
{
    Continue,
    Quit
};

/**
 * @brief 单个控制台命令。
 */
struct Command
{
    std::string name;                                                      /** 命令主名称 */
    std::vector<std::string> aliases;                                      /** 命令别名 */
    std::string description;                                               /** help 中显示的说明 */
    std::function<CommandResult(const std::vector<std::string> &)> action; /** 命令执行函数 */
    bool hidden = false;                                                   /** 是否从 help 和模糊提示中隐藏 */
};

#endif // INKING_BACKEND_FRAMEWORK_DATA_STRUCT_COMMAND_STRUCT_H
