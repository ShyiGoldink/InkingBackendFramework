#include "database/DatabasePool.h"
#include "database/IDatabase.h"
#include "basic/ShineLog.h"
#include "thread/TaskQueueLoop.h"

#ifdef INKING_ENABLE_MYSQL
#include "database/MySQL/MySQLDatabase.h"
#endif

#include <atomic>

std::unordered_map<PoolType, std::vector<PooledConnection>> DatabasePool::_pools;
std::unordered_map<PoolType, DatabaseConfig> DatabasePool::_configs;
std::unordered_map<PoolType, int> DatabasePool::_total;
std::unordered_map<PoolType, uint64_t> DatabasePool::_generations;
std::mutex DatabasePool::_mutex;
std::condition_variable DatabasePool::_condition;
std::chrono::milliseconds DatabasePool::_borrowTimeout{5000};
std::chrono::milliseconds DatabasePool::_maxIdleTime{60000};
std::chrono::milliseconds DatabasePool::_probeIdleThreshold{0}; // 0 = 每次借出都探活

namespace
{
constexpr auto kMaintainInterval = std::chrono::seconds(30);/** 后台维护的间隔 */
constexpr int kMaxIdleAttempts = 4;/** 单次借出最多连续丢弃并重试的坏连接条数 */

/** 后台维护任务只排一次，用这个标志保证不会重复排队 */
std::atomic<bool> gMaintenanceScheduled{false};

Task<std::any> makeMaintenanceTask();

/** 确保后台维护任务已经排上（只排一次） */
void ensureMaintenanceScheduled()
{
    if (gMaintenanceScheduled.exchange(true))
    {
        return;
    }
    TaskQueueLoop::instance().addTask(makeMaintenanceTask(), kMaintainInterval);
}

/**
 * 后台维护任务：清理所有池里空闲过久的连接，然后把自己排下一次。
 * 这就是一个跑在线程池里的定时器——不额外起线程，也不占着借出路径。
 */
Task<std::any> makeMaintenanceTask()
{
    Task<std::any> task;
    task.action = [](const std::vector<std::any> &) -> std::any
    {
        DatabasePool::maintainAllIdle();
        TaskQueueLoop::instance().addTask(makeMaintenanceTask(), kMaintainInterval);
        return {};
    };
    return task;
}
}

// 构造函数，构造成功的同时就取一个指针作为对象
DatabasePool::DatabasePool(PoolType poolType)
    : _poolType(poolType)
{
    std::unique_lock<std::mutex> lock(_mutex);

    // 池里的空闲连接可能已经不可用了（服务端杀空闲连接、网络中断），
    // 所以每次借出都要确认：先看免费的本地标记，再做一次 ping。
    // ping 有一次网络往返，但它比"借到坏连接再重连"便宜两个数量级，
    // 所以默认不做省这笔开销的猜测，直接每次都问一遍。
    for (int attempt = 0; attempt < kMaxIdleAttempts; ++attempt)
    {
        if (!takeIdleLocked())
        {
            break; // 没有空闲连接，交给下面的扩容/等待逻辑
        }

        IDatabase *candidate = _db.get();
        const auto idleFor = std::chrono::steady_clock::now() - _idleSince;
        const bool needProbe = idleFor >= _probeIdleThreshold;

        // ping 是一次网络往返，必须在锁外做，否则所有借出会被这条连接串行化
        lock.unlock();
        const bool alive = !candidate->isBroken() && (!needProbe || candidate->ping().success);
        lock.lock();

        if (alive)
        {
            return;
        }

        // 坏连接不回池：断开它、把存活数减一，然后在锁外真正断开，再继续借下一条
        std::unique_ptr<IDatabase> dead = std::move(_db);
        if (_total[_poolType] > 0)
        {
            --_total[_poolType];
        }
        lock.unlock();
        dead.reset();
        lock.lock();
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

     PooledConnection &pooled = it->second.back();
     _db = std::move(pooled.database);
      _idleSince = pooled.idleSince;
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

    // 建连期间有连接被归还：优先借出归还的连接，把新建的连接放入池中。
    // 顺手借出的那条不做探活，因为它刚刚才被归还（空闲≈0），按规则本来就轮不到 ping。
    if (takeIdleLocked())
    {
        _pools[_poolType].push_back(PooledConnection{std::move(fresh), std::chrono::steady_clock::now()});
        _condition.notify_one();
        return true;
    }

    _db = std::move(fresh);
    _generation = poolGeneration;
    _idleSince = std::chrono::steady_clock::now(); // 新建的连接，视作刚刚进入可用状态
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

    // 池还在、代次一致，并且这条连接没有被标记为损坏才允许归还。
    // isBroken() 是零往返的本地判断，不会给归还路径增加网络开销。
    const bool poolAlive = it != _pools.end() && _generations[_poolType] == _generation;
    if (poolAlive && !_db->isBroken())
    {
        it->second.push_back(PooledConnection{std::move(_db), std::chrono::steady_clock::now()});
        _condition.notify_one();
        return;
    }

    database = std::move(_db);
    if (poolAlive && _total[_poolType] > 0)
    {
        --_total[_poolType]; // 坏连接不再回池，存活数要相应减一
    }
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
        std::vector<PooledConnection> databasePool;
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
                    databasePool.push_back(PooledConnection{std::move(mySQL), std::chrono::steady_clock::now()});
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
            ensureMaintenanceScheduled(); // 池建起来了，把空闲清理的后台任务排上
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

        for (auto &pooled : it->second)
        {
            if (pooled.database)
            {
                databases.push_back(std::move(pooled.database));
            }
        }
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
            for (auto &pooled : pool.second)
            {
                if (pooled.database)
                {
                    databases.push_back(std::move(pooled.database));
                }
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

        auto &idle = it->second;
        const auto now = std::chrono::steady_clock::now();

        auto totalIt = _total.find(poolType);
        const auto releaseTotal = [&totalIt]()
        {
            if (totalIt != _total.end() && totalIt->second > 0)
            {
                --totalIt->second;
            }
        };

        // 先按空闲时长淘汰：归还时是 push_back，所以越靠前的连接空闲越久。
        // 这一步让连接在被服务端/NAT 静默杀掉之前就主动断开，而不是等借出时才发现是坏的。
        while (!idle.empty() && now - idle.front().idleSince >= _maxIdleTime)
        {
            databases.push_back(std::move(idle.front().database));
            idle.erase(idle.begin());
            releaseTotal();
            ++removed;
        }

        // 再按数量裁：只保留一般上限内的空闲连接，挤出去的是最新归还的那几条
        int excess = static_cast<int>(idle.size()) - maxPoolNum;
        while (excess-- > 0 && !idle.empty())
        {
            databases.push_back(std::move(idle.back().database));
            idle.pop_back();
            releaseTotal();
            ++removed;
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

void DatabasePool::setMaxIdleTime(const std::chrono::milliseconds &timeout)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _maxIdleTime = timeout;
}

void DatabasePool::setProbeIdleThreshold(const std::chrono::milliseconds &threshold)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _probeIdleThreshold = threshold;
}

int DatabasePool::maintainAllIdle()
{
    // 先取出当前有哪些池，再逐个清理。
    // trimIdle 内部自己会加锁，所以这里不能持着锁去调它。
    std::vector<PoolType> types;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        types.reserve(_pools.size());
        for (const auto &pool : _pools)
        {
            types.push_back(pool.first);
        }
    }

    int removed = 0;
    for (PoolType type : types)
    {
        removed += trimIdle(type);
    }
    return removed;
}
