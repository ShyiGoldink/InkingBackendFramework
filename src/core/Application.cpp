#include "core/Application.h"

#include <iostream>

Application::Application()
{
    addStage("SAY_HELLO", "初始化前的管理员问候阶段");
}

Application::~Application() = default;

std::string Application::moduleName() const
{
    return "Application";
}

void Application::run()
{
    sayHello();
}

void Application::sayHello()
{
    std::cout << "下午好，管理员。我是玘·蓝。你可以像他一样叫我玘小姐。" << std::endl;
    setStageStatus("SAY_HELLO", true, "玘小姐已上线");
}
