#include "command/CommandLibrary.h"
#include "command/CommandCenter.h"
#include <iostream>

std::vector<Command> CommandLibrary::uiCommands() const
{
    return {
        {"help",
         {"-h", "-help", "--help", "?", "？"},
         "提供可供使用的命令列表",
         [](const std::vector<std::string> &)
         {
             auto modules = ShineStatusChecker::getModules("CommandCenter");
             for (auto *module : modules)
             {
                 if (module == nullptr)
                     continue;

                 // 尝试转型为 CommandCenter
                 auto *cmdCenter = dynamic_cast<CommandCenter *>(module);
                 if (cmdCenter != nullptr)
                 {
                     cmdCenter->sayMyName();
                     cmdCenter->printHelp();
                 }
             }
             return CommandResult::Continue;
         },
         false},
        {"status",
         {"stat", "health"},
         "显示控制台模块",
         [](const std::vector<std::string> &)
         {
             const auto moduleNames = ShineStatusChecker::getAllModuleNames();
             for (const auto &moduleName : moduleNames)
             {
                 const auto modules = ShineStatusChecker::getModules(moduleName);
                 for (auto *module : modules)
                 {
                     if (module == nullptr)
                     {
                         continue;
                     }

                     // 先输出模块身份信息
                     module->sayMyName();
                     // 再逐条展示该模块的自检阶段状态
                     const auto &stages = module->getStage();
                     for (const auto &stage : stages)
                     {
                         std::cout << "  [" << (stage.status ? "通过" : "失败") << "] "
                                   << stage.name << " - " << stage.message;
                         if (!stage.status)
                         {
                             std::cout << " (建议: " << stage.suggestion << ")";
                         }
                         std::cout << std::endl;
                     }
                 }
             }
             return CommandResult::Continue;
         },
         false},
        {"exit",
         {"quit", "bye", "q"},
         "结束控制台交互循环并退出程序",
         [](const std::vector<std::string> &)
         {
             std::cout << "控制台循环已停止." << std::endl;
             return CommandResult::Quit;
         },
         false}};
}
