#include "command/CommandLibrary.h"
#include "command/CommandCenter.h"
#include "ui/UIMessageLibrary.h"
#include <iomanip>
#include <iostream>
#include <sstream>

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
                     std::ostringstream statusBuilder;
                     const auto &stages = module->getStage();
                     for (const auto &stage : stages)
                     {
                         // 阶段编号要和日志里的 "模块:编号" 对得上，所以这里必须显示 step。
                         const std::string number = [&stage]()
                         {
                             std::ostringstream text;
                             text << std::setw(2) << std::setfill('0') << stage.step;
                             return text.str();
                         }();

                         // 只用 setStageDetail 登记过、函数还没跑到的阶段是没有名字的，
                         // 这种阶段既不能说"通过"也没有内容可说，单独显示成"未运行"。
                         const bool neverRan = stage.name.empty();
                         if (neverRan)
                         {
                             const std::string label = !stage.description.empty()
                                                           ? stage.description
                                                           : "阶段 " + number;
                             statusBuilder << "  [未运行] [" << number << "] " << label << "\n";
                             continue;
                         }

                         // 次数与最近时间来自内存里的计数：日志为了不刷屏会折叠重复记录，
                         // 但"这个阶段跑了多少次、最后一次是什么时候"依然要看得见。
                         statusBuilder << "  [" << (stage.status ? "通过" : "失败") << "] [" << number << "] "
                                       << stage.name << " - " << stage.message << "  (×" << stage.count;
                         if (!stage.lastSeen.empty())
                         {
                             statusBuilder << ", 最近 " << stage.lastSeen;
                         }
                         statusBuilder << ")";
                         if (!stage.status && !stage.suggestion.empty())
                         {
                             statusBuilder << "  建议: " << stage.suggestion;
                         }
                         statusBuilder << "\n";
                     }
                     const std::string statusText = statusBuilder.str();
                     UIMessageLibrary::addMessage(MessageType::normal, 0.0f, statusText);
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
             UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "控制台循环已停止.");
             return CommandResult::Quit;
         },
         false}};
}
