#include "command/CommandLibrary.h"

#include "database/DatabaseManager.h"
#include "ui/UIMessageLibrary.h"

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
        {"mysql-e",
         {"mysql-execute", "mysqle"},
         "增/删/改数据库",
         [](const std::vector<std::string> &args)
         {
             if (args.empty())
             {
                 UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "用法: mysql-e <SQL 语句>");
                 return CommandResult::Continue;
             }
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().execute(PoolType::MySQL, args);
             return CommandResult::Continue;
         },
         false},
        {"mysql-q",
         {"mysql-query", "mysqlq"},
         "查数据库",
         [](const std::vector<std::string> &args)
         {
             if (args.empty())
             {
                 UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "用法: mysql-q <SQL 语句>");
                 return CommandResult::Continue;
             }
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().query(PoolType::MySQL, args);
             return CommandResult::Continue;
         },
         false},
        {"mysql-disconnect",
         {"mysql-d", "mysqld"},
         "断开MySQL的数据库连接",
         [](const std::vector<std::string> &args)
         {
             DatabaseManager::instance().sayMyName();
             DatabaseManager::instance().disconnectDatabase(PoolType::MySQL);
             return CommandResult::Continue;
         },
         false}};
}
