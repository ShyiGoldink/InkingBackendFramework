#ifndef INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H
#define INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H

#include "dataStruct/DatabaseStruct.h"

#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

class IDatabase;

/**
 * 添加一个枚举，增强多池情况下的拓展性
 * 写在这里是为了保证在使用连接池时就能直接使用这个枚举
 */
enum PoolType
{
    MySQL = 0,
};

inline constexpr const int maxPoolNum = 10;
/**
 * 这是我自己设计的对象池模型
 * 内存相较于传统模型比较优势
 * 拓展性稍差
 * 比较好理解
 */
class DatabasePool
{
public:
    DatabasePool(PoolType);
    ~DatabasePool();

    DatabasePool(const DatabasePool &) = delete;
    DatabasePool &operator=(const DatabasePool &) = delete;
    DatabasePool(DatabasePool &&) = delete;
    DatabasePool &operator=(DatabasePool &&) = delete;

    /**
     * @brief 初始化连接池
     * @param type 池类型
     * @param num 初始数量
     * @param config 数据库设置
     */
    static std::vector<QueryResult> init(PoolType type, int num, DatabaseConfig config);
    /**断开全部连接 */
    static std::vector<QueryResult> freeAll();
    /**断开某个数据库类型的全部连接 */
    static std::vector<QueryResult> free(PoolType type);
    /**拿出借用好的对象 */
    IDatabase *operator->() const { return _db.get(); }
    bool valid() const { return _db != nullptr; }

private:
    static std::unordered_map<PoolType, std::vector<std::unique_ptr<IDatabase>>> _pools; /**如果有多个池的话，可以从这里取，但其实全部都要走这里 */

    /**归还对象，析构时自行调用 */
    void release();
    PoolType _poolType;             /**当前连接池对象的连接类型 */
    std::unique_ptr<IDatabase> _db; /**当前借出的对象 */
    static std::mutex _mutex;/**用于在多线程之中保护队列 */
};

#endif // INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H