#include "command/CommandLibrary.h"
#include "command/CommandCenter.h"
#include "ui/UiThread.h"
#include <iostream>

std::vector<Command> CommandLibrary::uiCommands() const
{
    return {
        {
            "help",
            {"-h", "-help", "--help", "?", "？"},
            "提供可供使用的命令列表",
            []()
            {
                auto modules = ShineStatusChecker::getModules("CommandCenter");
                for (auto* module : modules)
                {
                    if (module == nullptr)
                        continue;

                    // 尝试转型为 CommandCenter
                    auto* cmdCenter = dynamic_cast<CommandCenter*>(module);
                    if (cmdCenter != nullptr)
                    {
                        cmdCenter->sayMyName();
                        cmdCenter->printHelp();
                    }
                }
                return CommandResult::Continue;
            },
            false
        },
        {
            "status",
            {"stat", "health"},
            "显示控制台模块",
            []()
            {  UIThread ui;
                ui.sayMyName();
                std::cout << "控制台模块正在正常运行." << std::endl;
                return CommandResult::Continue;
            },
            false
        },
        {
            "exit",
            {"quit", "bye", "q"},
            "结束控制台交互循环并退出程序",
            []()
            {   UIThread ui;
                ui.sayMyName();
                std::cout << "控制台循环已停止." << std::endl;
                return CommandResult::Quit;
            },
            false
        }
    };
}