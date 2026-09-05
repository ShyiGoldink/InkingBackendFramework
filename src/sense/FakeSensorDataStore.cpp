#include "sense/FakeSensorDataStore.h"

#include <cmath>
#include <sstream>

namespace
{
    /** 生成多少天的分钟级数据 */
    constexpr int kSeedDays = 15;

    double fakeTemperature(int index)
    {
        return 26.0 + 3.0 * std::sin(index / 110.0) + 0.7 * std::sin(index / 17.0);
    }

    double fakeHumidity(int index)
    {
        return 54.0 + 11.0 * std::cos(index / 150.0) + 2.0 * std::cos(index / 29.0);
    }
}

bool FakeSensorDataStore::insertSample(const SenseSample &sample)
{
    std::lock_guard<std::mutex> lock(_mutex);
    seedIfEmpty();

    // 内部结构按“整分钟”聚合，把上报值并入对应分钟
    const int64_t minuteStart = (sample.timestampMs / 60000LL) * 60000LL;
    auto it = _minuteData.find(minuteStart);
    if (it == _minuteData.end())
    {
        _minuteData[minuteStart] = sample;
    }
    else
    {
        // 同一分钟多条上报时取平均值，模拟真实后端的分钟聚合
        it->second.temperature = (it->second.temperature + sample.temperature) / 2.0;
        it->second.humidity = (it->second.humidity + sample.humidity) / 2.0;
    }
    return true;
}

SenseQueryResult FakeSensorDataStore::querySeries(SenseSeriesPeriod period, int pointCount)
{
    std::lock_guard<std::mutex> lock(_mutex);
    seedIfEmpty();

    SenseQueryResult result;
    if (pointCount <= 0)
    {
        pointCount = kSenseSeriesPointCount;
    }

    const int64_t bucketMs = sensePeriodBucketMilliseconds(period);
    const int64_t nowMs = currentEpochMilliseconds();
    const int64_t endBucket = (nowMs / bucketMs) * bucketMs;            // 当前所在桶
    const int64_t startBucket = endBucket - (pointCount - 1) * bucketMs;

    SenseSeries &series = result.series;
    series.startMs = startBucket;

    // 没有数据的桶沿用上一个值；最前面没有值时用 0
    double lastTemperature = 0.0;
    double lastHumidity = 0.0;
    for (int64_t bucket = startBucket; bucket <= endBucket; bucket += bucketMs)
    {
        double sumTemperature = 0.0;
        double sumHumidity = 0.0;
        int count = 0;

        // 聚合当前桶内的分钟数据（分钟粒度时桶内只有 1 条）
        const auto lower = _minuteData.lower_bound(bucket);
        const auto upper = _minuteData.lower_bound(bucket + bucketMs);
        for (auto it = lower; it != upper; ++it)
        {
            sumTemperature += it->second.temperature;
            sumHumidity += it->second.humidity;
            ++count;
        }
        if (count > 0)
        {
            lastTemperature = sumTemperature / count;
            lastHumidity = sumHumidity / count;
        }

        series.temperature.push_back(lastTemperature);
        series.humidity.push_back(lastHumidity);
    }

    result.success = true;
    return result;
}

std::string FakeSensorDataStore::backendName() const
{
    return "FakeSensorDataStore（本地内存假数据）";
}

std::string FakeSensorDataStore::summary() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::ostringstream builder;
    builder << "内存分钟数据 " << _minuteData.size() << " 条（约最近 " << kSeedDays << " 天）";
    return builder.str();
}

void FakeSensorDataStore::seedIfEmpty()
{
    if (!_minuteData.empty())
    {
        return;
    }

    const int64_t nowMinuteStart = (currentEpochMilliseconds() / 60000LL) * 60000LL;
    constexpr int totalMinutes = kSeedDays * 1440;
    for (int index = 0; index <= totalMinutes; ++index)
    {
        const int64_t timestampMs = nowMinuteStart - (totalMinutes - index) * 60000LL;
        SenseSample sample;
        sample.temperature = fakeTemperature(index);
        sample.humidity = fakeHumidity(index);
        sample.timestampMs = timestampMs;
        _minuteData[timestampMs] = sample;
    }
}
