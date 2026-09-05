#ifndef INKING_BACKEND_FRAMEWORK_SENSE_SENSE_DATA_SERVICE_H
#define INKING_BACKEND_FRAMEWORK_SENSE_SENSE_DATA_SERVICE_H

#include "basic/ShineBasicModule.h"
#include "net/NetMessage.h"
#include "sense/ISensorDataStore.h"

#include <memory>
#include <optional>
#include <string>

inline constexpr const char *kSenseDataServiceModuleName = "SenseDataService";

/**
 * @brief 传感数据业务服务。
 *
 * 负责把 TCP 收到的协议消息翻译成数据层调用，并生成协议响应：
 * - GET_TIME/             -> SEND_TIME/<Unix毫秒>
 * - GET_DATA_MINUTE/      -> SEND_DATA_TABEL/[10湿度][10温度][起始毫秒]
 * - GET_DATA_HOUR/        -> 同上（1 小时/点）
 * - GET_DATA_DAY/         -> 同上（1 天/点）
 * - SEND_SENSE_DATA/t,h   -> 入库（按测试程序约定不回包）
 */
class SenseDataService : public ShineBasicModule
{
public:
    SenseDataService();
    ~SenseDataService() override;

    std::string moduleName() const override
    {
        return kSenseDataServiceModuleName;
    }

    /**
     * @brief 选择数据后端并初始化。
     * 编译开启 MySQL 且连接成功时使用 MySQLSensorDataStore；
     * 否则回退 FakeSensorDataStore（本地假数据），保证随时可以联调。
     */
    void initialize();

    /** 处理一条协议请求，返回协议响应；空 optional 表示不回包 */
    std::optional<NetMessage> handle(const NetMessage &request);

    /** 当前数据后端名称 */
    std::string backendName() const;
    /** 当前数据后端摘要 */
    std::string storeSummary() const;

private:
    std::optional<NetMessage> handleGetTime() const;
    std::optional<NetMessage> handleGetData(const std::string &command) const;
    std::optional<NetMessage> handleUploadData(const std::string &data) const;
    std::string buildTableText(const SenseSeries &series) const;

    std::unique_ptr<ISensorDataStore> _store;
};

#endif // INKING_BACKEND_FRAMEWORK_SENSE_SENSE_DATA_SERVICE_H
