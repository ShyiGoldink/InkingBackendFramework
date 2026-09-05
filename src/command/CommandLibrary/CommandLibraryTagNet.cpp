#include "command/CommandLibrary.h"

#include "basic/ShineStatusChecker.h"
#include "net/TcpServer.h"
#include "sense/SenseDataService.h"
#include "ui/UIMessageLibrary.h"

#include <sstream>

namespace
{
    /** 从状态检查器中找到当前进程内的 TcpServer 实例 */
    TcpServer *findTcpServer()
    {
        const auto modules = ShineStatusChecker::getModules(kTcpServerModuleName);
        for (auto *module : modules)
        {
            if (module == nullptr)
            {
                continue;
            }
            if (auto *server = dynamic_cast<TcpServer *>(module))
            {
                return server;
            }
        }
        return nullptr;
    }

    /** 从状态检查器中找到当前进程内的传感业务服务 */
    SenseDataService *findSenseDataService()
    {
        const auto modules = ShineStatusChecker::getModules(kSenseDataServiceModuleName);
        for (auto *module : modules)
        {
            if (module == nullptr)
            {
                continue;
            }
            if (auto *service = dynamic_cast<SenseDataService *>(module))
            {
                return service;
            }
        }
        return nullptr;
    }

    /** 输出 net 命令的帮助提示 */
    void showUsage(const std::string &usage)
    {
        UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "用法: " + usage);
    }
}

std::vector<Command> CommandLibrary::netCommands() const
{
    return {
        {"net-start",
         {"tcp-start", "netopen"},
         "启动 TCP 服务器",
         [](const std::vector<std::string> &)
         {
             TcpServer *server = findTcpServer();
             if (server == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "找不到 TcpServer 模块");
                 return CommandResult::Continue;
             }
             server->sayMyName();
             UIMessageLibrary::quickMessage(server->start(), 0.0f, "TCP 服务器启动");
             return CommandResult::Continue;
         },
         false},
        {"net-stop",
         {"tcp-stop", "netclose"},
         "停止 TCP 服务器并断开全部客户端",
         [](const std::vector<std::string> &)
         {
             TcpServer *server = findTcpServer();
             if (server == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "找不到 TcpServer 模块");
                 return CommandResult::Continue;
             }
             server->sayMyName();
             server->stop();
             UIMessageLibrary::addMessage(MessageType::pass, 0.0f, "TCP 服务器已停止");
             return CommandResult::Continue;
         },
         false},
        {"net-status",
         {"netstat", "tcp-status"},
         "查看 TCP 服务器运行状态",
         [](const std::vector<std::string> &)
         {
             TcpServer *server = findTcpServer();
             if (server == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "找不到 TcpServer 模块");
                 return CommandResult::Continue;
             }

             std::ostringstream builder;
             builder << "TcpServer: " << (server->isRunning() ? "运行中" : "已停止")
                     << " | 端口: " << server->port()
                     << " | 连接数: " << server->clientCount();
             UIMessageLibrary::addMessage(MessageType::normal, 0.0f, builder.str());
             return CommandResult::Continue;
         },
         false},
        {"net-port",
         {"tcp-port", "setport"},
         "设置 TCP 端口（需先 net-stop 再 net-start 生效）",
         [](const std::vector<std::string> &args)
         {
             TcpServer *server = findTcpServer();
             if (server == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "找不到 TcpServer 模块");
                 return CommandResult::Continue;
             }
             if (args.empty())
             {
                 showUsage("net-port <端口>");
                 return CommandResult::Continue;
             }
             if (server->isRunning())
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f,
                                              "服务器运行中不能修改端口，请先 net-stop");
                 return CommandResult::Continue;
             }
             try
             {
                 const int port = std::stoi(args[0]);
                 if (port <= 0 || port > 65535)
                 {
                     throw std::out_of_range("port");
                 }
                 server->setPort(static_cast<uint16_t>(port));
                 UIMessageLibrary::addMessage(MessageType::pass, 0.0f,
                                              "TCP 端口已设置为 " + std::to_string(port));
             }
             catch (...)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "端口必须是 1-65535 的数字");
             }
             return CommandResult::Continue;
         },
         false},
        {"net-clients",
         {"tcp-clients", "netlist"},
         "查看当前已连接的客户端",
         [](const std::vector<std::string> &)
         {
             TcpServer *server = findTcpServer();
             if (server == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f, "找不到 TcpServer 模块");
                 return CommandResult::Continue;
             }
             const auto clients = server->clientDescriptions();
             if (clients.empty())
             {
                 UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "当前没有客户端连接");
                 return CommandResult::Continue;
             }
             std::ostringstream builder;
             builder << "当前客户端 (" << clients.size() << "):\n";
             for (const auto &client : clients)
             {
                 builder << "  " << client << "\n";
             }
             UIMessageLibrary::addMessage(MessageType::normal, 0.0f, builder.str());
             return CommandResult::Continue;
         },
         false},
        {"sense-info",
         {"sense-status", "sensor-info"},
         "查看传感数据后端与数据量",
         [](const std::vector<std::string> &)
         {
             SenseDataService *service = findSenseDataService();
             if (service == nullptr)
             {
                 UIMessageLibrary::addMessage(MessageType::error, 0.0f,
                                              "找不到 SenseDataService 模块");
                 return CommandResult::Continue;
             }
             service->sayMyName();
             UIMessageLibrary::addMessage(MessageType::normal, 0.0f,
                                          "数据后端: " + service->backendName());
             UIMessageLibrary::addMessage(MessageType::normal, 0.0f,
                                          "数据摘要: " + service->storeSummary());
             return CommandResult::Continue;
         },
         false}};
}
