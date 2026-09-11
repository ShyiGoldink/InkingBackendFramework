#include "ShineStatusChecker.h"
#include "ShineBasicModule.h"
#include "ShineLog.h"
#include "ui/UIMessageLibrary.h"

#include <algorithm>
#include <iostream>
#include <sstream>

ShineBasicModule::ShineBasicModule() = default;

ShineBasicModule::~ShineBasicModule() = default;

std::vector<Stage> ShineBasicModule::getStage() const
{
    std::lock_guard<std::mutex> lock(_stageMutex);
    return _stage; // 拷贝一份返回，避免把内部指针暴露出去
}

void ShineBasicModule::setStageStatus(int step, const std::string &name, bool statu, const std::string &message)
{
    std::string logMessage;
    bool logAsError = !statu;

    {
        std::lock_guard<std::mutex> lock(_stageMutex);

        // 首先找到stage
        Stage *stage = findStage(step);
        // 如果stage不存在，那么创建stage
        if (stage == nullptr)
        {
            Stage newStage;
            newStage.step = step;
            newStage.name = name;
            _stage.push_back(newStage);
            stage = &_stage.back();
        }
        // 如果stage存在，并且name与当前修改的name不一致，那么记录错误日志并拒绝更新
        else if (!stage->name.empty() && stage->name != name)
        {
            logAsError = true;
            logMessage = "自检阶段编号冲突: step " + std::to_string(step) + " 已注册为 " + stage->name +
                         "，不能重新注册为 " + name;
        }
        // 如果stage存在但是name为空，那么更新stage的name为当前修改的name
        else if (stage->name.empty())
        {
            stage->name = name;
            stage->status = statu;
            stage->message = message;
            logMessage = stage->name + " - " + message;
        }
        else
        {
            stage->status = statu;
            stage->message = message;
            logMessage = stage->name + " - " + message;
        }

        std::stable_sort(_stage.begin(), _stage.end(), [](const Stage &left, const Stage &right)
                         { return left.step < right.step; });
    }

    // 日志写在锁外：日志模块自己有锁，两把锁不要嵌套
    if (logAsError)
    {
        ShineLog::error(moduleName(), logMessage);
    }
    else
    {
        ShineLog::pass(moduleName(), logMessage);
    }
}

void ShineBasicModule::setStageDetail(int step, const std::string &description, const std::string &suggestion)
{
    std::lock_guard<std::mutex> lock(_stageMutex);

    // 首先找到stage
    Stage *stage = findStage(step);
    // 如果stage不存在，那么创建stage
    if (stage == nullptr)
    {
        Stage newStage;
        newStage.step = step;
        _stage.push_back(newStage);
        stage = &_stage.back();
    }
    // 更新stage的描述和修改建议
    stage->description = description;
    stage->suggestion = suggestion;
}

Stage *ShineBasicModule::findStage(int step)
{
    for (auto &stage : _stage)
    {
        if (stage.step == step)
        {
            return &stage;
        }
    }

    return nullptr;
}

const Stage *ShineBasicModule::findStage(int step) const
{
    for (const auto &stage : _stage)
    {
        if (stage.step == step)
        {
            return &stage;
        }
    }

    return nullptr;
}

void ShineBasicModule::sayMyName() const
{
    std::ostringstream builder;
    builder << "[" << moduleName() << "@ 0x" << std::hex << (void *)this << std::dec << "]";
    const std::string text = builder.str();
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, text);
}
