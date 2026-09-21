#include "command/CommandLibrary.h"

#include "network/Net.h"
#include "ui/UIMessageLibrary.h"
#include "thread/TaskQueueLoop.h"

std::vector<Command> CommandLibrary::netCommands() const
{
    return {
        {"network-run",
         {"n-run"},
         "运行网络程序",
         [](const std::vector<std::string> &)
         {
             const auto modules = ShineStatusChecker::getModules(kNetModuleName);
             for (auto *module : modules)
             {
                 if (module == nullptr)
                 {
                     continue;
                 }

                 auto *net = dynamic_cast<Net *>(module);
                 if (net == nullptr)
                 {
                     continue;
                 }

                 Task<std::any> networkTask;
                 networkTask.action = [net](const std::vector<std::any> &) -> std::any
                 {
                     net->run();
                     return {};
                 };
                 TaskQueueLoop::instance().addTask(std::move(networkTask));
                 return CommandResult::Continue;
             }

             UIMessageLibrary::addMessage(
                 MessageType::error,
                 0.0f,
                 "没有找到已注册的 Net 模块，无法运行网络程序。\n");
             return CommandResult::Continue;
         },
         false},
    };
}