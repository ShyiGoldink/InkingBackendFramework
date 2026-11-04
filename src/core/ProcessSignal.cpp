#include "core/ProcessSignal.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    /**
     * 信号处理函数与主线程之间的唯一通信方式。
     *
     * 必须是 sig_atomic_t：这是标准保证"读写不会被信号打断成半个值"的类型。
     * 换成 std::atomic 在多数实现上也能用，但标准只对 volatile sig_atomic_t 开了这个口子。
     */
    volatile std::sig_atomic_t g_terminated = 0;
    volatile std::sig_atomic_t g_signalNumber = 0;

    extern "C" void handleSignal(int signalNumber)
    {
        if (g_terminated != 0)
        {
            // 第二个信号：说明收尾卡住了（例如网络线程不响应）。这时候不能再等，
            // 否则用户会遇到"按了 Ctrl+C 却退不掉"这种最让人恼火的状态。
            std::_Exit(128 + signalNumber);
        }
        g_signalNumber = signalNumber;
        g_terminated = 1;
    }

#ifdef _WIN32
    /**
     * Windows 没有 SIGTERM：服务停止走的是控制台事件。
     *
     * 返回 TRUE 表示"这个事件我处理了"，系统就不会再执行默认动作（直接杀进程）。
     * 注意 CTRL_CLOSE_EVENT / CTRL_SHUTDOWN_EVENT 只给几秒钟，来不及做完整收尾，
     * 这里只置位——能写多少算多少，剩下的交给日志的 atexit 兜底。
     */
    BOOL WINAPI handleConsoleEvent(DWORD eventType)
    {
        switch (eventType)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_terminated = 1;
            return TRUE;
        default:
            return FALSE;
        }
    }
#endif
}

void ProcessSignal::install()
{
#ifdef _WIN32
    ::SetConsoleCtrlHandler(handleConsoleEvent, TRUE);
#else
    // 用 std::signal 而不是 sigaction：这里不需要 SA_RESTART 之类的精细控制，
    // 阻塞中的 poll/read 靠超时轮询标志位发现终止，不依赖信号打断系统调用。
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    // SIGHUP：终端断开。作为服务挂在终端下运行时，"终端没了"就等于"服务该收尾了"，
    // 否则进程会变成没人管的后台孤儿，还占着端口。
    std::signal(SIGHUP, handleSignal);

    // SIGPIPE：输出端（例如 `| head` 或已退出的日志收集器）关闭后，write 会触发这个信号，
    // 默认动作是直接杀进程。常驻服务不该因为"没人看输出"而死掉：忽略之后
    // write 只返回 EPIPE，iostream 置 badbit，进程继续提供服务。
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

bool ProcessSignal::terminationRequested()
{
    return g_terminated != 0;
}

void ProcessSignal::requestTermination()
{
    g_terminated = 1;
}

int ProcessSignal::terminationSignal()
{
    return static_cast<int>(g_signalNumber);
}
