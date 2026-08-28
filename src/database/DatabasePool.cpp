#include "database/DatabasePool.h"
#include "database/IDatabase.h"
#include "database/MySQL/MySQLDatabase.h"

std::unordered_map<PoolType, std::vector<std::unique_ptr<IDatabase>>> DatabasePool::_pools;

// 构造函数，构造成功的同时就取一个指针作为对象
DatabasePool::DatabasePool(PoolType poolType)
    : _poolType(poolType)
{
    auto it = _pools.find(poolType);
    if (it != _pools.end() && !it->second.empty())
    {
        _db = std::move(it->second.back());
        it->second.pop_back();
    }
    // TODO：这里之后需要做一件事，也就是当池中为空时，需要向其中添加新的指针
    // 上限暂时确定为10个，因为当前还是个小项目
    // 等项目需求变大之后再调整。如果到达上限，但是当前无空闲指针的话，那么异步等待await，这个等之后完善多线程时在做
}

DatabasePool::~DatabasePool()
{
    release();
}

void DatabasePool::release()
{
    if (!_db)
    {
        return;
    }

    auto it = _pools.find(_poolType);
    if (it != _pools.end())
    {
        it->second.push_back(std::move(_db));
        return;
    }

    _db->disconnect();
}

std::vector<QueryResult> DatabasePool::init(PoolType poolType, int num, DatabaseConfig config)
{
    std::vector<QueryResult> result = {};
    // 首先根据num初始化函数
    num = num > maxPoolNum ? maxPoolNum : num;
    // 没有该类型的池再进行初始化，否则不进行初始化
    if (_pools.find(poolType) == _pools.end())
    {
        std::vector<std::unique_ptr<IDatabase>> databasePool;
        switch (poolType)
        {
        // 先只完善MySQL的初始化，其它的按需补充
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
        default:
            break;
        }
        if (!databasePool.empty())
            _pools[poolType] = std::move(databasePool);
    }
    return result;
}

std::vector<QueryResult> DatabasePool::free(PoolType poolType)
{
    std::vector<QueryResult> results;
    auto it = _pools.find(poolType);
    if (it != _pools.end())
    {
        for (auto &database : it->second)
        {
            if (database)
            {
                results.push_back(database->disconnect());
            }
        }

        _pools.erase(it); // 移除该池
    }

    return results;
}

std::vector<QueryResult> DatabasePool::freeAll()
{
    std::vector<QueryResult> results;

    for (auto &pool : _pools)
    {
        for (auto &database : pool.second)
        {
            if (database)
            {
                results.push_back(database->disconnect());
            }
        }
    }

    _pools.clear();
    return results;
}