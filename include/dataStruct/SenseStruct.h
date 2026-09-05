#ifndef INKING_BACKEND_FRAMEWORK_DATA_STRUCT_SENSE_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_DATA_STRUCT_SENSE_STRUCT_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 传感数据的页面粒度。
 *
 * 对应硬件端 GET_DATA_MINUTE / GET_DATA_HOUR / GET_DATA_DAY 三张表格：
 * MINUTE 每个点是 1 分钟，HOUR 每个点是 1 小时，DAY 每个点是 1 天。
 * 客户端收到起始时间戳后，会按页面粒度自行推导后续点的时间戳。
 */
enum class SenseSeriesPeriod
{
    Minute,
    Hour,
    Day,
};

/** 每张表固定 10 个点 */
inline constexpr int kSenseSeriesPointCount = 10;

/** 每个页面粒度中“点与点之间”的毫秒数 */
inline int64_t sensePeriodBucketMilliseconds(SenseSeriesPeriod period)
{
    switch (period)
    {
    case SenseSeriesPeriod::Minute:
        return 60LL * 1000;
    case SenseSeriesPeriod::Hour:
        return 60LL * 60 * 1000;
    case SenseSeriesPeriod::Day:
        return 24LL * 60 * 60 * 1000;
    }
    return 60LL * 1000;
}

/** 当前 Unix 时间戳（毫秒） */
inline int64_t currentEpochMilliseconds()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/** 一条传感器上报数据 */
struct SenseSample
{
    double temperature = 0.0; /** 温度（℃） */
    double humidity = 0.0;    /** 湿度（%） */
    int64_t timestampMs = 0;  /** 采集时间（Unix 毫秒） */
};

/** 一页表格数据：湿度数组 / 温度数组 / 起始时间戳 */
struct SenseSeries
{
    std::vector<double> humidity;   /** 升序，与 temperature 等长 */
    std::vector<double> temperature;
    int64_t startMs = 0;            /** 第一个点的时间戳（毫秒） */
};

/** 取数结果 */
struct SenseQueryResult
{
    bool success = false;
    std::string error;
    SenseSeries series;
};

#endif // INKING_BACKEND_FRAMEWORK_DATA_STRUCT_SENSE_STRUCT_H
