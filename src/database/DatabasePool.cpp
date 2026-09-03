#include "database/DatabasePool.h"
#include "database/IDatabase.h"
#include "basic/ShineLog.h"

#ifdef INKING_ENABLE_MYSQL
#include "database/MySQL/MySQLDatabase.h"
#endif

std::unordered_map<PoolType, std::vector<std::unique_ptr<IDatabase>>> DatabasePool::_pools;
std::unordered_map<PoolType, DatabaseConfig> DatabasePool::_configs;
std::unordered_map<PoolType, int> DatabasePool::_total;
std::unordered_map<PoolType, uint64_t> DatabasePool::_generations;
std::mutex DatabasePool::_mutex;
std::condition_variable DatabasePool::_condition;
std::chrono::milliseconds DatabasePool::_borrowTimeout{5000};

// 构造函数，构造成功的同时就取一个指针作为对象
DatabasePool::DatabasePool(PoolType poolType)
    : _poolType(poolType)
{
    std::unique_lock<std::mutex> lock(_mutex);

    // 有空闲连接，直接借出
    if (takeIdleLocked())
    {
        return;
    }

    // 连接池从未初始化（没有缓存配置），无法按需建连
    auto configIt = _configs.find(poolType);
    if (configIt == _configs.end())
    {
        //选择抛出异常，让DatabaseManager去处理
        throw std::runtime_error("借用连接失败：连接池尚未初始化，请先调用 init()");
    }

    const DatabaseConfig config = configIt->second; // 拷贝配置，在锁外执行建连

    // 存活连接数未达一般上限：直接新建连接（快速扩容，无需等待）
    if (_total[poolType] < maxPoolNum)
    {
        if (createAndTake(config, lock))
        {
            return;
        }
        throw std::runtime_error("新建数据库连接失败，本次借用失败");
    }

    // 存活连接数已达一般上限：优雅等待空闲连接被归还
    // 超时后若仍未超过忙时上限，再新建连接扩容；否则本次借用失败
    const bool gotIdle = _condition.wait_for(lock, _borrowTimeout, [poolType]()
    {
        const auto poolIt = _pools.find(poolType);
        const bool hasIdle = poolIt != _pools.end() && !poolIt->second.empty();
        const bool poolAlive = _configs.find(poolType) != _configs.end();
        // 空闲连接出现，或等待期间连接池被释放
        return hasIdle || !poolAlive;
    });

    if (gotIdle)
    {
        if (takeIdleLocked())
        {
            return;
        }
        // 唤醒原因是被释放而非有空闲连接
        throw std::runtime_error("等待期间连接池已被释放，本次借用失败");
    }

    // 等待超时
    if (_configs.find(poolType) == _configs.end())
    {
        throw std::runtime_error("连接超时，本次借用失败");
    }

    if (_total[poolType] < busyPoolNum)
    {
        // 尚未达到忙时上限：为应对突发负载扩容一条连接
        if (!createAndTake(config, lock))
        {
            throw std::runtime_error("新建数据库连接失败，本次借用失败");
        }
        return;
    }

    // 已达忙时上限且等待超时：本次借用失败
    throw std::runtime_error(
                    "连接池已满（" + std::to_string(_total[poolType]) + "/" +
                        std::to_string(busyPoolNum) + "）且等待超时，本次借用失败");
}

DatabasePool::~DatabasePool()
{
    release();
}

// 锁内调用：从池中取出一条空闲连接
bool DatabasePool::takeIdleLocked()
{
    auto it = _pools.find(_poolType);
    if (it == _pools.end() || it->second.empty())
    {
        return false;
    }

    _db = std::move(it->second.back());
    it->second.pop_back();
    _generation = _generations[_poolType]; // 记录本次借出时的池代次
    return true;
}

// 无锁调用：按配置新建一条数据库连接
std::unique_ptr<IDatabase> DatabasePool::createConnection(PoolType poolType, const DatabaseConfig &config)
{
    switch (poolType)
    {
#ifdef INKING_ENABLE_MYSQL
    case PoolType::MySQL:
    {
        auto connection = std::make_unique<MySQLDatabase>();
        const QueryResult result =
            connection->connect(config.host, config.port, config.userName, config.password, config.databaseName);
        if (result.success)
        {
            return connection;
        }
        return nullptr;
    }
#endif
    default:
        return nullptr;
    }
}

// 锁内调用：先占用一个名额，再在锁外建连，避免慢速建连阻塞整个池
bool DatabasePool::createAndTake(const DatabaseConfig &config, std::unique_lock<std::mutex> &lock)
{
    const uint64_t poolGeneration = _generations[_poolType]; // 记录建连前的池代次
    ++_total[_poolType]; // 先占名额，防止多个借出方并发扩容超过忙时上限

    lock.unlock();
    auto fresh = createConnection(_poolType, config);
    lock.lock();

    // 建连期间连接池被 free()/freeAll() 释放甚至用新配置重建：
    // 占的名额已随旧池一起失效，旧配置新建的连接绝不能进入新代次的池
    if (_generations[_poolType] != poolGeneration)
    {
        if (fresh)
        {
            fresh->disconnect();
        }
        return false;
    }

    if (!fresh)
    {
        if (_total[_poolType] > 0)
        {
            --_total[_poolType];
        }
        return false;
    }

    // 建连期间有连接被归还：优先借出归还的连接，把新建的连接放入池中
    if (takeIdleLocked())
    {
        _pools[_poolType].push_back(std::move(fresh));
        _condition.notify_one();
        return true;
    }

    _db = std::move(fresh);
    _generation = poolGeneration;
    return true;
}

void DatabasePool::release()
{
    if (!_db)
    {
        return;
    }

    std::unique_ptr<IDatabase> database;

    std::unique_lock<std::mutex> lock(_mutex);
    auto it = _pools.find(_poolType);

    // 只有池还存在、且代次与借用时一致，才允许把连接归还进池；
    // 旧代次的连接（free() 之后才析构）直接断开，防止进入新配置的池
    if (it != _pools.end() && _generations[_poolType] == _generation)
    {
        it->second.push_back(std::move(_db));
        _condition.notify_one();
        return;
    }

    database = std::move(_db);
    lock.unlock();

    database->disconnect();
}

std::vector<QueryResult> DatabasePool::init(PoolType poolType, int num, DatabaseConfig config)
{
    std::vector<QueryResult> result = {};
    // 初始化数量不得超过忙时上限
    num = num > busyPoolNum ? busyPoolNum : num;
    std::lock_guard<std::mutex> lock(_mutex);
    // 没有该类型的池再进行初始化，否则不进行初始化
    if (_pools.find(poolType) == _pools.end())
    {
        std::vector<std::unique_ptr<IDatabase>> databasePool;
        switch (poolType)
        {
            // 先只完善MySQL的初始化，其它的按需补充
#ifdef INKING_ENABLE_MYSQL
        case PoolType::MySQL:
            for (int i = 0; i < num; i++)
            {
                auto mySQL = std::make_unique<MySQLDatabase>();
                auto queryResult = mySQL->connect(config.host, config.port, config.userName, config.password, config.databaseName);
                if (queryResult.success)
                {
                    databasePool.push_back(std::move(mySQL));
                }
                result.push_back(queryResult);
            }
            break;
#endif
        default:
            break;
        }
        const int createdCount = static_cast<int>(databasePool.size());
        if (createdCount > 0)
        {
            _pools[poolType] = std::move(databasePool);
            _configs[poolType] = config; // 缓存配置，供池空时按需建连
            _total[poolType] = createdCount; // 记录当前存活连接总数
        }
    }
    return result;
}

std::vector<QueryResult> DatabasePool::free(PoolType poolType)
{
    std::vector<QueryResult> results;
    std::vector<std::unique_ptr<IDatabase>> databases;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _pools.find(poolType);
        if (it == _pools.end())
        {
            return results;
        }

        ++_generations[poolType]; // 池代次自增：旧借出连接归还时不允许再进入新池

        databases = std::move(it->second);
        _pools.erase(it); // 移除该池
        _configs.erase(poolType);
        _total.erase(poolType);
        _condition.notify_all(); // 唤醒等待中的借出方，让其尽快结束
    }

    for (auto &database : databases)
    {
        if (database)
        {
            results.push_back(database->disconnect());
        }
    }

    return results;
}

std::vector<QueryResult> DatabasePool::freeAll()
{
    std::vector<QueryResult> results;
    std::vector<std::unique_ptr<IDatabase>> databases;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto &pool : _pools)
        {
            ++_generations[pool.first]; // 池代次自增：旧借出连接归还时不允许再进入新池
            for (auto &database : pool.second)
            {
                databases.push_back(std::move(database));
            }
        }

        _pools.clear();
        _configs.clear();
        _total.clear();
        _condition.notify_all();
    }

    for (auto &database : databases)
    {
        if (database)
        {
            results.push_back(database->disconnect());
        }
    }

    return results;
}

DatabasePool::PoolInfo DatabasePool::info(PoolType poolType)
{
    std::lock_guard<std::mutex> lock(_mutex);

    PoolInfo poolInfo{};
    poolInfo.poolType = poolType;

    const auto poolIt = _pools.find(poolType);
    poolInfo.idle = poolIt != _pools.end() ? static_cast<int>(poolIt->second.size()) : 0;

    const auto totalIt = _total.find(poolType);
    poolInfo.total = totalIt != _total.end() ? totalIt->second : 0;

    const auto generationIt = _generations.find(poolType);
    poolInfo.generation = generationIt != _generations.end() ? generationIt->second : 0;

    poolInfo.busy = poolInfo.total - poolInfo.idle;
    if (poolInfo.busy < 0)
    {
        poolInfo.busy = 0;
    }
    return poolInfo;
}

int DatabasePool::trimIdle(PoolType poolType)
{
    std::vector<std::unique_ptr<IDatabase>> databases;
    int removed = 0;

    {
        std::unique_lock<std::mutex> lock(_mutex);

        auto it = _pools.find(poolType);
        if (it == _pools.end())
        {
            return 0;
        }

        // 只保留一般上限内的空闲连接，超出部分移出并在锁外断开
        int excess = static_cast<int>(it->second.size()) - maxPoolNum;
        while (excess-- > 0 && !it->second.empty())
        {
            databases.push_back(std::move(it->second.back()));
            it->second.pop_back();
            ++removed;

            auto totalIt = _total.find(poolType);
            if (totalIt != _total.end() && totalIt->second > 0)
            {
                --totalIt->second;
            }
        }
    }

    for (auto &database : databases)
    {
        if (database)
        {
            database->disconnect();
        }
    }

    return removed;
}

void DatabasePool::setBorrowTimeout(const std::chrono::milliseconds &timeout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _borrowTimeout = timeout;
}
