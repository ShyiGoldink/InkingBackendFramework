#include "core/Application.h"
#include "core/CommandLine.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    constexpr int kExitCodeOk = 0;
    constexpr int kExitCodePasswordRejected = 1;
    constexpr int kExitCodeBadArguments = 2;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // 与 README 保持一致：源码与程序输出统一使用 UTF-8。
    // 不设置的话，Windows 控制台默认按 GBK 解码，中文会显示成乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 命令行解析放在程序最前面，且不依赖任何全局对象：
    // --help / --version 不应该顺带初始化日志、打开数据库、装信号处理。
    const CommandLineOptions options = CommandLine::parse(argc, argv);

    switch (options.mode)
    {
    case StartupMode::Help:
        CommandLine::printUsage(std::cout);
        return kExitCodeOk;

    case StartupMode::Version:
        CommandLine::printVersion(std::cout);
        return kExitCodeOk;

    case StartupMode::Invalid:
        // 参数错误写 stderr：`--help > /dev/null` 与 `--bad 2> /dev/null` 的语义必须分开
        std::cerr << options.error << "\n使用 --help 查看用法。\n";
        return kExitCodeBadArguments;

    case StartupMode::Interactive:
    case StartupMode::Service:
        break;
    }

    ApplicationOptions applicationOptions;
    applicationOptions.serviceMode = (options.mode == StartupMode::Service);
    applicationOptions.startNetwork = options.startNetwork;

    //这里进入了主线程
    Application app;
    return app.run(applicationOptions) == 0 ? kExitCodeOk : kExitCodePasswordRejected;
}
