#include "ui/UIThread.h"
#include "basic/ShineLog.h"
#include "core/ConsoleEnvironment.h"
#include "core/ProcessSignal.h"
#include "ui/UIMessageLibrary.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

#include "replxx.hxx"

namespace
{
    constexpr int STAGE_START_LOOP = 1;
    constexpr int STAGE_READ_INPUT = 2;
    constexpr int STAGE_EXECUTE_COMMAND = 3;
    constexpr int STAGE_STOP_LOOP = 4;
    constexpr int STAGE_OUTPUT = 5;
    constexpr int STAGE_RESIDENT = 6;

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

    /** 单行输入长度上限：这是管理员命令，不是数据通道，超长只可能是粘贴事故或误连的管道 */
    constexpr std::size_t kMaxInputLength = 4096;

    /** 常驻等待的轮询间隔：500ms 足够让 SIGTERM 在半秒内被看见，又不至于空转烧 CPU */
    constexpr auto kResidentPollInterval = std::chrono::milliseconds(500);

    /** 心跳间隔（按轮询次数折算）：每 10 轮更新一次状态表，即 5 秒一次 */
    constexpr int kResidentHeartbeatTicks = 10;

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

void UIThread::renderQueuedMessages(const std::function<void(const std::string &)> &writeOut)
{
    // 这一层不写自检：它每 50 毫秒轮询一次，只要队列不是空的就写一条日志，
    // 网络跑起来之后会把没有轮转的日志文件撑爆。输出失败由调用方兜底记录。
    const std::vector<Message> messages = UIMessageLibrary::drainMessages();
    for (const Message &message : messages)
    {
        writeOut(formatMessage(message));
    }
}

void UIThread::run(bool stayAliveWhenInputClosed)
{
    setStageStatus(STAGE_START_LOOP, "启动交互循环", true, "控制台交互已启动");
    _running.store(true);

    // 只有真正的终端才能用 replxx；管道、/dev/null、systemd 下必须逐行读取。
    const bool useReplxx = ConsoleEnvironment::stdinIsTerminal();
    const bool echoPrompt = ConsoleEnvironment::stdoutIsTerminal();

    std::unique_ptr<replxx::Replxx> rx;
    if (useReplxx)
    {
        rx = std::make_unique<replxx::Replxx>();
    }
    _replxxActive.store(rx != nullptr);

    // 统一出口：交互模式交给 replxx 重绘整行，管道模式直接写标准输出（加锁，别和提示符插队）。
    auto writeOut = [this, &rx](const std::string &text)
    {
        if (rx && _replxxActive.load())
        {
            // write() 而不是 print()：内容里可能有 % 之类的字符，按长度写不会当成格式串。
            // 在别的线程里调用时，replxx 会把消息排队，等它自己那一轮循环再输出。
            rx->write(text.data(), static_cast<int>(text.size()));
            return;
        }

        std::lock_guard<std::mutex> lock(_outputMutex);
        std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
        std::cout.flush();
    };

    std::thread outputThread([this, writeOut]()
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
                                         renderQueuedMessages(writeOut);
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

    bool inputClosed = false;
    while (_running.load() && !ProcessSignal::terminationRequested())
    {
        // 心跳阶段：只更新状态表，不写日志。
        // "等待用户输入"每敲一条命令就重复一次，它证明的是"循环还活着"，
        // 这件事在 status 命令里看执行次数就够了，不需要在日志里刷屏。
        setStageStatus(STAGE_READ_INPUT, "读取控制台输入", true, "等待用户输入", StageLogPolicy::Silent);

        std::string input;
        if (rx)
        {
            // 提示符交给 replxx：它自己画，也自己重绘，我们不再往控制台上写提示符
            const char *line = rx->input(kPrompt);
            if (line == nullptr)
            {
                inputClosed = true;
                break;
            }
            input = line;
        }
        else
        {
            if (echoPrompt)
            {
                std::lock_guard<std::mutex> lock(_outputMutex);
                std::cout << kPrompt << std::flush;
            }
            if (!readLineFromPipe(input))
            {
                inputClosed = true;
                break;
            }
        }

        if (input.empty())
        {
            // 直接回车：什么都不用做，下一轮会自己画一个新的提示符
            continue;
        }

        // 先把"用户到底输入了什么"记下来，这是整份日志的定位锚点。
        // 旧日志只有"执行控制台命令 - 命令处理完成"，复盘时完全看不出操作内容。
        ShineLog::command(input);

        setStageStatus(STAGE_EXECUTE_COMMAND, "执行控制台命令", true, "开始执行", StageLogPolicy::Silent);
        const CommandResult result = _commandCenter.execute(input);

        if (result == CommandResult::Quit)
        {
            _running.store(false);
        }
    }

    // 输入关闭 ≠ 进程结束。这是常驻模式与交互模式唯一的语义差别，
    // 也是"在 Linux 上作为进程运行"最容易踩的一脚：systemd 下 stdin 是 /dev/null，
    // 一启动就读到 EOF，旧逻辑会立刻退出，服务永远起不来。
    if (inputClosed && stayAliveWhenInputClosed && !ProcessSignal::terminationRequested())
    {
        waitAsResident();
    }

    _running.store(false);

    if (outputThread.joinable())
    {
        outputThread.join();
    }

    setStageStatus(STAGE_STOP_LOOP, "停止交互循环", true, "控制台交互已结束");
}

bool UIThread::readLineFromPipe(std::string &line)
{
    line.clear();

#ifndef _WIN32
    // 逐字节读 + 带超时的 poll，而不是 std::getline：
    // signal() 在 glibc 上是 BSD 语义（SA_RESTART），阻塞中的 read 会被自动重启，
    // 于是 SIGTERM 到达后进程仍卡在等输入上，外部看起来就是"关不掉"。
    // poll 每 200ms 醒一次顺便检查终止标志，是这里唯一可靠的做法。
    while (!ProcessSignal::terminationRequested())
    {
        pollfd descriptor{};
        descriptor.fd = STDIN_FILENO;
        descriptor.events = POLLIN;

        const int ready = ::poll(&descriptor, 1, 200);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ready == 0)
        {
            continue;
        }

        char character = 0;
        const ssize_t received = ::read(STDIN_FILENO, &character, 1);
        if (received == 0)
        {
            return false; // EOF：管道写端关闭，或 stdin 就是 /dev/null
        }
        if (received < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            return false;
        }

        if (character == '\n')
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back(); // Windows 换行经管道传过来会带 \r
            }
            return true;
        }

        line.push_back(character);
        if (line.size() >= kMaxInputLength)
        {
            // 截断而不是继续增长：否则一条没有换行的管道输入能把内存吃干
            return true;
        }
    }
    return false;
#else
    // Windows 上没有 poll(2)，也没有"服务模式靠信号唤醒"的需求（控制台事件由
    // SetConsoleCtrlHandler 处理），这里直接用阻塞读，行为与改造前一致。
    return static_cast<bool>(std::getline(std::cin, line));
#endif
}

void UIThread::waitAsResident()
{
    // 从这里开始主线程不再调用 replxx 的 input()，它的队列也就没人来画了；
    // 先关掉这个开关，下面的提示与后续消息才会直接写到标准输出上。
    _replxxActive.store(false);

    UIMessageLibrary::addMessage(MessageType::normal, 0.0f,
                                 "标准输入已关闭，进程继续运行；结束进程请发送 SIGTERM/SIGINT。");
    setStageStatus(STAGE_RESIDENT, "常驻等待终止信号", true, "控制台已关闭，进程保持运行",
                   StageLogPolicy::Silent);

    int heartbeatTicks = 0;
    while (!ProcessSignal::terminationRequested())
    {
        std::this_thread::sleep_for(kResidentPollInterval);

        // 每 5 秒更新一次状态表（Silent：不写日志，只涨计数）。
        // 没有控制台时，这一行的计数在涨就是"进程还活着"的唯一证据。
        if (++heartbeatTicks % kResidentHeartbeatTicks == 0)
        {
            setStageStatus(STAGE_RESIDENT, "常驻等待终止信号", true, "控制台已关闭，进程保持运行",
                           StageLogPolicy::Silent);
        }
    }

    const int signalNumber = ProcessSignal::terminationSignal();
    setStageStatus(STAGE_RESIDENT, "常驻等待终止信号", true,
                   "收到终止信号" + (signalNumber != 0 ? "(" + std::to_string(signalNumber) + ")" : "") +
                       "，准备退出");
}

void UIThread::registerStages()
{
    setStageDetail(STAGE_START_LOOP, "启动控制台交互循环", "如果启动失败，请检查控制台模块是否被正确调用。");
    setStageDetail(STAGE_READ_INPUT, "读取管理员输入", "如果读取失败，请检查标准输入流是否已关闭。");
    setStageDetail(STAGE_EXECUTE_COMMAND, "执行控制台命令", "如果执行失败，请检查命令回调函数。");
    setStageDetail(STAGE_STOP_LOOP, "停止控制台交互循环", "如果无法退出，请检查 exit 命令和循环状态。");
    setStageDetail(STAGE_OUTPUT, "输出控制台消息", "如果输出失败，请检查标准输出是否仍然可用。");
    setStageDetail(STAGE_RESIDENT, "控制台关闭后等待终止信号",
                   "如果进程应该结束却一直不退出，请检查是否收到 SIGTERM/SIGINT。");
}
