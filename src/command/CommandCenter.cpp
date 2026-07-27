#include "command/CommandCenter.h"

#include "tool/FuzzyMatcher.h"

#include <iostream>
#include <sstream>

void CommandCenter::registerCommand(Command command)
{
    const std::size_t index = _commands.size();
    _index[command.name] = index;

    for (const auto& alias : command.aliases) {
        _index[alias] = index;
    }

    _commands.push_back(std::move(command));
}

CommandResult CommandCenter::execute(const std::string& input) const
{
    const std::string commandName = firstWord(input);
    if (commandName.empty()) {
        return CommandResult::Continue;
    }

    const Command* command = findCommand(commandName);
    if (command != nullptr) {
        return command->action();
    }

    std::cout << "没有找到命令：" << commandName << std::endl;

    const auto suggestion = FuzzyMatcher::bestMatch(commandName, commandWords());
    if (suggestion.has_value()) {
        std::cout << "你是不是想输入：" << suggestion.value() << " ?" << std::endl;
    }

    std::cout << "输入 help 查看可用命令。" << std::endl;
    return CommandResult::Continue;
}

void CommandCenter::printHelp() const
{
    std::cout << "可用命令：" << std::endl;
    for (const auto& command : _commands) {
        if (command.hidden) {
            continue;
        }

        std::cout << "  " << command.name;
        if (!command.aliases.empty()) {
            std::cout << " (";
            for (std::size_t i = 0; i < command.aliases.size(); ++i) {
                if (i != 0) {
                    std::cout << ", ";
                }
                std::cout << command.aliases[i];
            }
            std::cout << ")";
        }
        std::cout << " - " << command.description << std::endl;
    }
}

const Command* CommandCenter::findCommand(const std::string& commandName) const
{
    const auto iter = _index.find(commandName);
    if (iter == _index.end()) {
        return nullptr;
    }

    return &_commands[iter->second];
}

std::vector<std::string> CommandCenter::commandWords() const
{
    std::vector<std::string> words;
    words.reserve(_index.size());

    for (const auto& command : _commands) {
        if (command.hidden) {
            continue;
        }
        words.push_back(command.name);
    }

    for (const auto& command : _commands) {
        if (command.hidden) {
            continue;
        }
        for (const auto& alias : command.aliases) {
            words.push_back(alias);
        }
    }

    return words;
}

std::string CommandCenter::firstWord(const std::string& input)
{
    std::istringstream stream(input);
    std::string word;
    stream >> word;
    return word;
}
