#include "core/Application.h"
#include "tool/PasswordTool.h"

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
    // UI loop runs in its own worker, while the main thread waits for it to finish.
    PasswordTool passwordTool;
    if (!passwordTool.verifyConsole())
        return;
    _uiWorker = std::thread([this]()
                            { _uiThread.run(); });

    if (_uiWorker.joinable())
    {
        _uiWorker.join();
    }
}

void Application::showStartupMessage()
{
    std::cout << "InkingBackendFramework started. Type help to view commands." << std::endl;
    setStageStatus(STAGE_STARTUP_MESSAGE, "STARTUP_MESSAGE", true, "Application startup message printed");
    setStageDetail(STAGE_STARTUP_MESSAGE, "启动前的基础提示阶段", "如果没有看到启动提示，请检查标准输出是否可用。");
}
