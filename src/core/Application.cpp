#include "core/Application.h"
#include "tool/PasswordTool.h"
#include "ui/UIMessageLibrary.h"

#include <iostream>

namespace
{
    constexpr int STAGE_STARTUP_MESSAGE = 1;
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
    if (!passwordTool.verifyConsole())
        return;
    //ui线程是主线程
    _uiWorker = std::thread([this]()
                            { _uiThread.run(); });

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
