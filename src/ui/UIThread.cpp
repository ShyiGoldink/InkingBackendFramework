#include "ui/UIThread.h"
#include "ui/UIMessageLibrary.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include "replxx.hxx"

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

void UIThread::renderQueuedMessages()
{
    const auto messages = UIMessageLibrary::drainMessages();
    if (messages.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_consoleMutex);
    std::cout << "\r\033[K";

    for (const auto& message : messages)
    {
        switch (message.type)
        {
        case MessageType::error:
            std::cout << "\033[31m[ERROR] \033[0m" << message.message << "\r\n";
            break;
        case MessageType::pass:
            std::cout << "\033[32m[PASS] \033[0m" << message.message << "\r\n";
            break;
        case MessageType::normal:
        default:
            std::cout << message.message << "\r\n";
            break;
        }
    }

    std::cout << "\r\033[33mInking> \033[0m";
    if (!_liveInput.empty())
    {
        std::cout << _liveInput;
    }
    std::cout << std::flush;
}

void UIThread::run()
{
    setStageStatus(STAGE_START_LOOP, "启动交互循环", true, "控制台交互已启动");
    _running.store(true);

    replxx::Replxx rx;
    rx.set_prompt("\033[33mInking> \033[0m");
    rx.set_modify_callback([this](std::string& line, int& cursorPosition)
                          {
                              std::lock_guard<std::mutex> lock(_consoleMutex);
                              _liveInput = line;
                              _liveCursorPosition = cursorPosition;
                          });

    std::thread outputThread([this]()
                             {
                                 while (_running.load())
                                 {
                                     UIMessageLibrary::waitForMessage(std::chrono::milliseconds(50));
                                     if (!_running.load())
                                     {
                                         break;
                                     }
                                     renderQueuedMessages();
                                 }
                             });

    while (_running.load())
    {
        setStageStatus(STAGE_READ_INPUT, "读取控制台输入", true, "等待用户输入");
        const char* line = rx.input("");
        if (line == nullptr)
        {
            _running.store(false);
            break;
        }

        std::string input = line;
        if (input.empty())
        {   rx.print("\033[33mInking> \033[0m");
            continue;
        }
        setStageStatus(STAGE_EXECUTE_COMMAND, "执行控制台命令", true, "命令处理完成");
        const CommandResult result = _commandCenter.execute(input);

        if (result == CommandResult::Quit)
        {
            _running.store(false);
        }
        else{
            std::lock_guard<std::mutex> lock(_consoleMutex);
            //如果内容为空，那么会导致inking>>丢失
            _liveInput.clear();   // 清空，而不是设置为 input
            _liveCursorPosition = 0;
        }
    }

    if (outputThread.joinable())
    {
        outputThread.join();
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

