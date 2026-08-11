#include "database/MySQL/MySQLDatabase.h"

// 链接数据库
QueryResult MySQLDatabase::connect(const std::string &host, int port, const std::string &userName, const std::string &password, const std::string &databaseName)
{
    // 如果现在已经有了MySQL连接，那么不需要再连接了，直接返回成功
    if (_conn)
    {
        return {true, ""};
    }
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

// 数据库执行语句
QueryResult MySQLDatabase::execute(const std::string &sql)
{
    if (!_conn)
    {
        return {false, "Database connection is not established."};
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        std::string errorMessage = mysql_error(_conn);
        return {false, errorMessage};
    }

    return {true, ""};
}

// 数据库查询语句
QueryResult MySQLDatabase::query(const std::string &sql)
{
    if (_conn)
    {
        return {false, "数据库已经存在,请先断开连接再进行连接"};
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        std::string errorMessage = mysql_error(_conn);
        return {false, errorMessage};
    }

    MYSQL_RES *result = mysql_store_result(_conn);
    if (!result)
    {
        std::string errorMessage = mysql_error(_conn);
        return {false, errorMessage};
    }

    QueryResult queryResult;
    queryResult.success = true;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        Row resultRow;
        unsigned long *lengths = mysql_fetch_lengths(result);
        for (unsigned int i = 0; i < mysql_num_fields(result); ++i)
        {
            resultRow.columns.emplace_back(row[i] ? row[i] : "", lengths[i]);
        }
        queryResult.rows.push_back(std::move(resultRow));
    }

    mysql_free_result(result);
    return queryResult;
}

// 断开数据库的连接
QueryResult MySQLDatabase::disconnect()
{
    if (_conn)
    {
        mysql_close(_conn);
        _conn = nullptr;
    }
    return {true, ""};
}