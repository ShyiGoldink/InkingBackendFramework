#include "database/MySQL/MySQLDatabase.h"

// 链接数据库
QueryResult MySQLDatabase::connect(const std::string &host, int port, const std::string &userName, const std::string &password, const std::string &databaseName)
{
    // 保存配置，供后续断线自动重连使用
    _host = host;
    _port = port;
    _userName = userName;
    _password = password;
    _databaseName = databaseName;

    // 如果现在已经有了MySQL连接，那么不需要再连接了，直接返回成功
    if (_conn)
    {
        return {true, ""};
    }

    if (!reconnect())
    {
        return {false, _lastError.empty() ? "Failed to connect to MySQL." : _lastError};
    }
    return {true, ""};
}

bool MySQLDatabase::ensureConnected()
{
    // 连接存在且心跳正常，直接使用
    if (_conn && mysql_ping(_conn) == 0)
    {
        return true;
    }

    // 连接已失效：关闭旧连接后自动重连
    if (_conn)
    {
        mysql_close(_conn);
        _conn = nullptr;
    }
    _lastError.clear();
    return reconnect();
}

bool MySQLDatabase::reconnect()
{
    // 创建MySQL对象
    _conn = mysql_init(nullptr);
    if (!_conn)
    {
        _lastError = "Failed to initialize MySQL connection.";
        return false;
    }

    // 连接到数据库
    if (!mysql_real_connect(_conn, _host.c_str(), _userName.c_str(),
                            _password.c_str(), _databaseName.c_str(), _port, nullptr, 0))
    {
        _lastError = mysql_error(_conn);
        mysql_close(_conn);
        _conn = nullptr;
        return false;
    }

    _lastError.clear();
    return true;
}

// 数据库执行语句
QueryResult MySQLDatabase::execute(const std::string &sql)
{
    if (!ensureConnected())
    {
        return {false, "MySQL 连接失效且自动重连失败: " + _lastError};
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        std::string errorMessage = mysql_error(_conn);
        return {false, errorMessage};
    }

    QueryResult queryResult;
    queryResult.success = true;

    my_ulonglong affected = mysql_affected_rows(_conn);

    if (affected == (my_ulonglong)-1)
    {
        queryResult.success = false;
        queryResult.errorMessage = mysql_error(_conn);
    }
    else
    {
        queryResult.affectedRows = affected;
    }
    return queryResult;
}

// 数据库查询语句
QueryResult MySQLDatabase::query(const std::string &sql)
{
    if (!ensureConnected())
    {
        return {false, "MySQL 连接失效且自动重连失败: " + _lastError};
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

MySQLDatabase::~MySQLDatabase()
{
    disconnect();
}
