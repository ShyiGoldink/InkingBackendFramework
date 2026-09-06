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
    MySQLDatabase() = default;
    ~MySQLDatabase() override;
    // 禁用拷贝
    MySQLDatabase(const MySQLDatabase &) = delete;
    MySQLDatabase &operator=(const MySQLDatabase &) = delete;

    // 禁用移动
    MySQLDatabase(MySQLDatabase &&) = delete;
    MySQLDatabase &operator=(MySQLDatabase &&) = delete;

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
    /**
     * @brief 确保当前连接可用。
     *
     * MySQL 服务端可能因为重启、wait_timeout 清理或网络抖动断开空闲连接，
     * 连接池里借出的可能是已经失效的连接。
     * 每次操作前先 mysql_ping，失败则关闭旧连接并按保存的配置自动重连。
     */
    bool ensureConnected();
    /** @brief 按保存的配置重连（失败时把原因写入 _lastError） */
    bool reconnect();

    MYSQL *_conn = nullptr;
    std::string _host;
    int _port = 0;
    std::string _userName;
    std::string _password;
    std::string _databaseName;
    std::string _lastError; /** 最近一次连接/重连失败原因 */
};
#endif // INKING_BACKEND_FRAMEWORK_MYSQL_DATABASE_H
