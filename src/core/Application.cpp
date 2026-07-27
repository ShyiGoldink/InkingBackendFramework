#include "core/Application.h"

#include <iostream>

namespace
{
constexpr int STAGE_STARTUP_MESSAGE = 1;
}

Application::Application()
{
    setStageDetail(
        STAGE_STARTUP_MESSAGE,
        "启动前的基础提示阶段",
        "如果没有看到启动提示，请检查标准输出是否可用。");
}

Application::~Application() = default;

std::string Application::moduleName() const
{
    return "Application";
}

void Application::run()
{
    showStartupMessage();
    _uiThread.run();
}

void Application::showStartupMessage()
{
    std::cout << "InkingBackendFramework started. Type help to view commands." << std::endl;
    setStageStatus(STAGE_STARTUP_MESSAGE, "STARTUP_MESSAGE", true, "Application startup message printed");
}
