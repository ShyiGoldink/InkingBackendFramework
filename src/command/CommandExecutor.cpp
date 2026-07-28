#include <iostream>
#include <sstream>
#include "command/CommandRegistrant.h"
#include "command/CommandExecutor.h"
#include "tool/FuzzyMatchTool.h"

CommandExecutor::CommandExecutor(CommandRegistrant &commandRegistrant)
{
    _commandRegistrant = &commandRegistrant;
}

CommandResult CommandExecutor::execute(const std::string &input) const
{
    const std::string commandName = firstWord(input);
    if (commandName.empty())
    {
        return CommandResult::Continue;
    }

    const Command *command = _commandRegistrant->getCommand(commandName);
    if (command != nullptr)
    {
        return command->action();
    }

    std::cout << "没有找到命令：" << commandName << std::endl;

    const auto suggestion = FuzzyMatchTool::bestMatch(commandName, _commandRegistrant->commandWords());
    if (suggestion.has_value())
    {
        std::cout << "你是不是想输入：" << suggestion.value() << " ?" << std::endl;
    }

    std::cout << "输入 help 查看可用命令。" << std::endl;
    return CommandResult::Continue;
}

std::string CommandExecutor::firstWord(const std::string &input) const
{
    std::istringstream stream(input);
    std::string word;
    stream >> word;
    return word;
}
