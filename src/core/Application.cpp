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
    // 数据后端：优先 MySQL（云服务器部署时开启 INKING_ENABLE_MYSQL），
    // 连不上时回退本地假数据，保证本地也可以直接联调。
    _senseDataService.initialize();
    // 协议消息交给传感业务服务处理：GET_TIME / GET_DATA_* / SEND_SENSE_DATA
    _tcpServer.setMessageHandler([this](const NetMessage &message)
                                 { return _senseDataService.handle(message); });
    _tcpServer.start();
    //ui线程是主线程
    _uiWorker = std::thread([this]()
                            { _uiThread.run(); });

    if (_uiWorker.joinable())
    {
        _uiWorker.join();
    }
    _tcpServer.stop();
}

void Application::showStartupMessage()
{
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "InkingBackendFramework已经启动。输入help来查看指令");
    setStageStatus(STAGE_STARTUP_MESSAGE, "打印启动提示", true, "启动提示已打印");
    setStageDetail(STAGE_STARTUP_MESSAGE, "启动前的基础提示阶段", "如果没有看到启动提示，请检查标准输出是否可用。");
}
