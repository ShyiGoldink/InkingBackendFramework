#ifndef INKING_BACKEND_FRAMEWORK_CORE_PROCESS_SIGNAL_H
#define INKING_BACKEND_FRAMEWORK_CORE_PROCESS_SIGNAL_H

#include <csignal>

/**
 * @brief 进程终止信号的统一入口。
 *
 * 存在的原因：常驻模式下**进程何时结束不由控制台决定**，而由操作系统发来的信号决定。
 * 默认的信号动作会立刻杀死进程，跳过 atexit 与 ShineLog::shutdown()，
 * 于是"退出时的全量快照"永远写不出来——恰好是排查"服务为什么被停掉"最需要的那份证据。
 *
 * 这里的做法是：处理函数只做一件事，把 sig_atomic_t 置位；真正的收尾放在主线程里，
 * 因为信号处理函数里能安全调用的东西太少（malloc、mutex、日志全都不行）。
 *
 * 只允许在主线程调用 install()，且在启动早期调用；其它线程随时可读 terminationRequested()。
 */
namespace ProcessSignal
{
    /**
     * @brief 安装 SIGINT / SIGTERM / SIGHUP（Windows 上是控制台事件）处理。
     *
     * 只在常驻模式下安装：交互模式下 Ctrl+C 的语义是"中断当前输入行"，
     * 由 replxx 在原始模式下自己处理，装了我们自己的处理函数反而会抢掉它。
     */
    void install();

    /** 是否已经收到终止信号。可以在任意线程、任意频率调用。 */
    bool terminationRequested();

    /**
     * @brief 由程序内部主动请求终止（等价于收到一次信号）。
     *
     * 给"外部条件要求退出但没人发信号"的场景用，例如将来的远程 stop 命令。
     */
    void requestTermination();

    /** 收到的是哪个信号（SIGINT/SIGTERM/SIGHUP）；未收到时为 0，Windows 控制台事件同样记 0 */
    int terminationSignal();
}

#endif // INKING_BACKEND_FRAMEWORK_CORE_PROCESS_SIGNAL_H
