#include "ui/UIThread.h"
#include "ui/UIMessageLibrary.h"

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
    _status = UIStastus::getInput;
    registerStages();
    registerToStatusChecker();
    // 向UIMessageLibrary传入回调
    // 当添加了message之后，就通过回调函数进行处理
    UIMessageLibrary::regnsisCallback([this]()
                                      { this->_status = UIStastus::quitInput; });
}

UIThread::~UIThread() = default;

void UIThread::run()
{
    setStageStatus(STAGE_START_LOOP, "启动交互循环", true, "控制台交互已启动");
    _running = true;

    std::string input;
    // runing时就是在处理，勉强看作是一帧一帧在跑也可以
    // 但是如果使用getline会阻塞后续内容
    // 除非输入是另一个线程
    while (_running)
    {
        switch (_status)
        {
            // 处理用户输入
        case UIStastus::getInput:

            break;
            // 处理消息
        case UIStastus::addMessage:
            break;
            // 阻塞用户输入
        case UIStastus::quitInput:
            break;
        default:
            break;
        }
        printPrompt();
        if (!std::getline(std::cin, input))
        {
            setStageStatus(STAGE_READ_INPUT, "读取控制台输入", false, "输入流已关闭");
            break;
        }

        setStageStatus(STAGE_READ_INPUT, "读取控制台输入", true, "收到控制台输入");
        const CommandResult result = _commandCenter.execute(input);
        setStageStatus(STAGE_EXECUTE_COMMAND, "执行控制台命令", true, "命令处理完成");

        if (result == CommandResult::Quit)
        {
            _running = false;
        }
    }

    setStageStatus(STAGE_STOP_LOOP, "停止交互循环", true, "控制台交互已结束");
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
