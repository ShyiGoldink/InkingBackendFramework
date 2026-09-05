#ifndef INKING_BACKEND_FRAMEWORK_SENSE_MYSQL_SENSOR_DATA_STORE_H
#define INKING_BACKEND_FRAMEWORK_SENSE_MYSQL_SENSOR_DATA_STORE_H

#include "sense/ISensorDataStore.h"

/**
 * @brief MySQL 真实数据后端（仅 INKING_ENABLE_MYSQL=ON 时可用）。
 *
 * 对应 sql/sense_schema.sql 中的 sense_data 表。
 * 分钟/小时/天表格通过 SQL 对原始数据按时间桶聚合得到。
 */
class MySQLSensorDataStore : public ISensorDataStore
{
public:
    /** 尝试建表并确认连接池可用；成功后才可读写 */
    bool initialize();

    bool insertSample(const SenseSample &sample) override;
    SenseQueryResult querySeries(SenseSeriesPeriod period, int pointCount) override;
    std::string backendName() const override;
    std::string summary() const override;

private:
    bool _ready = false;
    std::string _initError;
};

#endif // INKING_BACKEND_FRAMEWORK_SENSE_MYSQL_SENSOR_DATA_STORE_H
