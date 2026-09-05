#include "sense/MySQLSensorDataStore.h"

#ifdef INKING_ENABLE_MYSQL

#include "basic/ShineLog.h"
#include "database/DatabasePool.h"
#include "database/IDatabase.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace
{
    constexpr const char *kDeviceId = "default";

    std::string formatNumber(double value)
    {
        std::ostringstream builder;
        builder << std::fixed << std::setprecision(4) << value;
        return builder.str();
    }

    QueryResult runExecute(const std::string &sql)
    {
        DatabasePool pool(PoolType::MySQL);
        if (!pool.valid())
        {
            return {false, "MySQL 连接不可用"};
        }
        return pool->execute(sql);
    }

    QueryResult runQuery(const std::string &sql)
    {
        DatabasePool pool(PoolType::MySQL);
        if (!pool.valid())
        {
            return {false, "MySQL 连接不可用"};
        }
        return pool->query(sql);
    }
}

bool MySQLSensorDataStore::initialize()
{
    if (_ready)
    {
        return true;
    }

    try
    {
        const QueryResult result = runExecute(R"SQL(
CREATE TABLE IF NOT EXISTS sense_data (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    device_id VARCHAR(64) NOT NULL DEFAULT 'default',
    temperature DECIMAL(6,2) NOT NULL COMMENT '温度(℃)',
    humidity DECIMAL(6,2) NOT NULL COMMENT '湿度(%)',
    sample_time BIGINT NOT NULL COMMENT '采集时间(Unix毫秒)',
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    KEY idx_device_sample_time (device_id, sample_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='传感器上报数据'
)SQL");
        if (!result.success)
        {
            _initError = "建表失败: " + result.errorMessage;
            ShineLog::error("MySQLSensorDataStore", _initError);
            return false;
        }
        _ready = true;
        return true;
    }
    catch (const std::exception &exception)
    {
        _initError = "MySQL 后端初始化异常: " + std::string(exception.what());
        ShineLog::error("MySQLSensorDataStore", _initError);
        return false;
    }
}

bool MySQLSensorDataStore::insertSample(const SenseSample &sample)
{
    if (!_ready)
    {
        ShineLog::error("MySQLSensorDataStore", "后端尚未初始化");
        return false;
    }

    try
    {
        std::ostringstream sql;
        sql << "INSERT INTO sense_data (device_id, temperature, humidity, sample_time) VALUES ('"
            << kDeviceId << "', "
            << formatNumber(sample.temperature) << ", "
            << formatNumber(sample.humidity) << ", "
            << sample.timestampMs << ")";
        const QueryResult result = runExecute(sql.str());
        if (!result.success)
        {
            ShineLog::error("MySQLSensorDataStore", "写入失败: " + result.errorMessage);
            return false;
        }
        return true;
    }
    catch (const std::exception &exception)
    {
        ShineLog::error("MySQLSensorDataStore", "写入异常: " + std::string(exception.what()));
        return false;
    }
}

SenseQueryResult MySQLSensorDataStore::querySeries(SenseSeriesPeriod period, int pointCount)
{
    SenseQueryResult result;
    if (!_ready)
    {
        result.error = "MySQL 后端尚未初始化";
        return result;
    }
    if (pointCount <= 0)
    {
        pointCount = kSenseSeriesPointCount;
    }

    try
    {
        const int64_t bucketMs = sensePeriodBucketMilliseconds(period);
        const int64_t nowMs = currentEpochMilliseconds();
        const int64_t endBucket = (nowMs / bucketMs) * bucketMs;
        const int64_t startBucket = endBucket - (pointCount - 1) * bucketMs;

        std::ostringstream sql;
        sql << "SELECT FLOOR(sample_time/" << bucketMs << ")*" << bucketMs
            << " AS bucket, AVG(temperature), AVG(humidity) FROM sense_data"
            << " WHERE device_id='" << kDeviceId
            << "' AND sample_time >= " << startBucket
            << " AND sample_time < " << (endBucket + bucketMs)
            << " GROUP BY bucket ORDER BY bucket ASC";

        const QueryResult query = runQuery(sql.str());
        if (!query.success)
        {
            result.error = query.errorMessage;
            return result;
        }

        // 桶起点 -> (温度, 湿度)
        std::map<int64_t, std::pair<double, double>> bucketValues;
        for (const auto &row : query.rows)
        {
            if (row.columns.size() < 3)
            {
                continue;
            }
            char *endPointer = nullptr;
            const int64_t bucket = std::strtoll(row.columns[0].c_str(), &endPointer, 10);
            const double temperature = std::stod(row.columns[1]);
            const double humidity = std::stod(row.columns[2]);
            bucketValues[bucket] = {temperature, humidity};
        }

        SenseSeries &series = result.series;
        series.startMs = startBucket;

        double lastTemperature = 0.0;
        double lastHumidity = 0.0;
        for (int64_t bucket = startBucket; bucket <= endBucket; bucket += bucketMs)
        {
            const auto it = bucketValues.find(bucket);
            if (it != bucketValues.end())
            {
                lastTemperature = it->second.first;
                lastHumidity = it->second.second;
            }
            series.temperature.push_back(lastTemperature);
            series.humidity.push_back(lastHumidity);
        }

        result.success = true;
        return result;
    }
    catch (const std::exception &exception)
    {
        result.error = "查询异常: " + std::string(exception.what());
        return result;
    }
}

std::string MySQLSensorDataStore::backendName() const
{
    return "MySQLSensorDataStore（MySQL sense_data 表）";
}

std::string MySQLSensorDataStore::summary() const
{
    try
    {
        const QueryResult result = runQuery("SELECT COUNT(*) FROM sense_data");
        if (result.success && !result.rows.empty() && !result.rows[0].columns.empty())
        {
            return "sense_data 共 " + result.rows[0].columns[0] + " 条";
        }
        return "sense_data 查询失败";
    }
    catch (const std::exception &exception)
    {
        return std::string("sense_data 查询异常: ") + exception.what();
    }
}

#else

// 未开启 MySQL 时给出明确提示（本文件仍会被编译，但不会真正连接数据库）
namespace
{
    constexpr const char *kMySqlDisabledReason = "当前构建未开启 INKING_ENABLE_MYSQL，请使用 FakeSensorDataStore";
}

bool MySQLSensorDataStore::initialize()
{
    _initError = kMySqlDisabledReason;
    return false;
}

bool MySQLSensorDataStore::insertSample(const SenseSample &)
{
    return false;
}

SenseQueryResult MySQLSensorDataStore::querySeries(SenseSeriesPeriod, int)
{
    SenseQueryResult result;
    result.error = kMySqlDisabledReason;
    return result;
}

std::string MySQLSensorDataStore::backendName() const
{
    return "MySQLSensorDataStore（未启用）";
}

std::string MySQLSensorDataStore::summary() const
{
    return kMySqlDisabledReason;
}

#endif // INKING_ENABLE_MYSQL
