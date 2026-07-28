#include "core/Application.h"
#include <thread>
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

std::string Application::moduleName() const
{
    return "Application";
}

void Application::run()
{
    showStartupMessage();
    // 在另一个线程中运行ui，以避免主线程阻塞UI线程
    // 注意，之后要结束程序的函数时，需要确保UI线程已经结束，否则可能会出现未定义行为
    std::thread([this]()
                { _uiThread.run(); })
        .detach();
}

void Application::showStartupMessage()
{
    std::cout << "InkingBackendFramework started. Type help to view commands." << std::endl;
    setStageStatus(STAGE_STARTUP_MESSAGE, "STARTUP_MESSAGE", true, "Application startup message printed");
    setStageDetail(STAGE_STARTUP_MESSAGE, "启动前的基础提示阶段", "如果没有看到启动提示，请检查标准输出是否可用。");
}
