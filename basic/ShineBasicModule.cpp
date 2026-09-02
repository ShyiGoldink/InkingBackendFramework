#include "ShineStatusChecker.h"
#include "ShineBasicModule.h"
#include "ShineLog.h"
#include "ui/UIMessageLibrary.h"

#include <iostream>
#include <sstream>

ShineBasicModule::ShineBasicModule() = default;

ShineBasicModule::~ShineBasicModule() = default;

std::vector<Stage> ShineBasicModule::getStage() const
{
    return _stage;
}

void ShineBasicModule::setStageStatus(int step, const std::string &name, bool statu, const std::string &message)
{ // 首先找到stage
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
    // 如果stage存在，并且name与当前修改的name不一致，那么写入错误日志并拒绝更新
    else if (!stage->name.empty() && stage->name != name)
    {
        ShineLog::error(
            moduleName(),
            "自检阶段编号冲突: step " + std::to_string(step) + " 已注册为 " + stage->name + "，不能重新注册为 " + name);
        return;
    }
    // 如果stage存在但是name为空，那么更新stage的name为当前修改的name
    else if (stage->name.empty())
    {
        stage->name = name;
    }
    // 最后更新状态和消息
    stage->status = statu;
    stage->message = message;

    const std::string logMessage = stage->name + " - " + message;
    // 根据状态写入日志
    if (statu)
    {
        ShineLog::pass(moduleName(), logMessage);
    }
    else
    {
        ShineLog::error(moduleName(), logMessage);
    }
}

void ShineBasicModule::setStageDetail(int step, const std::string &description, const std::string &suggestion)
{ // 首先找到stage
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
