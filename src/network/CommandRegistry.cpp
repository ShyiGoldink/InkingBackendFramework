#include "network/CommandRegistry.h"

#include "basic/ShineLog.h"
#include "network/NetworkStage.h"

#include <algorithm>
#include <vector>

CommandRegistry &CommandRegistry::instance()
{
    // 懒加载单例：第一次被注册或查询时构造。
    // 这里不能是「第一次入队才构造」那种写法——网络线程和注册代码可能同时到。
    static CommandRegistry registry;
    return registry;
}

bool CommandRegistry::registerCommand(const std::string &command, Handler handler)
{
    if (command.empty() || !handler)
    {
        return false;
    }

    if (_frozen)
    {
        // 冻结后还来注册，说明调用顺序写错了：此时分片可能已经在查表，
        // 往里插元素会让正在遍历的读端看到不一致的表。
        ShineLog::log(LogLevel::Error, kNetModuleName, NetworkStage::kShard,
                      "指令注册表已冻结，拒绝注册：" + command);
        return false;
    }

    const auto inserted = _handlers.emplace(command, std::move(handler));
    if (!inserted.second)
    {
        ShineLog::log(LogLevel::Error, kNetModuleName, NetworkStage::kShard,
                      "指令名重复注册，已忽略：" + command);
        return false;
    }
    return true;
}

void CommandRegistry::freeze()
{
    _frozen = true;
}

const CommandRegistry::Handler *CommandRegistry::find(const std::string &command) const
{
    const auto iter = _handlers.find(command);
    if (iter == _handlers.end())
    {
        return nullptr;
    }
    return &iter->second;
}

std::string CommandRegistry::describe() const
{
    std::vector<std::string> names;
    names.reserve(_handlers.size());
    for (const auto &entry : _handlers)
    {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());

    std::string text;
    for (const auto &name : names)
    {
        if (!text.empty())
        {
            text += "、";
        }
        text += name;
    }
    return text.empty() ? "（空）" : text;
}
