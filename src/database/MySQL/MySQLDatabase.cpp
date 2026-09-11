#include "database/MySQL/MySQLDatabase.h"

// mysql.h 只带进了 errmsg.h（CR_* 客户端错误码）,
// 服务端的 ER_* 错误码在 mysqld_error.h 里,要单独包含。
#include <mysqld_error.h>

namespace
{
/**
 * 把 MySQL 原生错误码分类：连接级的连接必须丢弃，语句级的可以继续用。
 *
 * 这里分两段。2000~2999 是客户端库自己的错误码（CR_*，errmsg.h），
 * 1000~1999 和 3000~4999 是服务端返回的错误码（ER_*，mysqld_error.h）。
 * 服务端的错误码绝大多数是"这条语句有问题"，连接是好的；
 * 只有下面列出的这几个意味着"收到这个错误之后这条连接已经不能用了"。
 *
 * 注意 4031：MySQL 8.0.24 之后，服务端回收空闲连接时不再只是静默断开，
 * 而是先回一个 4031 再断开。这个是实测踩到的，漏了它坏连接就会被当成
 * 普通的语句错误原样还回池里。
 *
 * 另外 1317(ER_QUERY_INTERRUPTED) 故意不列进来：那是"查询被 KILL QUERY 打断"，
 * 连接本身通常还在。万一它真把协议状态弄乱了，下一次会报 2014，
 * 而 2014 在上面，届时照样会被丢弃。
 */
ErrorKind classifyMySQLError(unsigned int code)
{
    switch (code)
    {
    // ---- 客户端库层面的错误 ----
    case CR_CONNECTION_ERROR:     // 2002 本地连不上服务端
    case CR_CONN_HOST_ERROR:      // 2003 连接服务端失败
    case CR_UNKNOWN_HOST:         // 2005 域名解析失败
    case CR_SERVER_GONE_ERROR:    // 2006 服务端已经不在
    case CR_SERVER_HANDSHAKE_ERR: // 2012 握手失败
    case CR_SERVER_LOST:          // 2013 查询过程中连接丢失（读写超时也报这个）
    case CR_COMMANDS_OUT_OF_SYNC: // 2014 协议状态错乱，通常是上一条结果集没取干净
    case CR_NET_PACKET_TOO_LARGE: // 2020 包过大，协议状态已经不可信
    case CR_SSL_CONNECTION_ERROR: // 2026 TLS 层断开
    case CR_SERVER_LOST_EXTENDED: // 2055 连接丢失

    // ---- 服务端层面：收到即代表连接已经不可用 ----
    case ER_SERVER_SHUTDOWN:            // 1053 服务端正在关闭
    case ER_ABORTING_CONNECTION:        // 1152 连接被中断
    case ER_SESSION_WAS_KILLED:         // 3169 本会话被 KILL
    case ER_CLIENT_INTERACTION_TIMEOUT: // 4031 空闲太久被服务端回收（8.0.24+）
        return ErrorKind::Connection;
    default:
        return ErrorKind::Statement;
    }
}
} // namespace

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
        return {false, "Failed to initialize MySQL connection.", ErrorKind::Local};
    }

    // 这三个必须在 mysql_real_connect 之前设置。
    // 没有读写超时的话，半开连接（拔网线、NAT 表项过期、对端进程假死）
    // 会让 mysql_query 永久阻塞，只报不了错也返回不了，直接把一个借出名额吃掉。
    // 注意不要在其它地方设 MYSQL_OPT_RECONNECT：它的重连会静默丢掉会话状态，
    // 而且会让 ping() 永远返回成功，把探活变成摆设。
    unsigned int connectTimeoutSeconds = 5;
    unsigned int readTimeoutSeconds = 5;
    unsigned int writeTimeoutSeconds = 5;
    mysql_options(_conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSeconds);
    mysql_options(_conn, MYSQL_OPT_READ_TIMEOUT, &readTimeoutSeconds);
    mysql_options(_conn, MYSQL_OPT_WRITE_TIMEOUT, &writeTimeoutSeconds);

    // 连接到数据库
    if (!mysql_real_connect(_conn, host.c_str(), userName.c_str(), password.c_str(), databaseName.c_str(), port, nullptr, 0))
    {   
        const unsigned int code = mysql_errno(_conn);
        std::string errorMessage = mysql_error(_conn);
        mysql_close(_conn);
        _conn = nullptr;
        return {false, errorMessage,ErrorKind::Connection, code};
    }

     _broken = false;
    return {true, ""};
}

// 数据库执行语句
QueryResult MySQLDatabase::execute(const std::string &sql)
{
    if (!_conn)
    {
        return {false, "数据库尚未连接，请先连接", ErrorKind::Local};
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        const unsigned int code = mysql_errno(_conn);
        const std::string errorMessage = mysql_error(_conn);
        const ErrorKind kind = classifyMySQLError(code);
        if (kind == ErrorKind::Connection)
        {
            _broken = true; // 自己先记住，归还时池子不用再问
        }
        return {false, errorMessage, kind, code};
    }

    QueryResult queryResult;
    queryResult.success = true;

   // 误把带结果集的语句交给 execute 时，必须取干净并释放。
    // 不释放会让协议状态错乱，之后每条语句都报 2014，这条连接等于废掉。
    if (mysql_field_count(_conn) > 0)
    {
        if (MYSQL_RES *result = mysql_store_result(_conn))
        {
            queryResult.affectedRows = mysql_num_rows(result);
            mysql_free_result(result);
        }
    }
    else
    {
        const my_ulonglong affected = mysql_affected_rows(_conn);
        if (affected == (my_ulonglong)-1)
        {
            const unsigned int code = mysql_errno(_conn);
            queryResult.success = false;
            queryResult.errorMessage = mysql_error(_conn);
            queryResult.errorCode = code;
            queryResult.errorKind = classifyMySQLError(code);
            if (queryResult.errorKind == ErrorKind::Connection)
            {
                _broken = true;
            }
        }
        else
        {
            queryResult.affectedRows = affected;
        }
    }
    return queryResult;
}

// 数据库查询语句
QueryResult MySQLDatabase::query(const std::string &sql)
{
    if (!_conn)
    {
        return {false, "数据库尚未连接，请先连接",ErrorKind::Local};
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        const unsigned int code = mysql_errno(_conn);
        const std::string errorMessage = mysql_error(_conn);
        const ErrorKind kind = classifyMySQLError(code);
        if (kind == ErrorKind::Connection)
        {
            _broken = true;
        }
        return {false, errorMessage, kind, code};
    }

    MYSQL_RES *result = mysql_store_result(_conn);
    if (!result)
    {
        const unsigned int code = mysql_errno(_conn);
        if (code != 0)
        {
            const ErrorKind kind = classifyMySQLError(code);
            if (kind == ErrorKind::Connection)
            {
                _broken = true;
            }
            return {false, mysql_error(_conn), kind, code};
        }

        // 没有错误码：这条语句本来就没有结果集（例如把 INSERT 交给 query），不是错误
        QueryResult noRows;
        noRows.success = true;
        noRows.affectedRows = mysql_affected_rows(_conn);
        return noRows;
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
        _broken = false;
    }
    return {true, ""};
}

QueryResult MySQLDatabase::ping()
{
    if (!_conn)
    {
        return {false, "数据库尚未连接，请先连接", ErrorKind::Local};
    }

    if (mysql_ping(_conn))
    {
        const unsigned int code = mysql_errno(_conn);
        const std::string errorMessage = mysql_error(_conn);
        _broken = true;
        return {false, errorMessage, ErrorKind::Connection, code};
    }

    _broken = false;
    return {true, ""};
}

bool MySQLDatabase::isBroken() const
{
    return _conn == nullptr || _broken;
}

MySQLDatabase::~MySQLDatabase()
{
    disconnect();
}
