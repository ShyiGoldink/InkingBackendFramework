#include "database/DatabaseManager.h"
#include "tool/JsonTool.h"
#include "tool/PathFindTool.h"
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
    DatabaseConfig config;
    if (!loadDatabaseConfig(config))
    {
        setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", false, "加载数据库配置失败");
        return;
    }
    // 连接数据库
    QueryResult result = _dp.connect(
        config.host,
        config.port,
        config.userName,
        config.password,
        config.databaseName);
    result.printResult();
    setStageStatus(STAGE_INIT_DATABASE, "初始化数据库", result.success, result.errorMessage);
}

// 增删改数据库
void DatabaseManager::execute(const std::vector<std::string> &args)
{
    auto arg = vTransTos(args);
    QueryResult result = _dp.execute(arg);
    result.printResult();
    setStageStatus(STAGE_EXECUTE_DATABASE, "增/删/改数据库", result.success, result.errorMessage);
}

// 查数据库
void DatabaseManager::query(const std::vector<std::string> &args)
{
    auto arg = vTransTos(args);
    QueryResult result = _dp.query(arg);
    result.printResult();
    setStageStatus(STAGE_QUERY_DATABASE, "查数据库", result.success, result.errorMessage);
}

// 断开数据库的连接
void DatabaseManager::disconnectDatabase()
{
    QueryResult result = _dp.disconnect();
    result.printResult();
    setStageStatus(STAGE_DISCONNECT_DATABASE, "数据库断开连接", result.success, result.errorMessage);
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