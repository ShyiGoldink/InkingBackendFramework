#ifndef INKING_BACKEND_FRAMEWORK_MYSQL_DATABASE_H
#define INKING_BACKEND_FRAMEWORK_MYSQL_DATABASE_H

#include <mysql.h>
#include "database/IDatabase.h"
/**
 * @brief
 * 这是一个MySQL的数据库实现类
 */

class MySQLDatabase : public IDatabase
{
public:
    QueryResult connect(
        const std::string &host,
        int port,
        const std::string &userName,
        const std::string &password,
        const std::string &databaseName) override;

    QueryResult execute(const std::string &sql) override;
    QueryResult query(const std::string &sql) override;
    QueryResult disconnect() override;

private:
    MYSQL *_conn = nullptr;
};
#endif // INKING_BACKEND_FRAMEWORK_MYSQL_DATABASE_H