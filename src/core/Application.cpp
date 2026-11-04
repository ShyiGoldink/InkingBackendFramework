#include "core/Application.h"
#include "basic/ShineLog.h"
#include "core/CommandLine.h"
#include "core/ConsoleEnvironment.h"
#include "core/ProcessSignal.h"
#include "network/Net.h"
#include "thread/TaskQueueLoop.h"
#include "tool/PasswordTool.h"
#include "ui/UIMessageLibrary.h"

#include <any>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr int STAGE_STARTUP_MESSAGE = 1;
    constexpr int STAGE_SKIP_PASSWORD = 2;
    constexpr int STAGE_PASSWORD = 3;
    constexpr int STAGE_SERVICE_MODE = 4;
    constexpr int STAGE_NETWORK = 5;
    constexpr int STAGE_SHUTDOWN = 6;
    /**
     * 停止网络服务必须单独占一个编号。
     *
     * 自检表按 step 定位阶段，同一个 step 用两个不同的名字会被判为编号冲突并拒绝更新
     * （"拉起网络服务"与"停止网络服务"就是两个阶段，不是一个阶段的两种状态）。
     */
    constexpr int STAGE_NETWORK_STOP = 7;
    constexpr int STAGE_CONSOLE_ENV = 8;

    /**
     * 网络线程退出的等待上限。
     *
     * 这是**安全网**，不是正常耗时：真正的耗时上限由网络层自己控制
     * （`config/networkConfig.json` 的 `shutdownGraceMs`，默认 5 秒）。
     * 正常停机是「停止接入 → 排空在飞 → 停分片」，全都在那个上限之内完成。
     *
     * 所以这里必须明显大于 `shutdownGraceMs`：早先它是 5 秒，与排空上限相同，
     * 一旦有客户端在停机时赖着不走，排空还没到时限、这边就已经判定「卡死」并硬退
     * （退出码 3），把「优雅停机」变成了「随机硬杀」。
     */
    constexpr std::chrono::seconds kNetworkShutdownTimeout{15};

    /**
     * 免密码控制台启动网络服务的开关（给自动化/无人值守用）。
     *
     * 密码是用来保护「有人坐在控制台前面」这个场景的；当程序被脚本、systemd、容器
     * 或者测试脚本拉起时，stdin 往往不是一个交互终端，人工输密码这件事根本不成立。
     * 所以这里给一个显式开关：设了它才跳过密码，并直接启动网络服务。
     * 用法：INKING_AUTO_NETWORK=1 ./InkingBackendFramework
     */
    bool autoNetworkRequested()
    {
        const char *value = std::getenv("INKING_AUTO_NETWORK");
        if (value == nullptr)
        {
            return false;
        }
        const std::string text = value;
        return text == "1" || text == "true" || text == "yes";
    }
}

Application::Application()
{
    registerToStatusChecker();
}

Application::~Application() = default;

int Application::run(const ApplicationOptions &options)
{
    // 日志必须在任何模块写第一条记录之前就绪：文件、阈值、快照线程都在 init 里定下来。
    // 默认不往控制台写日志；只有显式设置 INKING_LOG_CONSOLE 时才会调用下面的镜像出口。
    ShineLog::init();
    ShineLog::setMirrorSink([](LogLevel level, const std::string &line)
                            { UIMessageLibrary::addMessage(
                                  level >= LogLevel::Error ? MessageType::error : MessageType::normal, 0.0f,
                                  line); });

    // 控制台环境是所有后续行为的输入（要不要密码、控制台用 replxx 还是逐行读、
    // 要不要打提示符），所以第一条自检就登记它。
    // 这一行也是"systemd 下还在要密码"这类问题的第一现场：status 里看到
    // "stdin=终端" 而实际是重定向，就说明判定层出了问题，不必再去猜密码逻辑。
    const bool stdinIsTerminal = ConsoleEnvironment::stdinIsTerminal();
    const bool stdoutIsTerminal = ConsoleEnvironment::stdoutIsTerminal();
    setStageStatus(STAGE_CONSOLE_ENV, "探测控制台环境", true,
                   std::string("stdin=") + (stdinIsTerminal ? "终端" : "非终端") +
                       "，stdout=" + (stdoutIsTerminal ? "终端" : "非终端"));

    if (options.serviceMode)
    {
        // 信号处理必须赶在控制台线程和网络线程起来之前装好：
        // 否则"启动过程中被 Ctrl+C"走的还是默认动作，会跳过日志收尾，
        // 留下一个"服务启动了一半就没了、而且没有最终快照"的现场。
        ProcessSignal::install();
        setStageStatus(STAGE_SERVICE_MODE, "进入常驻模式", true,
                       "标准输入关闭不会结束进程，SIGTERM/SIGINT/SIGHUP 触发有序退出");
    }

    showStartupMessage(options.serviceMode);

    // 常驻模式下如果 stdin 不是终端（systemd、容器、重定向），没人能输入密码，
    // 继续要求密码的唯一结果就是进程立刻退出——那正是这次要修掉的症状。
    // 注意：有终端时仍然要求密码，坐在控制台前的人不该因为加了 --start 就免认证。
    const bool skipPassword = autoNetworkRequested() ||
                              (options.serviceMode && !stdinIsTerminal);

    if (skipPassword)
    {
        const std::string reason = autoNetworkRequested()
                                       ? "由 INKING_AUTO_NETWORK 触发"
                                       : "常驻模式下标准输入不是终端，无人可输密码";
        setStageStatus(STAGE_SKIP_PASSWORD, "跳过控制台密码校验", true, reason);
        // 跳过密码是安全边界的一次移动，必须留下 WARN 级证据：
        // 事后要回答"这个服务是谁在没认证的情况下拉起来的"，只能靠这一条。
        ShineLog::log(LogLevel::Warn, kApplicationModuleName, STAGE_SKIP_PASSWORD,
                      "跳过控制台密码校验：" + reason);
    }
    else
    {
        PasswordTool passwordTool;
        if (!passwordTool.verifyConsole())
        {
            setStageStatus(STAGE_PASSWORD, "校验控制台密码", false, "密码校验失败，进程不启动");
            ShineLog::shutdown();
            return 1;
        }
        setStageStatus(STAGE_PASSWORD, "校验控制台密码", true, "密码校验通过");
    }

    // ui线程是主线程
    _uiWorker = std::thread([this, serviceMode = options.serviceMode]()
                            { _uiThread.run(serviceMode); });

    if (options.startNetwork || autoNetworkRequested())
    {
        // 网络服务跑在 TaskQueueLoop 的后台线程上，所以这里不受控制台是否可用影响
        startNetworkService();
    }

    if (_uiWorker.joinable())
    {
        _uiWorker.join();
    }

    if (!stopNetworkService())
    {
        // 任务队列的静态析构会 join 住那条网络线程，进程会永久挂起。
        // 对一个"关掉就该退出"的进程来说，挂起是比崩溃更糟的失败方式，所以这里硬退；
        // 日志已经 shutdown 过，最终快照照样落地。
        ShineLog::log(LogLevel::Error, kApplicationModuleName, STAGE_SHUTDOWN,
                      "网络线程未能在 " + std::to_string(kNetworkShutdownTimeout.count()) +
                          " 秒内退出，强制结束进程");
        ShineLog::shutdown();
        std::cout.flush();
        std::_Exit(3);
    }

    setStageStatus(STAGE_SHUTDOWN, "进程收尾", true,
                   "控制台与网络均已停止，写入最终快照");

    // 收尾时把最终状态写成快照再关文件；即使这里被跳过，atexit 也会兜底再做一次
    ShineLog::shutdown();
    return 0;
}

void Application::startNetworkService()
{
    if (_networkStarted.exchange(true))
    {
        setStageStatus(STAGE_NETWORK, "拉起网络服务", true, "网络服务已在运行，跳过重复启动");
        return;
    }

    _network = std::make_shared<Net>();
    _networkFinished->store(false);

    // 两个 shared_ptr 按值进任务：网络线程的生命周期可能比 Application 更长，
    // 任何对 this 的捕获都会在退出路径上变成悬空引用。
    auto network = _network;
    auto finished = _networkFinished;

    Task<std::any> networkTask;
    networkTask.action = [network, finished](const std::vector<std::any> &) -> std::any
    {
        network->run();
        finished->store(true);
        return {};
    };
    TaskQueueLoop::instance().addTask(std::move(networkTask));

    setStageStatus(STAGE_NETWORK, "拉起网络服务", true, "网络服务已交给后台线程");
}

bool Application::stopNetworkService()
{
    if (!_network)
    {
        return true;
    }

    // 只置标志、不关 socket：事件循环在自己的线程里退出，连接由它自己清理。
    // 从别的线程 close 监听 fd 会和 accept/poll 抢同一个描述符。
    _network->stop();

    const auto deadline = std::chrono::steady_clock::now() + kNetworkShutdownTimeout;
    while (!_networkFinished->load())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    setStageStatus(STAGE_NETWORK_STOP, "停止网络服务", true, "网络线程已退出");
    return true;
}

void Application::showStartupMessage(bool serviceMode)
{
    std::string banner = std::string(CommandLine::kProgramName) + " " + CommandLine::kVersion + " 已启动（";
    banner += serviceMode ? "常驻模式" : "交互模式";
    banner += "）。输入help来查看指令";
    UIMessageLibrary::addMessage(MessageType::normal, 0.0f, banner);

    if (serviceMode)
    {
        UIMessageLibrary::addMessage(
            MessageType::normal,
            0.0f,
            "标准输入关闭后进程会继续运行；结束进程请发送 SIGTERM/SIGINT，或在控制台输入 exit。");
    }

    const std::string logPath = ShineLog::filePath();
    if (!logPath.empty())
    {
        // 只报路径，不把日志内容倒到控制台：需要时用 log tail / log open 取
        UIMessageLibrary::addMessage(MessageType::normal, 0.0f, "日志文件：" + logPath);
    }
    setStageStatus(STAGE_STARTUP_MESSAGE, "打印启动提示", true, "启动提示已打印");
    setStageDetail(STAGE_STARTUP_MESSAGE, "启动前的基础提示阶段", "如果没有看到启动提示，请检查标准输出是否可用。");
}
