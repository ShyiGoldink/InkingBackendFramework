#include "ShineBasicModule.h"
#include "ShineLog.h"

ShineBasicModule::ShineBasicModule() = default;

ShineBasicModule::~ShineBasicModule() = default;

void ShineBasicModule::addStage(const std::string& stageName, const std::string& description)
{
    Stage stage;
    stage.name = stageName;
    stage.description = description;

    _stage[stageName] = stage;
}

void ShineBasicModule::setStageStatus(const std::string& stageName, bool status, const std::string& message)
{
    auto iter = _stage.find(stageName);
    if (iter == _stage.end()) {
        ShineLog::error(moduleName(), "未注册的自检阶段: " + stageName);
        return;
    }

    iter->second.status = status;
    iter->second.error = status ? "" : message;

    const std::string logMessage = iter->second.name + " - " + message;
    if (status) {
        if (stageName == "SAY_HELLO") {
            ShineLog::blue(moduleName(), logMessage);
        } else {
            ShineLog::pass(moduleName(), logMessage);
        }
    } else {
        ShineLog::error(moduleName(), logMessage);
    }
}
