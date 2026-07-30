#ifndef INKING_BACKEND_FRAMEWORK_I_DATABASE_H
#define INKING_BACKEND_FRAMEWORK_I_DATABASE_H

#include "dataStruct/QueryResult.h"
/**
 * @brief
 * 定义了数据库应该有哪些功能的接口
 */
class IDatabase
{
public:
    /**
     * @brief
     * 连接到数据库
     * @param host 数据库的服务器地址(在哪一台机器上)
     * @param port 数据库端口
     * @param userName 用户名
     * @param password 数据库密码
     * @param databaseName 数据库名
     */
    virtual bool connect(const std::string &host, int port, const std::string &userName, const std::string &password, const std::string &databaseName) = 0;
    /**增删改数据*/
    virtual QueryResult execute(const std::string &sql) = 0;
    /**查数据 */
    virtual QueryResult query(const std::string &sql) = 0;
    /**断开链接 */
    virtual bool disconnect() = 0;

    virtual ~IDatabase() = default;
};
#endif // INKING_BACKEND_FRAMEWORK_I_DATABASE_H