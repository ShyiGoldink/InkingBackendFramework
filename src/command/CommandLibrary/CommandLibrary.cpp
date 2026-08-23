#include "command/CommandLibrary.h"

std::vector<std::vector<Command>> CommandLibrary::commands() const{
    std::vector<std::vector<Command>> commands;
    commands.push_back( databaseCommands());
    commands.push_back(uiCommands());
    return commands;
}