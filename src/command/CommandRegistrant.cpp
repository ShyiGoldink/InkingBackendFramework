#include <iostream>
#include "command/CommandRegistrant.h"

// 注册命令的函数
void CommandRegistrant::registerCommand(Command command)
{ // 先获取当前vector已有的size，用于添加新的命令
    const std::size_t index = _commands.size();
    // 防止重复注册以及空名注册
    if (command.name.empty() || _index.find(command.name) != _index.end())
    {
        return;
    }
    // 如果命令为空也要避免注册
    if (command.action == nullptr)
    {
        return;
    }
    // 先注册正式的名称
    _index[command.name] = index;
    // 再将别名注册
    for (const auto &alias : command.aliases)
    {   
        // 如果别名为空或者已经注册过了，就不注册
        if (alias.empty() || _index.find(alias) != _index.end())
        {
            continue;
        }
        _index[alias] = index;
    }
    // 最后将命令添加到vector之中
    _commands.push_back(std::move(command));
}

// 获取到指令的指针
const Command *CommandRegistrant::getCommand(const std::string &commandName) const
{
    const auto iter = _index.find(commandName);
    if (iter == _index.end())
    {
        return nullptr;
    }
    // 如果没有指令返回nullptr，外部get之后可以判断是否有该指令
    return &_commands[iter->second];
}

// 解析并输出help
void CommandRegistrant::provideHelp() const
{
    std::cout << "可用命令：" << std::endl;
    for (const auto &command : _commands)
    {
        if (command.hidden)
            continue;
        std::cout << "  " << command.name;
        if (!command.aliases.empty())
        {
            std::cout << " (";
            for (std::size_t i = 0; i < command.aliases.size(); ++i)
            {
                if (i != 0)
                    std::cout << ", ";
                std::cout << command.aliases[i];
            }
            std::cout << ")";
        }
        std::cout << " - " << command.description << std::endl;
    }
}
std::vector<std::string> CommandRegistrant::commandWords() const
{
    std::vector<std::string> words;
    words.reserve(_index.size());

    for (const auto &command : _commands)
    {
        if (command.hidden)
        {
            continue;
        }
        words.push_back(command.name);
    }

    for (const auto &command : _commands)
    {
        if (command.hidden)
        {
            continue;
        }
        for (const auto &alias : command.aliases)
        {
            words.push_back(alias);
        }
    }

    return words;
}

// 快捷注册在指令库中的指令
void CommandRegistrant::registerCommandsFromLibrary()
{
    CommandLibrary commandLibrary;
    const auto commands = commandLibrary.databaseCommands();
    for (const auto &command : commands)
    {
        registerCommand(command);
    }
}