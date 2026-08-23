#include "command/CommandLibrary.h"

#include "database/DatabaseManager.h"

std::vector<Command> CommandLibrary::databaseCommands() const
{
    return {
        {"database-init",
         {"db-init", "database-initialize", "initdatabase"},
         "初始化数据库",
         [](const std::vector<std::string> &)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().initDatabase();
             return CommandResult::Continue;
         },
         false},
        {"execute",
         {"database-execute", "db-execute"},
         "增/删/改数据库",
         [](const std::vector<std::string> &args)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().execute(args);
             return CommandResult::Continue;
         },
         false},
        {"query",
         {"database-query", "db-query"},
         "查数据库",
         [](const std::vector<std::string> &args)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().query(args);
             return CommandResult::Continue;
         },
         false},
        {"execute",
         {"database -execute", "db -execute"},
         "增/删/改数据库",
         [](const std::vector<std::string> &args)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().query(args);
             return CommandResult::Continue;
         },
         false},
        {"disconnect",
         {"database-disconnect", "db-disconnect"},
         "增/删/改数据库",
         [](const std::vector<std::string> &args)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().disconnectDatabase();
             return CommandResult::Continue;
         },
         false}};
}
