#include <iostream>
#include "tool/UIRegisterTool.h"
#include "command/CommandCenter.h"

UIRegisterTool::~UIRegisterTool() = default;

bool UIRegisterTool::init(CommandCenter &commandCenter)
{
    _commandCenter = &commandCenter;
    return _commandCenter != nullptr;
}

bool UIRegisterTool::registerBasicCommand()
{
    if (_commandCenter)
    {
        _commandCenter->registerCommand({"help",
                                         {"-h", "--help", "?"},
                                         "提供可供使用的命令列表",
                                         [this]()
                                         {
                                             _commandCenter->printHelp();
                                             return CommandResult::Continue;
                                         },
                                         false});

        _commandCenter->registerCommand({"status",
                                         {"stat", "health"},
                                         "显示控制台模块状态",
                                         [this]()
                                         {
                                             std::cout << "控制台模块正在正常运行." << std::endl;
                                             return CommandResult::Continue;
                                         },
                                         false});

        _commandCenter->registerCommand({"exit",
                                         {"quit", "bye", "q"},
                                         "结束控制台交互循环并退出程序",
                                         []()
                                         {
                                             std::cout << "控制台循环已停止." << std::endl;
                                             return CommandResult::Quit;
                                         },
                                         false});
        return true;
    }

    return false;
}
