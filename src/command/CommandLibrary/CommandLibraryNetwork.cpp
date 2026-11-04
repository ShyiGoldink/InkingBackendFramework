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
        {"network-stop",
         {"n-stop"},
         "停止网络程序",
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

                 // 只置标志、不在这里等：真正的停机流程（停接入 → 排空 → 停分片）
                 // 必须跑在网络线程上，控制台线程不能替它做。等完再返回会让控制台
                 // 在那几秒里完全没反应，用户会以为卡死了。
                 net->stop();
                 UIMessageLibrary::addMessage(
                     MessageType::normal, 0.0f,
                     "已请求停止网络服务：先停接入，再排空在飞请求，随后关闭监听。\n");
                 return CommandResult::Continue;
             }

             UIMessageLibrary::addMessage(
                 MessageType::error,
                 0.0f,
                 "没有找到已注册的 Net 模块，无法停止网络程序。\n");
             return CommandResult::Continue;
         },
         false},
        {"network-stat",
         {"n-stat"},
         "查看网络服务统计（连接数与分段计时）",
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

                 // 只读原子计数器：分片线程独占的字段一个都不碰，
                 // 所以这个命令在压测进行中也能安全执行
                 UIMessageLibrary::addMessage(MessageType::normal, 0.0f,
                                              net->profileReport() + "\n");
                 return CommandResult::Continue;
             }

             UIMessageLibrary::addMessage(
                 MessageType::error,
                 0.0f,
                 "没有找到已注册的 Net 模块，无法读取统计。\n");
             return CommandResult::Continue;
         },
         false},
    };
}