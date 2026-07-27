#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::string name;                              /** 命令主名称 */
    std::vector<std::string> aliases;              /** 命令别名 */
    std::string description;                       /** help 中显示的说明 */
    std::function<CommandResult()> action;         /** 命令执行函数 */
    bool hidden = false;                           /** 是否从 help 和模糊提示中隐藏 */
};

/**
 * @brief 注册式命令中心。
 *
 * 支持命令名、别名、帮助信息和未知命令的模糊提示。
 */
class CommandCenter
{
public:
    /**
     * @brief 注册一个命令。
     * @param command 命令定义。
     */
    void registerCommand(Command command);

    /**
     * @brief 执行一行用户输入。
     * @param input 用户输入的完整文本。
     * @return 命令执行结果。
     */
    CommandResult execute(const std::string& input) const;

    /**
     * @brief 打印命令帮助。
     */
    void printHelp() const;

private:
    const Command* findCommand(const std::string& commandName) const;
    std::vector<std::string> commandWords() const;
    static std::string firstWord(const std::string& input);

    std::vector<Command> _commands;
    std::unordered_map<std::string, std::size_t> _index;
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_COMMAND_CENTER_H
