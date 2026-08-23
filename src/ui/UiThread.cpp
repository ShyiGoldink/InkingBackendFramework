#include "ui/UIThread.h"

#include <iostream>
#include <string>

namespace
{
    constexpr int STAGE_START_LOOP = 1;
    constexpr int STAGE_READ_INPUT = 2;
    constexpr int STAGE_EXECUTE_COMMAND = 3;
    constexpr int STAGE_STOP_LOOP = 4;
}

UIThread::UIThread()
{
    registerStages();
    registerToStatusChecker();
}

UIThread::~UIThread() = default;


void UIThread::run()
{
    setStageStatus(STAGE_START_LOOP, "START_LOOP", true, "控制台交互已启动");
    _running = true;

    std::string input;
    while (_running)
    {
        printPrompt();
        if (!std::getline(std::cin, input))
        {
            setStageStatus(STAGE_READ_INPUT, "READ_INPUT", false, "输入流已关闭");
            break;
        }

        setStageStatus(STAGE_READ_INPUT, "READ_INPUT", true, "收到控制台输入");
        const CommandResult result = _commandCenter.execute(input);
        setStageStatus(STAGE_EXECUTE_COMMAND, "EXECUTE_COMMAND", true, "命令处理完成");

        if (result == CommandResult::Quit)
        {
            _running = false;
        }
    }

    setStageStatus(STAGE_STOP_LOOP, "STOP_LOOP", true, "控制台交互已结束");
}

void UIThread::registerStages()
{
    setStageDetail(STAGE_START_LOOP, "启动控制台交互循环", "如果启动失败，请检查控制台模块是否被正确调用。");
    setStageDetail(STAGE_READ_INPUT, "读取管理员输入", "如果读取失败，请检查标准输入流是否已关闭。");
    setStageDetail(STAGE_EXECUTE_COMMAND, "执行控制台命令", "如果执行失败，请检查命令回调函数。");
    setStageDetail(STAGE_STOP_LOOP, "停止控制台交互循环", "如果无法退出，请检查 exit 命令和循环状态。");
}


void UIThread::printPrompt() const
{
    std::cout << "\033[33mInking> \033[0m";
}
