#include "command/CommandLibrary.h"
#include "basic/ShineLog.h"
#include "tool/LogReportTool.h"
#include "ui/UIMessageLibrary.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{
    /** 日志相关命令统一走消息队列输出，和控制台其它结果保持同一种排版。 */
    void say(const std::string &text)
    {
        UIMessageLibrary::addMessage(MessageType::normal, 0.0f, text);
    }

    std::string joinArgs(const std::vector<std::string> &args, std::size_t begin)
    {
        std::string text;
        for (std::size_t index = begin; index < args.size(); ++index)
        {
            if (!text.empty())
            {
                text += ' ';
            }
            text += args[index];
        }
        return text;
    }

    std::size_t parseCount(const std::string &text, std::size_t fallback, std::size_t upper)
    {
        if (text.empty())
        {
            return fallback;
        }
        try
        {
            const long long value = std::stoll(text);
            if (value <= 0)
            {
                return fallback;
            }
            return static_cast<std::size_t>(std::min<long long>(value, static_cast<long long>(upper)));
        }
        catch (const std::exception &)
        {
            return fallback;
        }
    }

    std::string levelToText(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Fatal:
            return "fatal";
        }
        return "info";
    }

    const char *kUsage =
        "日志工具用法：\n"
        "  log tail [n]                 最近 n 条日志（默认 50，读内存缓冲不碰磁盘）\n"
        "  log grep <关键字> [模块]     在缓冲中检索\n"
        "  log level [级别]             查看/调整落盘阈值（trace|debug|info|warn|error）\n"
        "  log stats                    统计：总数、折叠、限流、按级别/模块\n"
        "  log snapshot                 立即写一份完整状态快照\n"
        "  log mark <文字>              打一个 MARK 锚点，方便标记复现起点\n"
        "  log open                     打印当前日志文件路径\n"
        "  log report                   从当前日志生成 HTML 报告";
}

std::vector<Command> CommandLibrary::logCommands() const
{
    return {
        {"log",
         {"logs"},
         "日志工具：tail / grep / level / stats / snapshot / mark / open / report",
         [](const std::vector<std::string> &args)
         {
             if (args.empty())
             {
                 say(kUsage);
                 return CommandResult::Continue;
             }

             const std::string sub = args[0];

             if (sub == "tail")
             {
                 const std::size_t count = parseCount(args.size() > 1 ? args[1] : "", 50, 500);
                 const auto lines = ShineLog::tail(count);
                 if (lines.empty())
                 {
                     say("日志缓冲为空。");
                     return CommandResult::Continue;
                 }
                 std::string text;
                 for (const std::string &line : lines)
                 {
                     text += line;
                     text += '\n';
                 }
                 say(text);
                 return CommandResult::Continue;
             }

             if (sub == "grep")
             {
                 if (args.size() < 2)
                 {
                     say("用法：log grep <关键字> [模块]");
                     return CommandResult::Continue;
                 }
                 const std::string module = args.size() > 2 ? args[2] : std::string();
                 const auto lines = ShineLog::grep(args[1], module);
                 if (lines.empty())
                 {
                     say("没有匹配的记录。");
                     return CommandResult::Continue;
                 }
                 std::string text;
                 const std::size_t limit = std::min<std::size_t>(lines.size(), 200);
                 for (std::size_t index = 0; index < limit; ++index)
                 {
                     text += lines[index];
                     text += '\n';
                 }
                 if (lines.size() > limit)
                 {
                     text += "…… 另有 " + std::to_string(lines.size() - limit) + " 条未显示\n";
                 }
                 say(text);
                 return CommandResult::Continue;
             }

             if (sub == "level")
             {
                 if (args.size() < 2)
                 {
                     say("当前日志阈值：" + levelToText(ShineLog::level()));
                     return CommandResult::Continue;
                 }
                 LogLevel parsed = LogLevel::Info;
                 if (!ShineLog::parseLevel(args[1], parsed))
                 {
                     say("无法识别的级别：" + args[1] + "（可用 trace/debug/info/warn/error/fatal）");
                     return CommandResult::Continue;
                 }
                 ShineLog::setLevel(parsed);
                 say("日志阈值已调整为：" + levelToText(parsed));
                 return CommandResult::Continue;
             }

             if (sub == "stats")
             {
                 say(ShineLog::statistics());
                 return CommandResult::Continue;
             }

             if (sub == "snapshot")
             {
                 ShineLog::snapshot("manual");
                 say("已写入状态快照。");
                 return CommandResult::Continue;
             }

             if (sub == "mark")
             {
                 const std::string text = joinArgs(args, 1);
                 if (text.empty())
                 {
                     say("用法：log mark <文字>");
                     return CommandResult::Continue;
                 }
                 ShineLog::mark(text);
                 say("已打点：" + text);
                 return CommandResult::Continue;
             }

             if (sub == "open")
             {
                 const std::string path = ShineLog::filePath();
                 say(path.empty() ? "日志尚未初始化。" : path);
                 return CommandResult::Continue;
             }

             if (sub == "report")
             {
                 const std::string path = ShineLog::filePath();
                 if (path.empty())
                 {
                     say("日志尚未初始化，无法生成报告。");
                     return CommandResult::Continue;
                 }
                 const std::string report = LogReportTool::generateFromFile(path);
                 say(report.empty() ? "报告生成失败：无法写入或读取日志文件。" : ("报告已生成：" + report));
                 return CommandResult::Continue;
             }

             say("未知的日志子命令：" + sub + "\n" + kUsage);
             return CommandResult::Continue;
         },
         false}};
}
