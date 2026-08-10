#include "MySQL/MySQLDatabase.h"

QueryResult MySQLDatabase::connect(const std::string &host, int port, const std::string &userName, const std::string &password, const std::string &databaseName)
{
    // 创建MySQL对象
    _conn = mysql_init(nullptr);
    if (!_conn)
    {
        return {false, "Failed to initialize MySQL connection."};
    }

    // 连接到数据库
    if (!mysql_real_connect(_conn, host.c_str(), userName.c_str(), password.c_str(), databaseName.c_str(), port, nullptr, 0))
    {
        std::string errorMessage = mysql_error(_conn);
        mysql_close(_conn);
        _conn = nullptr;
        return {false, errorMessage};
    }

    return {true, ""};
}