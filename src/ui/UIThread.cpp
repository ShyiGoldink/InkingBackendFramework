#include "ui/UIThread.h"
#include "ui/UIMessageLibrary.h"

#include <chrono>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include "replxx.hxx"

namespace
{
    constexpr int STAGE_START_LOOP = 1;
    constexpr int STAGE_READ_INPUT = 2;
    constexpr int STAGE_EXECUTE_COMMAND = 3;
    constexpr int STAGE_STOP_LOOP = 4;
    constexpr int STAGE_OUTPUT = 5;

    /**
     * 控制台提示符。
     *
     * 这个字符串必须原样交给 replxx（作为 input() 的入参），不能由我们自己打印。
     *
     * replxx 排版时假定「行首那一小段就是它自己的提示符」：左右移动、删除、上下翻历史
     * 这些键会触发整行重绘，重绘时它先把光标移到「提示符宽度」那一列，再重写这一行。
     * 如果提示符是我们手打上去的，replxx 认为提示符宽度是 0，就会从行首开始重写，
     * 把提示符本身覆盖掉——表现为 Inking> 莫名其妙消失、光标位置乱跳。
     */
    constexpr const char *kPrompt = "\033[33mInking> \033[0m";

    /** 给消息加上颜色前缀，并补一个换行（replxx 需要整行输出，否则提示符会接在消息后面） */
    std::string formatMessage(const Message &message)
    {
        std::string text;
        switch (message.type)
        {
        case MessageType::error:
            text = "\033[31m[ERROR] \033[0m" + message.message;
            break;
        case MessageType::pass:
            text = "\033[32m[PASS] \033[0m" + message.message;
            break;
        case MessageType::normal:
        default:
            text = message.message;
            break;
        }
        text.push_back('\n');
        return text;
    }
}

UIThread::UIThread()
{
    registerStages();
    registerToStatusChecker();
}

UIThread::~UIThread() = default;

void UIThread::renderQueuedMessages(replxx::Replxx &rx)
{
    // 这一层不写自检：它每 50 毫秒轮询一次，只要队列不是空的就写一条日志，
    // 网络跑起来之后会把没有轮转的日志文件撑爆。输出失败由调用方兜底记录。
    const std::vector<Message> messages = UIMessageLibrary::drainMessages();
    for (const Message &message : messages)
    {
        const std::string text = formatMessage(message);
        // write() 而不是 print()：内容里可能有 % 之类的字符，按长度写不会当成格式串。
        // 在别的线程里调用时，replxx 会把消息排队，等它自己那一轮循环再输出。
        rx.write(text.data(), static_cast<int>(text.size()));
    }
}

void UIThread::run()
{
    setStageStatus(STAGE_START_LOOP, "启动交互循环", true, "控制台交互已启动");
    _running.store(true);

    replxx::Replxx rx;

    std::thread outputThread([this, &rx]()
                             {
                                 while (_running.load())
                                 {
                                     UIMessageLibrary::waitForMessage(std::chrono::milliseconds(50));
                                     if (!_running.load())
                                     {
                                         break;
                                     }

                                     try
                                     {
                                         renderQueuedMessages(rx);
                                     }
                                     catch (const std::exception &error)
                                     {
                                         // 控制台写不进去时（例如输出被关掉），异常绝不能从线程函数里逃出去：
                                         // 逃出去会直接终止整个进程。这里记一笔就停掉输出线程。
                                         setStageStatus(
                                             STAGE_OUTPUT,
                                             "输出控制台消息",
                                             false,
                                             std::string("控制台输出失败: ") + error.what());
                                         _running.store(false);
                                         break;
                                     }
                                 }
                             });

    while (_running.load())
    {
        setStageStatus(STAGE_READ_INPUT, "读取控制台输入", true, "等待用户输入");
        // 提示符交给 replxx：它自己画，也自己重绘，我们不再往控制台上写提示符
        const char* line = rx.input(kPrompt);
        if (line == nullptr)
        {
            _running.store(false);
            break;
        }

        std::string input = line;
        if (input.empty())
        {
            // 直接回车：什么都不用做，下一轮 input() 会自己画一个新的提示符
            continue;
        }
        setStageStatus(STAGE_EXECUTE_COMMAND, "执行控制台命令", true, "命令处理完成");
        const CommandResult result = _commandCenter.execute(input);

        if (result == CommandResult::Quit)
        {
            _running.store(false);
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
    setStageDetail(STAGE_OUTPUT, "输出控制台消息", "如果输出失败，请检查标准输出是否仍然可用。");
}
