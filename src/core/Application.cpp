#include "core/Application.h"
#include "tool/PasswordTool.h"
#include "ui/UIMessageLibrary.h"
#include "network/Net.h"
#include "thread/TaskQueueLoop.h"

#include <iostream>
#include <cstdlib>
#include <any>
#include <string>
#include <vector>

namespace
{
    constexpr int STAGE_STARTUP_MESSAGE = 1;
    constexpr int STAGE_AUTO_NETWORK = 2;

    /**
     * 免密码控制台启动网络服务的开关（给自动化/无人值守用）。
     *
     * 密码是用来保护「有人坐在控制台前面」这个场景的；当程序被脚本、systemd、容器
     * 或者测试脚本拉起时，stdin 往往不是一个交互终端，人工输密码这件事根本不成立。
     * 所以这里给一个显式开关：设了它才跳过密码，并直接启动网络服务。
     * 用法：INKING_AUTO_NETWORK=1 ./InkingBackendFramework
     */
    bool autoNetworkRequested()
    {
        const char *value = std::getenv("INKING_AUTO_NETWORK");
        if (value == nullptr)
        {
            return false;
        }
        const std::string text = value;
        return text == "1" || text == "true" || text == "yes";
    }
}

Application::Application()
{
    registerToStatusChecker();
}

Application::~Application() = default;

void Application::run()
{
    showStartupMessage();
    PasswordTool passwordTool;
    if (!autoNetworkRequested())
    {
        if (!passwordTool.verifyConsole())
            return;
    }
    else
    {
        setStageStatus(STAGE_AUTO_NETWORK, "免交互启动网络服务", true, "由 INKING_AUTO_NETWORK 触发");
    }

    // ui线程是主线程
    _uiWorker = std::thread([this]()
                            { _uiThread.run(); });
    Net net;

    if (autoNetworkRequested())
    {
        // 网络服务跑在 TaskQueueLoop 的后台线程上，所以这里不受控制台是否可用影响
        Task<std::any> networkTask;
        networkTask.action = [&net](const std::vector<std::any> &) -> std::any
        {
            net.run();
            return {};
        };
        TaskQueueLoop::instance().addTask(std::move(networkTask));
    }

    if (_uiWorker.joinable())
    {
        _uiWorker.join();
    }
}

void Application::showStartupMessage()
{
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "InkingBackendFramework已经启动。输入help来查看指令");
    setStageStatus(STAGE_STARTUP_MESSAGE, "打印启动提示", true, "启动提示已打印");
    setStageDetail(STAGE_STARTUP_MESSAGE, "启动前的基础提示阶段", "如果没有看到启动提示，请检查标准输出是否可用。");
}
