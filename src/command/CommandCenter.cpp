#include "command/CommandCenter.h"
#include "tool/FuzzyMatchTool.h"

#include <iostream>
#include <sstream>

CommandCenter::CommandCenter()
    : _commandExecutor(_commandRegistrant)
{
    ;
}

void CommandCenter::registerCommand(Command command)
{
    _commandRegistrant.registerCommand(command);
}

CommandResult CommandCenter::execute(const std::string &input) const
{
    return _commandExecutor.execute(input);
}

void CommandCenter::printHelp() const
{
    _commandRegistrant.provideHelp();
}
