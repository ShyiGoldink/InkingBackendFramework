#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRANT_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRANT_H

#include "dataStruct/CommandStruct.h"
#include "command/CommandLibrary.h"
#include <unordered_map>

/**
 * @brief
 * 指令模块里用于注册指令的小帮手，同时也储存着指令的数据
 */
class CommandRegistrant
{
public:
    CommandRegistrant() = default;
    ~CommandRegistrant() = default;
    /**
     * @brief 注册一个命令。
     * 有多个别名的命令必须一次性全部注册，否则会创建新的命令
     * @param command 命令定义。
     */
    void registerCommand(Command command);
    /**通过指令名快速获得指令指针 */
    const Command *getCommand(const std::string &commandName) const;
    /**将全部的指令解析好，并提供输出用于向用户提供help */
    void provideHelp() const;
    /**获取全部指令名 */
    std::vector<std::string> commandWords() const;
    /**快捷注册在指令库中的指令*/
    void registerCommandsFromLibrary();

private:
    std::vector<Command> _commands;                      /**指令实际储存的容器 */
    std::unordered_map<std::string, std::size_t> _index; /**通过map将同一个指令对应的不同指令名指向相同vector下标 */
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_REGISTRANT_H
