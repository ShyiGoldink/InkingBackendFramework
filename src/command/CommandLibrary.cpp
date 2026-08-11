#include "command/CommandLibrary.h"

#include "database/DatabaseManager.h"

std::vector<Command> CommandLibrary::databaseCommands() const
{
    return {
        {"database-init",
         {"db-init", "database-initialize", "initdatabase"},
         "初始化数据库",
         []()
         {
             DatabaseManager::instance().initDatabase();
             return CommandResult::Continue;
         },
         false}};
}
