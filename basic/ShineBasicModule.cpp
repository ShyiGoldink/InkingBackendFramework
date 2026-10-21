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

void ShineBasicModule::setStageStatus(int step, const std::string &name, bool statu, const std::string &message,
                                      StageLogPolicy policy, const std::source_location &site)
{
    std::string logMessage;
    bool logAsError = false;
    bool shouldLog = false;

    {
        std::lock_guard<std::mutex> lock(_stageMutex);

        Stage *stage = findStage(step);
        const bool created = (stage == nullptr);

        if (created)
        {
            // 首建阶段：状态和消息必须一起写下来。
            // 旧版这里只填了 step 和 name 就返回，于是第一次日志永远是空的，
            // status 命令也会显示 "[通过] 打印启动提示 - " 这种没有内容的行。
            Stage newStage;
            newStage.step = step;
            newStage.name = name;
            newStage.status = statu;
            newStage.message = message;
            newStage.count = 1;
            newStage.firstSeen = ShineLog::timestamp();
            newStage.lastSeen = newStage.firstSeen;
            _stage.push_back(std::move(newStage));

            logAsError = !statu;
            shouldLog = (policy != StageLogPolicy::Silent) || !statu;

            std::ostringstream text;
            text << name << " | 已登记";
            if (!message.empty())
            {
                text << " | " << message;
            }
            logMessage = text.str();
        }
        else if (!stage->name.empty() && stage->name != name)
        {
            // 阶段编号冲突：拒绝更新，避免两个函数抢同一个 step 把状态互相覆盖
            logAsError = true;
            shouldLog = true;
            logMessage = "自检阶段编号冲突: step " + std::to_string(step) + " 已注册为 " + stage->name +
                         "，不能重新注册为 " + name;
        }
        else
        {
            const bool previousStatus = stage->status;
            const bool changed = (previousStatus != statu) || (stage->message != message);

            if (stage->name.empty())
            {
                stage->name = name;
            }
            stage->status = statu;
            stage->message = message;
            ++stage->count;
            stage->lastSeen = ShineLog::timestamp();
            if (stage->firstSeen.empty())
            {
                stage->firstSeen = stage->lastSeen;
            }

            switch (policy)
            {
            case StageLogPolicy::Always:
                shouldLog = true;
                break;
            case StageLogPolicy::Silent:
                // 静默只压成功流水；失败必须留证
                shouldLog = !statu;
                break;
            case StageLogPolicy::OnChange:
            default:
                shouldLog = !statu || changed;
                break;
            }

            logAsError = !statu;

            if (shouldLog)
            {
                std::ostringstream text;
                text << stage->name << " | ";
                if (previousStatus != statu)
                {
                    text << (previousStatus ? "通过 → 失败" : "失败 → 通过");
                }
                else
                {
                    text << (statu ? "通过" : "失败");
                }
                if (!message.empty())
                {
                    text << " | " << message;
                }
                logMessage = text.str();
            }
        }

        std::stable_sort(_stage.begin(), _stage.end(), [](const Stage &left, const Stage &right)
                         { return left.step < right.step; });
    }

    if (!shouldLog)
    {
        return;
    }

    // 兜底：任何情况下都不写空消息（空行是旧日志里最难排查的噪声）
    if (logMessage.empty())
    {
        logMessage = name;
    }

    // 日志写在锁外：日志模块自己有锁，两把锁不要嵌套
    if (logAsError)
    {
        ShineLog::log(LogLevel::Error, moduleName(), step, logMessage, site);
    }
    else
    {
        ShineLog::log(LogLevel::Info, moduleName(), step, logMessage, site);
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
    // 指针打印出来本来就带 0x 前缀，这里再写一个就成了 0x0x7ffe...
    builder << "[" << moduleName() << "@ " << static_cast<const void *>(this) << "]";
    const std::string text = builder.str();
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, text);
}
