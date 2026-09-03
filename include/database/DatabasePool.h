#ifndef INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H
#define INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H

#include "dataStruct/DatabaseStruct.h"

#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class IDatabase;

/**
 * 添加一个枚举，增强多池情况下的拓展性
 * 写在这里是为了保证在使用连接池时就能直接使用这个枚举
 */
enum PoolType
{
    MySQL = 0,
};

/**一般上限：常规情况下连接池保持的连接数量（也是清理空闲连接时的目标值） */
inline constexpr const int maxPoolNum = 10;
/**忙时上限：连接总数的绝对上限，超过后不再新建连接 */
inline constexpr const int busyPoolNum = 20;

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

    /** 当前连接池的状态，供 DatabaseManager 等模块写入自检阶段 */
    struct PoolInfo
    {
        PoolType poolType;
        uint64_t generation = 0; /** 当前连接池代次：池被释放/重建时自增 */
        int total = 0; /** 当前存活的连接总数（空闲 + 借出） */
        int idle = 0;  /** 当前空闲连接数 */
        int busy = 0;  /** 当前借出中的连接数 */
    };

    /**获取某类型连接池的当前状态（未初始化时全部为 0） */
    static PoolInfo info(PoolType type);
    /**手动释放超过一般上限(maxPoolNum)的空闲连接，返回清理掉的连接数量 */
    static int trimIdle(PoolType type);
    /**设置借出连接时等待空闲连接的超时时长，默认 5 秒 */
    static void setBorrowTimeout(const std::chrono::milliseconds &timeout);

    /**拿出借用好的对象 */
    IDatabase *operator->() const { return _db.get(); }
    bool valid() const { return _db != nullptr; }

private:
    /**锁内调用：池中有空闲连接则移入 _db，返回是否借出成功 */
    bool takeIdleLocked();
    /**无锁调用：按缓存配置新建并连接一条连接，失败返回 nullptr */
    static std::unique_ptr<IDatabase> createConnection(PoolType type, const DatabaseConfig &config);
    /**锁内调用：先占用名额再在锁外执行建连，完成后优先取用期间归还的连接 */
    bool createAndTake(const DatabaseConfig &config, std::unique_lock<std::mutex> &lock);

    /**空闲连接池：如果有多个池的话，可以从这里取，但其实全部都要走这里 */
    static std::unordered_map<PoolType, std::vector<std::unique_ptr<IDatabase>>> _pools;
    /**各类型的连接配置：池空时按需新建连接使用 */
    static std::unordered_map<PoolType, DatabaseConfig> _configs;
    /**各类型当前存活的连接总数（空闲 + 借出） */
    static std::unordered_map<PoolType, int> _total;
    /**各类型连接池的代次：池被 free()/freeAll() 释放时自增，用于识别旧连接 */
    static std::unordered_map<PoolType, uint64_t> _generations;
    static std::mutex _mutex;              /**用于在多线程之中保护连接池 */
    static std::condition_variable _condition; /**借出连接时等待空闲连接 */
    static std::chrono::milliseconds _borrowTimeout;

    /**归还对象，析构时自行调用 */
    void release();
    PoolType _poolType;             /**当前连接池对象的连接类型 */
    uint64_t _generation = 0;       /**借用时记录的池代次：归还时校验连接是否仍属于当前池 */
    std::unique_ptr<IDatabase> _db; /**当前借出的对象 */
};

#endif // INKING_BACKEND_FRAMEWORK_DATABASE_POOL_H
