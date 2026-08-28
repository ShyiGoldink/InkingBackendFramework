#ifndef INKING_BACKEND_FRAMEWORK_DATABASE_MANAGER_H
#define INKING_BACKEND_FRAMEWORK_DATABASE_MANAGER_H

// 这里是具体的类，之后用其它代替。
// 我这里先用IDatabase代替，等之后开发好MySQL的基础功能之后再替换成MySQL
// 用户有需要自己去替换成其他数据库，都可以，没什么关系。
#include "basic/ShineBasicModule.h"
#include "dataStruct/DatabaseStruct.h"
#include "database/DatabasePool.h"

#include <memory>

inline constexpr const char *kDatabaseManagerModuleName = "DatabaseManager";

/**
 * @brief
 * DatabaseManager是用于使用database的框架
 * 其真正的工作是依靠database的具体实现
 * 这里只提供调用方法的接口
 */
class DatabaseManager : public ShineBasicModule
{
public:
    /**返回单例 */
    static DatabaseManager &instance()
    {
        static DatabaseManager inst;
        return inst;
    }
    /**使用单例模式，禁用移动构造函数，移动函数，拷贝构造函数，拷贝函数 */
    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;
    DatabaseManager(DatabaseManager &&) = delete;
    DatabaseManager &operator=(DatabaseManager &&) = delete;

    std::string moduleName() const override
    {
        return kDatabaseManagerModuleName;
    };
    /**初始化数据库，根据的是Json配置文件中的内容 */
    void initDatabase();
    /** 增删改数据库*/
    void execute(PoolType poolType, const std::vector<std::string> &args);
    /**查数据库 */
    void query(PoolType poolType, const std::vector<std::string> &args);
    /**断开连接 */
    void disconnectDatabase(PoolType poolType);

private:
    /**私有构造函数，禁止外部调用 */
    DatabaseManager();
    /**加载数据库配置 */
    bool loadDatabaseConfig(DatabaseConfig &config);
    /** 辅助方法-将vector转换成string进行查询*/
    std::string vTransTos(const std::vector<std::string> &);
};

#endif // INKING_BACKEND_FRAMEWORK_DATABASE_MANAGER_H
