#include "core/CommandLine.h"

#include <cstdlib>
#include <string>

namespace
{
    /** 读取一个"开关型"环境变量：只有 1/true/yes 算打开，其余（含未设置）都算关闭 */
    bool envFlagEnabled(const char *name)
    {
        const char *value = std::getenv(name);
        if (value == nullptr)
        {
            return false;
        }
        const std::string text = value;
        return text == "1" || text == "true" || text == "yes";
    }
}

CommandLineOptions CommandLine::parse(int argc, char **argv)
{
    CommandLineOptions options;
    // 环境变量先落地，命令行参数再覆盖：显式参数优先于环境变量。
    options.startNetwork = envFlagEnabled("INKING_AUTO_NETWORK");

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];

        if (argument == "--start")
        {
            options.mode = StartupMode::Service;
            // 常驻模式的用途就是"作为进程一直跑"，网络服务是它的主要内容，
            // 没有理由让用户再额外设一个环境变量才肯监听。
            options.startNetwork = true;
        }
        else if (argument == "--help" || argument == "-h")
        {
            options.mode = StartupMode::Help;
        }
        else if (argument == "--version" || argument == "-v")
        {
            options.mode = StartupMode::Version;
        }
        else
        {
            options.mode = StartupMode::Invalid;
            options.error = "无法识别的参数: " + argument;
            return options;
        }
    }

    return options;
}

void CommandLine::printUsage(std::ostream &out)
{
    out << "用法: " << kProgramName << " [选项]\n"
        << "\n"
        << "选项:\n"
        << "  --start         常驻模式：拉起网络服务并进入控制台；标准输入关闭后进程继续运行，\n"
        << "                  收到 SIGTERM / SIGINT / SIGHUP 才退出\n"
        << "  -h, --help      显示本帮助后退出\n"
        << "  -v, --version   显示版本后退出\n"
        << "\n"
        << "不带参数启动时保持交互模式：校验控制台密码，控制台退出（exit 或 Ctrl+D）即结束进程。\n"
        << "\n"
        << "环境变量:\n"
        << "  INKING_AUTO_NETWORK=1   免密码启动并拉起网络服务\n"
        << "  INKING_LOG_LEVEL        日志落盘阈值 trace/debug/info/warn/error/fatal\n"
        << "  INKING_LOG_CONSOLE      把该级别及以上的日志镜像到控制台（默认关闭）\n"
        << "  INKING_LOG_SNAPSHOT=0   关闭定时快照\n"
        << "  INKING_LOG_SNAPSHOT_INTERVAL  快照间隔（秒）\n"
        << "\n"
        << "退出码:\n"
        << "  0  正常结束\n"
        << "  1  控制台密码校验失败\n"
        << "  2  命令行参数错误\n"; 
}

void CommandLine::printVersion(std::ostream &out)
{
    out << kProgramName << " " << kVersion << " (C++20)\n";
}
