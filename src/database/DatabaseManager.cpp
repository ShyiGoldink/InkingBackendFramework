#include "database/DatabaseManager.h"
#include "database/IDatabase.h"
#include "tool/JsonTool.h"
#include "tool/PathFindTool.h"
#include "ui/UIMessageLibrary.h"

#include <sstream>
namespace
{
    constexpr int STAGE_LOAD_CONFIG = 1;
    constexpr int STAGE_INIT_DATABASE = 2;
    constexpr int STAGE_EXECUTE_DATABASE = 3;
    constexpr int STAGE_QUERY_DATABASE = 4;
    constexpr int STAGE_DISCONNECT_DATABASE = 5;
}

DatabaseManager::DatabaseManager()
{
    registerToStatusChecker();
}

void DatabaseManager::initDatabase()
{
    // 获取数据库配置，这里就只加载MySQL，如有需要让用户自定义添加数据库配置
    DatabaseConfig config;
    if (!loadDatabaseConfig(config))
    {
        setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", false, "加载数据库配置失败");
        return;
    }
    // 初始化数据库
    std::vector<QueryResult> results = DatabasePool::init(PoolType::MySQL, maxPoolNum, config);
    // 池已存在时 init 不会重复创建连接，返回空结果，这里直接按成功处理
    if (results.empty())
    {
        UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "连接池已在或未在DatabasePool实现任何IDatabase实例");
        setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", false, "连接池已初始化或未在DatabasePool实现任何IDatabase实例");
        setStageDetail(STAGE_INIT_DATABASE,"这里进行了数据库连接池的初始化","如果这里为false,代表连接池进行初始化时没有返回任何连接实例。\n这通常代表着下面两种情况之一：\n1.连接池已经进行过初始化，所以没有进行任何新的初始化\n2.在cmake中没有开启任何数据库的实例方案");
        return;
    }
    int total = 0;
    int success = 0;
    for (QueryResult &query : results)
    {
        total++;
        const std::string text = query.toString();
        const char *msg = text.c_str();
        UIMessageLibrary::quickMessage(query.success, 0.0f, msg);
        if (query.success)
        {
            success++;
        }
    }
    // 计算成功率并给出结果
    total = total == 0 ? 1 : total;
    float rate = static_cast<float>(success) / total;
    std::ostringstream initMessage;
    initMessage << "数据库初始化效果:" << rate;
    const std::string text = initMessage.str();
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, text);
    if (rate != 0)
        setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", true, "加载数据库配置成功");
    else
        setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", false, "加载数据库配置失败");
}

// 增删改数据库
void DatabaseManager::execute(PoolType poolType, const std::vector<std::string> &args)
{
    DatabasePool databasePool(poolType);
    auto arg = vTransTos(args);
    if (!databasePool.valid())
    {
        return;
    }
    QueryResult result = databasePool->execute(arg);
    const std::string text = result.toString();
    const char *msg = text.c_str();
    UIMessageLibrary::quickMessage(result.success, 0.0f, msg);
    setStageStatus(STAGE_EXECUTE_DATABASE, "增/删/改数据库", result.success, result.errorMessage);
}

// 查数据库
void DatabaseManager::query(PoolType poolType, const std::vector<std::string> &args)
{
    DatabasePool databasePool(poolType);
    auto arg = vTransTos(args);
    if (!databasePool.valid())
    {
        return;
    }
    QueryResult result = databasePool->query(arg);
    const std::string text = result.toString();
    const char *msg = text.c_str();
    UIMessageLibrary::quickMessage(result.success, 0.0f, msg);
    setStageStatus(STAGE_QUERY_DATABASE, "查数据库", result.success, result.errorMessage);
}

// 断开数据库的连接
void DatabaseManager::disconnectDatabase(PoolType poolType)
{
    std::vector<QueryResult> results = DatabasePool::free(poolType);
    std::string errorMessages = "";
    bool isSuccess = true;
    for (QueryResult queryResult : results)
    {
        isSuccess = isSuccess && queryResult.success;
        errorMessages = errorMessages + ";" + queryResult.errorMessage;
        const std::string text = queryResult.toString();
        const char *msg = text.c_str();
        UIMessageLibrary::quickMessage(queryResult.success, 0.0f, msg);
    }
    setStageStatus(STAGE_DISCONNECT_DATABASE, "数据库断开连接", isSuccess, errorMessages);
}

bool DatabaseManager::loadDatabaseConfig(DatabaseConfig &config)
{
    JsonTool jsonTool;
    PathFindTool pathFindTool;
    const auto json =
        jsonTool.loadFromFile(pathFindTool.configPath("databaseConfig.json").string());
    if (!json.has_value())
    {
        return false;
    }
    std::string configKey = "databaseConfig";
    const auto databaseConfigJson =
        jsonTool.getValue<JsonTool::Json>(*json, configKey);

    if (!databaseConfigJson.has_value())
    {
        setStageStatus(STAGE_LOAD_CONFIG, "加载数据库信息", false, "数据库配置缺少 databaseConfig 节点");
        setStageDetail(STAGE_LOAD_CONFIG, "加载数据库信息", "请检查 databaseConfig.json 文件，确保包含 databaseConfig 节点");
        return false;
    }

    config.host =
        jsonTool.getValue<std::string>(*databaseConfigJson, "host").value_or("localhost");
    config.port =
        jsonTool.getValue<int>(*databaseConfigJson, "port").value_or(3306);
    config.userName =
        jsonTool.getValue<std::string>(*databaseConfigJson, "userName").value_or("root");
    config.password =
        jsonTool.getValue<std::string>(*databaseConfigJson, "password").value_or("");
    config.databaseName =
        jsonTool.getValue<std::string>(*databaseConfigJson, "databaseName").value_or("test");

    setStageStatus(STAGE_LOAD_CONFIG, "加载数据库信息", true, "");
    return true;
}

std::string DatabaseManager::vTransTos(const std::vector<std::string> &args)
{
    std::string result;
    for (size_t i = 0; i < args.size(); ++i)
    {
        result += args[i];
        if (i < args.size() - 1)
        {
            result += " ";
        }
    }
    return result;
}
