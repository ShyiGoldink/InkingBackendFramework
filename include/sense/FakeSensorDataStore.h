#ifndef INKING_BACKEND_FRAMEWORK_SENSE_FAKE_SENSOR_DATA_STORE_H
#define INKING_BACKEND_FRAMEWORK_SENSE_FAKE_SENSOR_DATA_STORE_H

#include "sense/ISensorDataStore.h"

#include <map>
#include <mutex>

/**
 * @brief 本地内存假数据后端。
 *
 * 结构等价于 sense_data 按分钟聚合后的表：
 * 每个 key 是一个整分钟起点，value 是该分钟的温度/湿度。
 * 首次查询时自动生成最近约 15 天的平滑假数据；
 * 设备上传的新数据会并入当前分钟。
 * 线程安全，供本地无数据库联调使用。
 */
class FakeSensorDataStore : public ISensorDataStore
{
public:
    bool insertSample(const SenseSample &sample) override;
    SenseQueryResult querySeries(SenseSeriesPeriod period, int pointCount) override;
    std::string backendName() const override;
    std::string summary() const override;

private:
    /** 生成最近若干天的分钟级假数据（调用方需已持锁） */
    void seedIfEmpty();

    mutable std::mutex _mutex;
    std::map<int64_t, SenseSample> _minuteData; /** key：整分钟起点（ms） */
};

#endif // INKING_BACKEND_FRAMEWORK_SENSE_FAKE_SENSOR_DATA_STORE_H
