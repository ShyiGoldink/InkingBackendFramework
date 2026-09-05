#ifndef INKING_BACKEND_FRAMEWORK_SENSE_ISENSOR_DATA_STORE_H
#define INKING_BACKEND_FRAMEWORK_SENSE_ISENSOR_DATA_STORE_H

#include "dataStruct/SenseStruct.h"

#include <string>

/**
 * @brief 传感器数据存取抽象层。
 *
 * 后端程序内部通过该接口读写数据，具体后端可替换：
 * - MySQLSensorDataStore：部署到云服务器时使用真实 MySQL；
 * - FakeSensorDataStore：本地没有数据库时的内存假数据，便于联调。
 */
class ISensorDataStore
{
public:
    virtual ~ISensorDataStore() = default;

    /** 保存一条传感器上报数据 */
    virtual bool insertSample(const SenseSample &sample) = 0;

    /** 按页面粒度取最近 pointCount 个点（从旧到新） */
    virtual SenseQueryResult querySeries(SenseSeriesPeriod period, int pointCount) = 0;

    /** 当前后端名称，用于控制台展示 */
    virtual std::string backendName() const = 0;

    /** 当前数据量等摘要，用于控制台展示 */
    virtual std::string summary() const = 0;
};

#endif // INKING_BACKEND_FRAMEWORK_SENSE_ISENSOR_DATA_STORE_H
