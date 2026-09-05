#include "sense/SenseDataService.h"

#include "basic/ShineLog.h"
#include "sense/FakeSensorDataStore.h"
#include "ui/UIMessageLibrary.h"

#ifdef INKING_ENABLE_MYSQL
#include "database/DatabaseManager.h"
#include "database/DatabasePool.h"
#include "sense/MySQLSensorDataStore.h"
#endif

#include <cmath>
#include <sstream>

namespace
{
    constexpr int STAGE_INIT_STORE = 1;
    constexpr int STAGE_HANDLE_MESSAGE = 2;

    /**
     * 上传命令是否回包。
     * D:\HarmonyTest\test_server.py 的约定是“上传通常不等回执”，不回包不视为失败；
     * 若硬件端确认上传也需要响应，把这里改成 true 即可。
     */
    constexpr bool kReplyToUpload = false;

    std::string formatNumber(double value)
    {
        std::ostringstream builder;
        builder << value; // 默认 6 位有效数字，避免多余的尾随 0
        return builder.str();
    }

    /** 解析 SEND_SENSE_DATA 的 payload：温度,湿度（兼容英文/中文逗号） */
    bool parseUploadPayload(const std::string &data, double &temperature, double &humidity)
    {
        const std::size_t asciiComma = data.find(',');
        const std::size_t chineseComma = data.find("，");

        std::size_t separator = std::string::npos;
        if (asciiComma == std::string::npos)
        {
            separator = chineseComma;
        }
        else if (chineseComma == std::string::npos || asciiComma < chineseComma)
        {
            separator = asciiComma;
        }
        else
        {
            separator = chineseComma;
        }
        if (separator == std::string::npos)
        {
            return false;
        }

        try
        {
            const std::string left = data.substr(0, separator);
            const std::string right = data.substr(separator + 1);
            if (left.empty() || right.empty())
            {
                return false;
            }
            temperature = std::stod(left);
            humidity = std::stod(right);
        }
        catch (...)
        {
            return false;
        }
        return std::isfinite(temperature) && std::isfinite(humidity);
    }
}

SenseDataService::SenseDataService()
{
    setStageDetail(STAGE_INIT_STORE, "选择传感器数据后端并初始化", "MySQL 连不上时自动回退本地假数据，不影响联调。");
    setStageDetail(STAGE_HANDLE_MESSAGE, "处理传感协议消息", "请确认协议命令与硬件端保持一致。");
    registerToStatusChecker();
}

SenseDataService::~SenseDataService() = default;

void SenseDataService::initialize()
{
#ifdef INKING_ENABLE_MYSQL
    // 尝试使用真实 MySQL：先初始化连接池，成功再启用 MySQL 后端
    DatabaseManager::instance().initDatabase();
    if (DatabasePool::info(PoolType::MySQL).total > 0)
    {
        auto mysqlStore = std::make_unique<MySQLSensorDataStore>();
        if (mysqlStore->initialize())
        {
            _store = std::move(mysqlStore);
            setStageStatus(STAGE_INIT_STORE, "初始化传感数据后端", true, "使用 MySQL");
            UIMessageLibrary::addMessage(MessageType::pass, 0.0f, "传感数据后端: MySQL");
            return;
        }
        ShineLog::error(kSenseDataServiceModuleName, "MySQL 后端初始化失败，回退本地假数据");
    }
#endif

    _store = std::make_unique<FakeSensorDataStore>();
    setStageStatus(STAGE_INIT_STORE, "初始化传感数据后端", true, "使用本地内存假数据（未连接数据库）");
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f,
                                 "未连接数据库，传感数据后端使用本地假数据 FakeSensorDataStore");
}

std::optional<NetMessage> SenseDataService::handle(const NetMessage &request)
{
    setStageStatus(STAGE_HANDLE_MESSAGE, "处理传感协议消息", true,
                   "收到 " + request.command + " 请求");

    if (request.command == "GET_TIME")
    {
        return handleGetTime();
    }
    if (request.command == "GET_DATA_MINUTE" ||
        request.command == "GET_DATA_HOUR" ||
        request.command == "GET_DATA_DAY")
    {
        return handleGetData(request.command);
    }
    if (request.command == "SEND_SENSE_DATA")
    {
        return handleUploadData(request.data);
    }

    ShineLog::error(kSenseDataServiceModuleName, "未识别的协议指令: " + request.command);
    return std::nullopt;
}

std::string SenseDataService::backendName() const
{
    return _store ? _store->backendName() : "尚未初始化";
}

std::string SenseDataService::storeSummary() const
{
    return _store ? _store->summary() : "尚未初始化";
}

std::optional<NetMessage> SenseDataService::handleGetTime() const
{
    return NetMessage{"SEND_TIME", std::to_string(currentEpochMilliseconds())};
}

std::optional<NetMessage> SenseDataService::handleGetData(const std::string &command) const
{
    if (!_store)
    {
        ShineLog::error(kSenseDataServiceModuleName, "数据后端尚未初始化");
        return std::nullopt;
    }

    SenseSeriesPeriod period = SenseSeriesPeriod::Minute;
    if (command == "GET_DATA_HOUR")
    {
        period = SenseSeriesPeriod::Hour;
    }
    else if (command == "GET_DATA_DAY")
    {
        period = SenseSeriesPeriod::Day;
    }

    const SenseQueryResult result = _store->querySeries(period, kSenseSeriesPointCount);
    if (!result.success)
    {
        ShineLog::error(kSenseDataServiceModuleName,
                        command + " 查询失败: " + result.error);
        return std::nullopt;
    }

    const std::string payload = buildTableText(result.series);
    ShineLog::write(kSenseDataServiceModuleName,
                    command + " 返回 " + std::to_string(result.series.humidity.size()) + " 个点");
    return NetMessage{"SEND_DATA_TABEL", payload};
}

std::optional<NetMessage> SenseDataService::handleUploadData(const std::string &data) const
{
    double temperature = 0.0;
    double humidity = 0.0;
    if (!parseUploadPayload(data, temperature, humidity))
    {
        ShineLog::error(kSenseDataServiceModuleName,
                        "SEND_SENSE_DATA 数据格式错误: " + data);
        return std::nullopt;
    }

    SenseSample sample;
    sample.temperature = temperature;
    sample.humidity = humidity;
    sample.timestampMs = currentEpochMilliseconds();

    const bool saved = _store && _store->insertSample(sample);
    ShineLog::write(kSenseDataServiceModuleName,
                    "收到上报 温度=" + std::to_string(temperature) +
                        " 湿度=" + std::to_string(humidity) +
                        " 入库=" + (saved ? "成功" : "失败"));
    UIMessageLibrary::addMessage(
        saved ? MessageType::pass : MessageType::error, 0.0f,
        std::string("传感器上报: ") + (saved ? "入库成功" : "入库失败") +
            " temp=" + formatNumber(temperature) +
            " humidity=" + formatNumber(humidity));

    if (kReplyToUpload)
    {
        return NetMessage{"SEND_SENSE_DATA", saved ? "ok" : "error"};
    }
    return std::nullopt;
}

std::string SenseDataService::buildTableText(const SenseSeries &series) const
{
    const auto joinValues = [](const std::vector<double> &values)
    {
        std::string text;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
            {
                text += ",";
            }
            text += formatNumber(values[i]);
        }
        return text;
    };

    return "[" + joinValues(series.humidity) + "]" +
           "[" + joinValues(series.temperature) + "]" +
           "[" + std::to_string(series.startMs) + "]";
}
