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
    const auto tokens = tokenize(input);
    if (tokens.empty())
        return CommandResult::Continue;

    const std::string &commandName = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    if (commandName.empty())
    {
        return CommandResult::Continue;
    }

    const Command *command = _commandRegistrant->getCommand(commandName);
    if (command != nullptr)
    {
        return command->action(args);
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

// 使用tokenize强化命令，而不只是简单使用firstWord。
// 同时通过简单的计数方式来判断引号
// 避免出现特殊的问题
std::vector<std::string> CommandExecutor::tokenize(const std::string &input) const
{
    std::vector<std::string> tokens;
    std::string current;
    // 奇数 = 在引号内，偶数 = 引号外
    int quoteLevel = 0;

    for (char c : input)
    {
        if (c == '"' || c == '\'')
        {
            // 引号本身保留，方便 SQL 拼接
            ++quoteLevel;
            current += c;
        }
        else if ((c == ' ' || c == '\t') && quoteLevel % 2 == 0)
        {
            // 引号外的空格才切分
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
    {
        tokens.push_back(current);
    }

    return tokens;
}
